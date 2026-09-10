// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceUnitReplicator.h"
#include "Engine/World.h"
#include "MassCommonFragments.h"
#include "MassReplicationProcessor.h" // EMassLOD / FMassReplicationSharedFragment（经 AsceUnitReplicator.h 已有，这里显式确保）

#include "AsceMass/Fragments/AsceCoreFragments.h"
#include "AsceMass/Fragments/AsceFootprintFragment.h"
#include "AsceMass/Vision/AsceVisionDefines.h"
#include "AsceMass/Vision/AsceFogSubsystem.h"

namespace
{
	/** client(Index) → TeamKey 注册表。Server 主线程访问；世界切换/重开时 ClearAllClientTeams。 */
	TMap<int32, int32>& GetClientTeamRegistry()
	{
		static TMap<int32, int32> Registry;
		return Registry;
	}

	/**
	 * 视野复制过滤：在本 chunk 的数据能力转调 CalculateClientReplication
	 * 之前，把候选 entity 相对"当前 client 队伍"不可见的 LOD 覆写为 EMassLOD::Off；
	 * 可见的保底为非 Off（原距离 Off → 抬到最低非 Off 档 Low，否则保留原距离档）。
	 * 之后引擎模板按 LOD 分支决定 bubble 进出（LOD < Off → Add/Modify，LOD >= Off → Remove）。
	 *
	 * 规则：友方（同队，含建筑）恒可见；敌方/中立按覆盖格任一可见判定。
	 */
	void ApplyVisionFilterToChunk(FMassExecutionContext& Context, UAsceFogSubsystem& Fog, int32 ClientTeam)
	{
		// LOD 一定在查询里（基类 EntityQuery 要求 FMassReplicationLODFragment ReadWrite）
		if (!Context.DoesArchetypeHaveFragment<FMassReplicationLODFragment>())
		{
			return;
		}
		TArrayView<FMassReplicationLODFragment> LODList = Context.GetMutableFragmentView<FMassReplicationLODFragment>();

		// 其余 fragment 只对"命中自己能力的 entity"存在（Shared EntityQuery 用 Any 合并），逐项 Any 检查
		TConstArrayView<FAsceTeamFragment> Teams;
		TConstArrayView<FTransformFragment> Transforms;
		TConstArrayView<FAgentRadiusFragment> Radii;
		TConstArrayView<FAsceGridFootprintFragment> Footprints;
		if (Context.DoesArchetypeHaveFragment<FAsceTeamFragment>())
		{
			Teams = Context.GetFragmentView<FAsceTeamFragment>();
		}
		if (Context.DoesArchetypeHaveFragment<FTransformFragment>())
		{
			Transforms = Context.GetFragmentView<FTransformFragment>();
		}
		if (Context.DoesArchetypeHaveFragment<FAgentRadiusFragment>())
		{
			Radii = Context.GetFragmentView<FAgentRadiusFragment>();
		}
		if (Context.DoesArchetypeHaveFragment<FAsceGridFootprintFragment>())
		{
			Footprints = Context.GetFragmentView<FAsceGridFootprintFragment>();
		}

		// footprint（按流场格计）→ 视野格换算比例（与 AsceFogProcessor 相同，架构文档 §2 整除约束）
		const int32 Scale = FMath::RoundToInt32(AsceVision::FlowFieldGridSizeCm / Fog.GetHeightMap().GetCellSizeCm());

		const int32 Num = Context.GetNumEntities();
		for (int32 i = 0; i < Num; ++i)
		{
			// 没有队伍/位置的 entity 不在我们处理范围：不动它的 LOD
			if (Teams.Num() != Num || Transforms.Num() != Num)
			{
				continue;
			}

			const int32 EntityTeam = Teams[i].TeamKey;

			// 友方（含建筑）恒可见（§6.1.4）
			if (EntityTeam == ClientTeam)
			{
				if (LODList[i].LOD == EMassLOD::Off)
				{
					LODList[i].LOD = EMassLOD::Low; // 保底进 bubble 的最低档
				}
				continue;
			}

			// 敌方/中立：按覆盖格任一可见判定（§4.1）
			FAsceVisionEntityGeometry Geometry;
			if (Footprints.Num() == Num)
			{
				// 建筑：footprint 换算成视野格矩形
				Geometry.bUseFootprint = true;
				Geometry.FootprintX0 = Footprints[i].OriginGridLocation.X * Scale;
				Geometry.FootprintY0 = Footprints[i].OriginGridLocation.Y * Scale;
				Geometry.FootprintX1 = Geometry.FootprintX0 + Footprints[i].SizeX * Scale - 1;
				Geometry.FootprintY1 = Geometry.FootprintY0 + Footprints[i].SizeY * Scale - 1;
			}
			else
			{
				// 单位：位置 + 物理覆盖半径（无 AgentRadius 时退化为 0 → 只看中心所在格）
				const FVector Loc = Transforms[i].GetTransform().GetLocation();
				Geometry.CenterWorld = FVector2D(Loc.X, Loc.Y);
				Geometry.CoverageRadiusCm = (Radii.Num() == Num) ? Radii[i].Radius : 0.f;
			}

			const bool bVisible = Fog.IsEntityVisible(ClientTeam, Geometry);
			if (bVisible)
			{
				if (LODList[i].LOD == EMassLOD::Off)
				{
					LODList[i].LOD = EMassLOD::Low; // 从距离 Off 抬回可见（模板会 AddEntity）
				}
				// 已非 Off：保留原距离档，作为可见范围内的更新频率/精度分档
			}
			else
			{
				LODList[i].LOD = EMassLOD::Off; // 模板会 RemoveEntity / 不入 bubble
			}
		}
	}
}

TArray<TUniquePtr<FAsceReplicationCapabilityBase>>& UAsceUnitReplicator::GetCapabilities()
{
	static TArray<TUniquePtr<FAsceReplicationCapabilityBase>> Capabilities;
	return Capabilities;
}

void UAsceUnitReplicator::RegisterCapability(TUniquePtr<FAsceReplicationCapabilityBase> Capability)
{
	GetCapabilities().Add(MoveTemp(Capability));
}

void UAsceUnitReplicator::SetClientTeam(int32 ClientIndex, int32 TeamKey)
{
	GetClientTeamRegistry().Add(ClientIndex, TeamKey);
}

void UAsceUnitReplicator::ClearAllClientTeams()
{
	GetClientTeamRegistry().Reset();
}

void UAsceUnitReplicator::AddRequirements(FMassEntityQuery& EntityQuery)
{
	for (const TUniquePtr<FAsceReplicationCapabilityBase>& Capability : GetCapabilities())
	{
		Capability->AddRequirements(EntityQuery);
	}
}

void UAsceUnitReplicator::ProcessClientReplication(FMassExecutionContext& Context, FMassReplicationContext& ReplicationContext)
{
#if UE_REPLICATION_COMPILE_SERVER_CODE

	// ── 视野复制过滤────────────────────────────
	// 决策者入口：在所有数据能力（转调 CalculateClientReplication）之前，按当前 client 队伍
	// 的可见集覆写 LOD（不可见 → Off）。之后各能力里引擎模板会按 LOD 自动 Add/Modify/Remove。
	// 基础设施未就绪（高度场未初始化 / client 队伍未注册）→ 跳过，保持引擎距离 LOD（安全默认）。
	UAsceFogSubsystem* Fog = UWorld::GetSubsystem<UAsceFogSubsystem>(&ReplicationContext.World);
	if (Fog && Fog->IsReady())
	{
		const FMassReplicationSharedFragment& RepShared = Context.GetSharedFragment<FMassReplicationSharedFragment>();
		if (const int32* ClientTeamPtr = GetClientTeamRegistry().Find(RepShared.CurrentClientHandle.GetIndex()))
		{
			ApplyVisionFilterToChunk(Context, *Fog, *ClientTeamPtr);
		}
		else
		{
			UE_LOG(LogVision, VeryVerbose, TEXT("UnitReplicator：client(%d) 队伍未注册，跳过视野过滤"),
				RepShared.CurrentClientHandle.GetIndex());
		}
	}
	// ────────────────────────────────────────────────────────────

#endif // UE_REPLICATION_COMPILE_SERVER_CODE
	for (const TUniquePtr<FAsceReplicationCapabilityBase>& Capability : GetCapabilities())
	{
		Capability->ProcessClientReplication(Context, ReplicationContext);
	}
}
