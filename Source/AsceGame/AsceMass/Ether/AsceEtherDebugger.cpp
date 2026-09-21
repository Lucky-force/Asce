// AsceEtherDebugger.cpp
#include "AsceEtherDebugger.h"

#include "AsceMass/Data/AsceEntityDefLibrary.h"
#include "AsceMass/Ether/AsceEtherBuildingDef.h"
#include "AsceMass/Ether/AsceEtherFragments.h"
#include "AsceMass/Ether/AsceEtherNetworkSubsystem.h"
#include "AsceMass/Ether/AsceEtherProcessCore.h"
#include "AsceMass/Fragments/AsceCoreFragments.h"
#include "AsceMass/Fragments/AsceFootprintFragment.h"
#include "AsceMass/FlowField/AsceFlowFieldCores.h"   // GRID_SIZE
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "MassCommonFragments.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "StructUtils/InstancedStruct.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceEtherDebugger)

AAsceEtherDebugger::AAsceEtherDebugger()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AAsceEtherDebugger::BeginPlay()
{
	Super::BeginPlay();

	// 仅服务器（standalone / listen server）生成与注册；客户端不参与逻辑
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}
	SpawnAndRegister();
}

void AAsceEtherDebugger::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterAndDestroy();
	Super::EndPlay(EndPlayReason);
}

void AAsceEtherDebugger::SpawnAndRegister()
{
	UWorld* World = GetWorld();
	UMassEntitySubsystem* EntitySubsystem = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	if (!EntitySubsystem || Placements.Num() == 0)
	{
		return;
	}

	FMassEntityManager& EM = EntitySubsystem->GetMutableEntityManager();

	for (const FAsceEtherDebugPlacement& Placement : Placements)
	{
		if (Placement.BuildingDef.IsNone())
		{
			continue;
		}
		FAsceEtherBuildingDef Def;
		if (!UAsceEntityDefLibrary::GetEtherBuildingDefByName(Placement.BuildingDef.RowName, Def))
		{
			UE_LOG(LogTemp, Warning, TEXT("[EtherDebug] 建筑类型 %s 不在实体定义表（或不是 Ether 建筑）"), *Placement.BuildingDef.RowName.ToString());
			continue;
		}

		const FName DefId = Placement.BuildingDef.RowName;
		// 行名非法直接崩（调试器也要保证身份正确；见 `ParseEntityDefIdChecked`）
		const uint16 DefNumericId = UAsceEntityDefLibrary::ParseEntityDefIdChecked(DefId);

		// 用"带初始值的 CreateEntity"建实体：Mass 的 Add 观察者在创建时立刻执行，
		// 走裸 Archetype（默认构造 fragment）会让观察者读到空的类型 id / footprint；
		// 这里和 Trait/模板路径一样，创建时就把 fragment 值填好。
		const FVector2D Center(
			(static_cast<float>(Placement.Cell.X) + 0.5f) * GRID_SIZE,
			(static_cast<float>(Placement.Cell.Y) + 0.5f) * GRID_SIZE);

		TArray<FInstancedStruct> FragmentList;
		{
			FTransformFragment& Transform = FragmentList.Add_GetRef(FInstancedStruct::Make<FTransformFragment>())
				.GetMutable<FTransformFragment>();
			Transform.GetMutableTransform().SetTranslation(FVector(Center.X, Center.Y, 0.f));
		}
		{
			FAsceGridFootprintFragment& Foot = FragmentList.Add_GetRef(FInstancedStruct::Make<FAsceGridFootprintFragment>())
				.GetMutable<FAsceGridFootprintFragment>();
			Foot.OriginGridLocation = Placement.Cell;
			Foot.SizeX = FMath::Max(1, Def.SizeX);
			Foot.SizeY = FMath::Max(1, Def.SizeY);
			Foot.OcclusionHeightCm = 0;   // 调试建筑不参与视野遮挡
		}
		{
			// 标记：Ether 建筑（身份见 FAsceUnitDefFragment；Add 观察者会 push AddNode）
			FragmentList.Add(FInstancedStruct::Make<FEtherBuildingFragment>());
		}
		{
			FAsceUnitDefFragment& UnitDef = FragmentList.Add_GetRef(FInstancedStruct::Make<FAsceUnitDefFragment>())
				.GetMutable<FAsceUnitDefFragment>();
			UnitDef.UniqueTypeId = DefNumericId;
		}

		const FMassEntityHandle Entity = EM.CreateEntity(FragmentList);
		if (!Entity.IsValid())
		{
			continue;
		}
		// 调试建筑也进 SpatialGrid "建筑层"：`CreateEntity(实例表)` 只能带 fragment ⇒ tag 创建后补
		EM.AddTagToEntity(Entity, FAsceBuildingTag::StaticStruct());

		SpawnedEntities.Add(Entity);
	}
}

void AAsceEtherDebugger::UnregisterAndDestroy()
{
	UWorld* World = GetWorld();
	UMassEntitySubsystem* EntitySubsystem = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	if (!EntitySubsystem)
	{
		SpawnedEntities.Reset();
		return;
	}

	// 销毁实体 → Remove 观察者会把建筑从 ether 网络注销
	FMassEntityManager& EM = EntitySubsystem->GetMutableEntityManager();
	for (const FMassEntityHandle& Entity : SpawnedEntities)
	{
		if (EM.IsEntityValid(Entity))
		{
			EM.DestroyEntity(Entity);
		}
	}
	SpawnedEntities.Reset();
}

void AAsceEtherDebugger::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

#if ENABLE_DRAW_DEBUG
	if (bDrawDebug)
	{
		DrawDebugState();
	}
#endif
}

#if ENABLE_DRAW_DEBUG
namespace
{
	/** 颜色：空=灰；能量=白；普通族按族哈希取色；互补态反色 */
	FColor EtherTypeColor(int16 SignalType, bool bHasEther)
	{
		if (!bHasEther)
		{
			return FColor(80, 80, 80);
		}
		const uint16 FamilyId = AsceEther::Family(SignalType);
		if (FamilyId == 0)
		{
			return FColor(255, 255, 255);
		}
		FColor C = FLinearColor::MakeFromHSV8(static_cast<uint8>((FamilyId * 47) % 255), 200, 255).ToFColor(true);
		if (AsceEther::IsComplement(SignalType))
		{
			C = FColor(255 - C.R, 255 - C.G, 255 - C.B);
		}
		return C;
	}

	FIntVector2 DirVec(uint8 Dir)
	{
		switch (static_cast<AsceEther::EDir>(Dir))
		{
			case AsceEther::EDir::Left:  return FIntVector2(-1, 0);
			case AsceEther::EDir::Up:    return FIntVector2(0, 1);
			case AsceEther::EDir::Right: return FIntVector2(1, 0);
			case AsceEther::EDir::Down:  return FIntVector2(0, -1);
			default:                     return FIntVector2::ZeroValue;
		}
	}
}
#endif

void AAsceEtherDebugger::DrawDebugState()
{
#if ENABLE_DRAW_DEBUG
	UWorld* World = GetWorld();
	UAsceEtherNetworkSubsystem* Net = UAsceEtherNetworkSubsystem::Get(this);
	if (!World || !Net)
	{
		return;
	}

	const int32 NodeNum = Net->GetNodeNum();
	int32 UsedNodes = 0;
	for (int32 NodeId = 0; NodeId < NodeNum; ++NodeId)
	{
		const int32 Count = Net->GetNodePortCount(NodeId);
		if (Count <= 0)
		{
			continue;
		}
		++UsedNodes;
		int32 FirstPortId = INDEX_NONE; // 器件状态显示用

		for (int32 i = 0; i < Count; ++i)
		{
			const int32 PortId = Net->GetNodePortId(NodeId, i); // 端口持久化：按 slot 取 id（不连续）
			UAsceEtherNetworkSubsystem::FPortView V;
			if (!Net->GetPortView(PortId, V))
			{
				continue;
			}
			if (FirstPortId == INDEX_NONE)
			{
				FirstPortId = PortId;
			}

			const FVector2D P(
				(static_cast<float>(V.CellX) + 0.5f) * GRID_SIZE,
				(static_cast<float>(V.CellY) + 0.5f) * GRID_SIZE);
			const FColor Color = EtherTypeColor(V.SignalType, V.Ether > 0);
			const FIntVector2 DV = DirVec(V.Dir);

			// 占格 + 端口朝向箭头
			DrawDebugBox(World, FVector(P.X, P.Y, 5.f),
				FVector(GRID_SIZE * 0.45f, GRID_SIZE * 0.45f, 2.f), FQuat::Identity, Color, false, -1.f, 0, 1.f);
			DrawDebugDirectionalArrow(World,
				FVector(P.X, P.Y, 30.f), FVector(P.X + DV.X * 60.f, P.Y + DV.Y * 60.f, 30.f),
				20.f, Color, false, -1.f, 0, 2.f);

			// 维度 / 符号 / 数量 / 吞吐
			DrawDebugString(World, FVector(P.X, P.Y, 110.f),
				FString::Printf(TEXT("d%d %s%d %d/%d"), V.Dim,
					V.SignalType < 0 ? TEXT("-") : TEXT("+"),
					FMath::Abs(static_cast<int32>(V.SignalType)),
					V.Ether, V.Throughput),
				nullptr, Color, 0.f, true);

			// 内部布线（同建筑内）
			if (const FAsceEtherPortIdList* Targets = Net->GetPortLinkTargets(PortId))
			{
				for (const int32 ToId : *Targets)
				{
					UAsceEtherNetworkSubsystem::FPortView TV;
					if (!Net->GetPortView(ToId, TV))
					{
						continue;
					}
					const FVector2D T(
						(static_cast<float>(TV.CellX) + 0.5f) * GRID_SIZE,
						(static_cast<float>(TV.CellY) + 0.5f) * GRID_SIZE);
					DrawDebugLine(World, FVector(P.X, P.Y, 15.f), FVector(T.X, T.Y, 15.f),
						FColor(150, 150, 150), false, -1.f, 0, 1.f);
				}
			}

			// 外部连接
			if (V.OutTargetPort != INDEX_NONE)
			{
				UAsceEtherNetworkSubsystem::FPortView TV;
				if (Net->GetPortView(V.OutTargetPort, TV))
				{
					const FVector2D T(
						(static_cast<float>(TV.CellX) + 0.5f) * GRID_SIZE,
						(static_cast<float>(TV.CellY) + 0.5f) * GRID_SIZE);
					DrawDebugLine(World, FVector(P.X, P.Y, 25.f), FVector(T.X, T.Y, 25.f),
						Color, false, -1.f, 0, 2.f);
				}
			}
		}

		// 器件内部状态（由 core 自己描述；无 core 就不画）
		if (const UAsceEtherProcessCore* Core = Net->GetProcessCore(NodeId))
		{
			const FString State = Core->GetDebugString();
			UAsceEtherNetworkSubsystem::FPortView V0;
			if (!State.IsEmpty() && Net->GetPortView(FirstPortId, V0))
			{
				const FVector2D P(
					(static_cast<float>(V0.CellX) + 0.5f) * GRID_SIZE,
					(static_cast<float>(V0.CellY) + 0.5f) * GRID_SIZE);
				DrawDebugString(World, FVector(P.X, P.Y, 160.f), State, nullptr, FColor::Yellow, 0.f, true);
			}
		}
	}

	DrawDebugString(World, GetActorLocation() + FVector(0.f, 0.f, 200.f),
		FString::Printf(TEXT("[Ether] 节点 %d，端口 %d"), UsedNodes, Net->GetPortNum()),
		nullptr, UsedNodes > 0 ? FColor::Green : FColor::Red, 0.f, true);
#endif
}
