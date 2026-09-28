// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceMassCollisionSolverProcessor.h"

#include "MassEntityQuery.h"
#include "MassExecutionContext.h"
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "MassEntityManager.h"
#include "MassMovementFragments.h"
#include "Engine/World.h"
#include "MassSubsystemAccess.h"
#include "AsceMass/Fragments/AsceMovementFragments.h"
#include "AsceMass/SpatialGrid/AsceSpatialGridSubsystem.h"
#include "AsceMass/FlowField/AscePathFindingSubsystem.h"
#include "AsceMass/FlowField/AscePathFinder.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceMassCollisionSolverProcessor)

UAsceMassCollisionSolverProcessor::UAsceMassCollisionSolverProcessor()
	: EntityQuery(*this)
{
	// 动态处理器：由 UAsceMassMovementSimulationSubsystem 手动按序跑（调度器是唯一时钟）。
	// 默认 true 会被 MassSimulation 自动链再跑一份，且在 world init/loading 门控之外
	// （开局墙簇表未就绪时的断言就是这个自动份造成的）。
	bAutoRegisterWithProcessingPhases = false;
	// 只在权威端跑：碰撞解算直接写位置，客户端再算一份必然会和服务器结果分叉（谁推谁、谁又推回来）。
	// 客户端不模拟，位置完全由服务器同步驱动。
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Server);
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Movement);
}

void UAsceMassCollisionSolverProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.RegisterWithProcessor(*this);
	EntityQuery.AddRequirement<FAsceMovementSimStateFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadWrite); // 墙接触时要做速度法向投影
	EntityQuery.AddSubsystemRequirement<UAsceSpatialGridSubsystem>(EMassFragmentAccess::ReadWrite);
	// 注意：UAscePathFindingSubsystem不在这里声明（它的trait是GameThreadOnly；
	// 		  我们只读其PathFinder的WorldMapData->WallClusters，不碰scratch，
	// 		  用Execute里手动缓存const FWorldMapData*的方式绕开query限制）
}

void UAsceMassCollisionSolverProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// B方案：GameThread上取一次墙簇数据源（并行lambda里只读，不会写PathFinder的scratch）
	UWorld* World = Context.GetWorld();
	UAscePathFindingSubsystem* PathFindingSubsystem = World ? UAscePathFindingSubsystem::Get(World) : nullptr;
	UPathFinder* PathFinder = PathFindingSubsystem ? PathFindingSubsystem->GetPathFinder() : nullptr;
	const FWorldMapData* WorldMapData = PathFinder ? PathFinder->GetWorldMapData() : nullptr;

	const int32 Iterations = FMath::Max(1, SolverIterations);

	// 每轮解算（Jacobi并行：所有chunk同时用当前位置算重叠并写回；下一轮用写回后位置再测）
	// 把"解一轮"做成lambda，跑Iterations次——每轮之间位置已更新，重叠量递减。
	for (int32 Iter = 0; Iter < Iterations; ++Iter)
	{
		EntityQuery.ParallelForEachEntityChunk(Context, [this, &EntityManager, WorldMapData](FMassExecutionContext& Ctx)
		{
			UAsceSpatialGridSubsystem& SpatialGrid = Ctx.GetMutableSubsystemChecked<UAsceSpatialGridSubsystem>();
			const TArrayView<FTransformFragment> Transforms = Ctx.GetMutableFragmentView<FTransformFragment>();
			const TConstArrayView<FAgentRadiusFragment> Radii = Ctx.GetFragmentView<FAgentRadiusFragment>();
			const TArrayView<FMassVelocityFragment> Velocities = Ctx.GetMutableFragmentView<FMassVelocityFragment>();
			const TConstArrayView<FMassEntityHandle> Handles = Ctx.GetEntities();

			const FMassEntityManager& ConstEntityManager = EntityManager;

			for (int32 i = 0; i < Ctx.GetNumEntities(); ++i)
			{
				const float SelfRadius = Radii[i].Radius;
				const FVector& CurrentLocation = Transforms[i].GetTransform().GetLocation();
				const FVector2D CurrentLocation2D(CurrentLocation.X, CurrentLocation.Y);

				// ── A. 圆-圆：邻居互推（半量，对称收敛） ──
				FVector2D EntityPush2D = FVector2D::ZeroVector;
				TArray<FMassEntityHandle> Neighbors;
				SpatialGrid.FindEntities(CurrentLocation2D, QueryRadius, Neighbors);
				for (const FMassEntityHandle& NeighborHandle : Neighbors)
				{
					if (NeighborHandle == Handles[i])
					{
						continue;
					}

					const FTransformFragment* NeighborTransform = ConstEntityManager.GetFragmentDataPtr<FTransformFragment>(NeighborHandle);
					if (!NeighborTransform)
					{
						continue;
					}
					const FAgentRadiusFragment* NeighborRadius = ConstEntityManager.GetFragmentDataPtr<FAgentRadiusFragment>(NeighborHandle);
					const float CombinedRadius = SelfRadius + (NeighborRadius ? NeighborRadius->Radius : SelfRadius);

					const FVector NeighborLocation = NeighborTransform->GetTransform().GetLocation();
					const FVector2D ToSelf2D = CurrentLocation2D - FVector2D(NeighborLocation.X, NeighborLocation.Y);
					const float Distance = ToSelf2D.Size();
					if (Distance <= KINDA_SMALL_NUMBER || Distance >= CombinedRadius)
					{
						continue;
					}

					// 穿透深度（CombinedRadius - Distance），沿"远离邻居"方向
					const float PenetrationDepth = CombinedRadius - Distance;
					const FVector2D Direction2D = ToSelf2D / Distance;
					EntityPush2D += Direction2D * PenetrationDepth;
				}

				// ── B. 圆-墙格簇：AABB接触（墙不可撼动，全量推开 + 速度投影） ──
				FVector2D WallPush2D = FVector2D::ZeroVector;
				if (WorldMapData && SelfRadius > 0.f)
				{
					// 查表O(1)：先定位圆心所在格，再遍历"单位圆覆盖的格子范围"，
					// 每个格子经 GridIndexToCluster 找它所属的墙簇，对命中的簇做圆-AABB解算。
					// （圆心旁边的非墙格也可能贴墙——覆盖范围内所有格的查表不会漏贴墙）
					const FIntVector2 CenterGrid = FWorldMapData::WorldLocationToGridLocation(CurrentLocation2D);
					const int32 RadiusInCells = FMath::CeilToInt(SelfRadius / GRID_SIZE) + 1;
					const int32 GridCountXInt = WorldMapData->GetGridCountX();
					const int32 GridCountYInt = WorldMapData->GetGridCountY();

					// 小列表去重（不同格可能指同一簇）：栈上8个，多数情况够；极端（贴多个簇）走堆分配
					TArray<int32, TInlineAllocator<8>> VisitedClusters;
					for (int32 DY = -RadiusInCells; DY <= RadiusInCells; ++DY)
					{
						for (int32 DX = -RadiusInCells; DX <= RadiusInCells; ++DX)
						{
							const int32 GridX = CenterGrid.X + DX;
							const int32 GridY = CenterGrid.Y + DY;
							if (GridX < 0 || GridX >= GridCountXInt || GridY < 0 || GridY >= GridCountYInt)
							{
								continue;
							}
							const int32 CellIndex = GridY * GridCountXInt + GridX;
							// 防御：表未就绪（开局墙未变化前 RebuildWallClusters 没跑，GridIndexToCluster 还空），
							// 本格跳过墙解算，下一帧表就绪后自然正常（Rebuild不改变表存在性后的行为）。
							if (WorldMapData->GridIndexToCluster.Num() <= CellIndex)
							{
								continue;
							}
							const int32 ClusterIndex = WorldMapData->GridIndexToCluster[CellIndex];
							if (ClusterIndex < 0)
							{
								continue;
							}
							if (VisitedClusters.Contains(ClusterIndex))
							{
								continue;
							}
							VisitedClusters.Add(ClusterIndex);
							
							if (WorldMapData->WallClusters.Num() <= ClusterIndex)
							{
								continue;
							}
							
							const FWallCluster& Cluster = WorldMapData->WallClusters[ClusterIndex];

							// 圆-AABB最近点：clamp圆心到簇范围
							const FVector2D ClosestOnCluster(
								FMath::Clamp(CurrentLocation2D.X, Cluster.Min.X, Cluster.Max.X),
								FMath::Clamp(CurrentLocation2D.Y, Cluster.Min.Y, Cluster.Max.Y));
							const FVector2D ToClosest = CurrentLocation2D - ClosestOnCluster;
							const float DistToCluster = ToClosest.Size();
							const float PenetrationDepth = SelfRadius - DistToCluster;
							if (PenetrationDepth <= 0.f)
							{
								continue; // 没接触
							}

							// 法线：圆心 → 最近点方向（DistToCluster=0 时圆心在簇内部，需退化为按分量的最浅外扩）
							FVector2D Normal;
							if (DistToCluster > KINDA_SMALL_NUMBER)
							{
								Normal = ToClosest / DistToCluster;
							}
							else
							{
								// 圆心落在簇内部：取"最浅逃出方向"=沿最靠近的边向外。
								// 计算到四条边的距离，取最短那条边的外向法线。
								const float DistLeft  = CurrentLocation2D.X - Cluster.Min.X;
								const float DistRight = Cluster.Max.X - CurrentLocation2D.X;
								const float DistDown  = CurrentLocation2D.Y - Cluster.Min.Y;
								const float DistUp    = Cluster.Max.Y - CurrentLocation2D.Y;
								const float MinDist = FMath::Min3(FMath::Min(DistLeft, DistRight), DistDown, DistUp);
								if (MinDist == DistLeft)
								{
									Normal = FVector2D(-1.f, 0.f);
								}
								else if (MinDist == DistRight)
								{
									Normal = FVector2D(1.f, 0.f);
								}
								else if (MinDist == DistDown)
								{
									Normal = FVector2D(0.f, -1.f);
								}
								else
								{
									Normal = FVector2D(0.f, 1.f);
								}
							}

							// 全量推开（墙不动、只推单位）
							WallPush2D += Normal * PenetrationDepth;

							// 速度投影：去掉指向墙内的法向速度分量（只留切向）→ 沿墙滑动
							// 法向分量 = V · (-Normal) （正值=向墙里走），V_corrected = V - (-Normal)*分量 = V + Normal*分量
							const FVector2D Velocity2D(Velocities[i].Value.X, Velocities[i].Value.Y);
							const float ComponentIntoWall = Velocity2D.Dot(-Normal);
							if (ComponentIntoWall > 0.f)
							{
								FVector2D CorrectedVelocity = Velocity2D + Normal * ComponentIntoWall;
								Velocities[i].Value = FVector(CorrectedVelocity.X, CorrectedVelocity.Y, Velocities[i].Value.Z);
							}
						}
					}
				}

				// 合并：实体互推半量 + 墙推全量
				const FVector2D FinalPush2D = EntityPush2D * EntitySeparatonRatio + WallPush2D;
				if (!FinalPush2D.IsNearlyZero())
				{
					FTransform& Transform = Transforms[i].GetMutableTransform();
					FVector NewLocation = Transform.GetLocation();
					NewLocation.X += FinalPush2D.X;
					NewLocation.Y += FinalPush2D.Y;
					Transform.SetTranslation(NewLocation);
				}
			}
		});
	}
}
