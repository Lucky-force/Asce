// AsceBuildingSubsystem.cpp
#include "AsceBuildingSubsystem.h"

#include "AsceBuildingPlacementActor.h"
#include "AsceMass/Ether/AsceEtherBuildingConfig.h"
#include "AsceMass/Ether/AsceEtherFragments.h"
#include "AsceMass/Ether/AsceEtherNetworkSubsystem.h"
#include "AsceMass/FlowField/AsceFlowFieldCores.h"
#include "AsceMass/Fragments/AsceCoreFragments.h"
#include "AsceMass/Fragments/AsceFootprintFragment.h"
#include "AsceMass/SpatialGrid/AsceSpatialGridSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "MassCommonFragments.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "StructUtils/InstancedStruct.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceBuildingSubsystem)

namespace
{
	FORCEINLINE void SetError(FString* OutError, const FString& Message)
	{
		if (OutError)
		{
			*OutError = Message;
		}
	}
}

UAsceBuildingSubsystem::UAsceBuildingSubsystem() = default;

UAsceBuildingSubsystem* UAsceBuildingSubsystem::Get(const UObject* WorldContextObject)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	return World ? World->GetSubsystem<UAsceBuildingSubsystem>() : nullptr;
}

bool UAsceBuildingSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	UWorld* World = Cast<UWorld>(Outer);
	if (!World)
	{
		return false;
	}
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	// 两端都建：客户端也要销毁摆放 Actor（表现来自实体复制）
	return World->IsGameWorld() || World->IsPlayInEditor();
}

void UAsceBuildingSubsystem::Deinitialize()
{
	Buildings.Reset();
	PendingPlacements.Reset();
	bBootstrapped = false;
	bWarnedNotReady = false;
	Super::Deinitialize();
}

// ── 报到 / 引导 ──

void UAsceBuildingSubsystem::RegisterPlacement(AAsceBuildingPlacementActor* PlacementActor)
{
	if (!PlacementActor)
	{
		return;
	}

	FAscePendingPlacement Pending;
	Pending.Actor = PlacementActor;
	Pending.Placement = PlacementActor->BuildPlacement();

	// 引导已过（例如流式加载后动态出现的摆放 Actor）：权威端立刻落地，然后两端都销毁它
	if (bBootstrapped)
	{
		UWorld* World = GetWorld();
		if (World && World->GetNetMode() != NM_Client)
		{
			FString Error;
			if (!ValidatePlacement(Pending.Placement, &Error))
			{
				UE_LOG(LogTemp, Warning, TEXT("[Building] 动态摆放 Actor %s 落地失败：%s"), *PlacementActor->GetName(), *Error);
			}
			else
			{
				CreateBuildingEntity(Pending.Placement);
			}
		}
		PlacementActor->Destroy();
		return;
	}

	PendingPlacements.Add(MoveTemp(Pending));
}

void UAsceBuildingSubsystem::RunBootstrapStage(const FAsceTickContext& Ctx)
{
	if (bBootstrapped)
	{
		return;
	}
	if (PendingPlacements.Num() == 0)
	{
		bBootstrapped = true;
		return;
	}

	UWorld* World = Ctx.World ? Ctx.World : GetWorld();
	if (!World)
	{
		return;
	}

	// 就绪条件：建筑层已构建（`AAsceWorldMapReplicatorActor` 里 ConstructBuildingsGrid 之后）
	UAsceSpatialGridSubsystem* Grid = Ctx.Grid ? Ctx.Grid : UAsceSpatialGridSubsystem::Get(this);
	if (!Grid || !Grid->IsBuildingsGridReady())
	{
		if (!bWarnedNotReady && World->GetTimeSeconds() > 5.0)
		{
			bWarnedNotReady = true;
			UE_LOG(LogTemp, Warning, TEXT("[Building] 建筑网格未就绪（ConstructBuildingsGrid 没跑？）：%d 个预摆建筑仍在等待"),
				PendingPlacements.Num());
		}
		return;
	}

	const bool bAuthority = Ctx.World ? Ctx.bAuthority : (World->GetNetMode() != NM_Client);
	int32 Created = 0;
	int32 Failed = 0;

	if (bAuthority)
	{
		for (const FAscePendingPlacement& Pending : PendingPlacements)
		{
			FString Error;
			if (!ValidatePlacement(Pending.Placement, &Error, &Ctx))
			{
				++Failed;
				UE_LOG(LogTemp, Warning, TEXT("[Building] 预摆落地失败（%s）：origin=(%d,%d) size=(%d,%d) rot=%d"),
					*Error,
					Pending.Placement.OriginGridLocation.X, Pending.Placement.OriginGridLocation.Y,
					Pending.Placement.Size.X, Pending.Placement.Size.Y,
					Pending.Placement.Rotation);
				continue;
			}
			if (CreateBuildingEntity(Pending.Placement, &Ctx).IsValid())
			{
				++Created;
			}
			else
			{
				++Failed;
			}
		}
	}

	// 两端都销毁摆放 Actor（建筑表现后续由实体复制负责）
	int32 Destroyed = 0;
	for (const FAscePendingPlacement& Pending : PendingPlacements)
	{
		if (AAsceBuildingPlacementActor* Actor = Pending.Actor.Get())
		{
			Actor->Destroy();
			++Destroyed;
		}
	}
	PendingPlacements.Reset();
	bBootstrapped = true;

	UE_LOG(LogTemp, Log, TEXT("[Building] 引导完成：建成 %d，失败 %d，销毁摆放 Actor %d（权威端=%s，已登记建筑总数 %d）"),
		Created, Failed, Destroyed, bAuthority ? TEXT("yes") : TEXT("no"), Buildings.Num());
}

// ── 运行期增删 ──

bool UAsceBuildingSubsystem::PlaceBuilding(const FAsceBuildingPlacement& Placement, FString* OutError)
{
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		SetError(OutError, TEXT("只有权威端能创建建筑"));
		return false;
	}

	FString Error;
	if (!ValidatePlacement(Placement, &Error))
	{
		SetError(OutError, Error);
		return false;
	}

	if (!CreateBuildingEntity(Placement).IsValid())
	{
		SetError(OutError, TEXT("创建建筑实体失败"));
		return false;
	}
	return true;
}

bool UAsceBuildingSubsystem::DemolishBuilding(FMassEntityHandle Building, FString* OutError)
{
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		SetError(OutError, TEXT("只有权威端能拆除建筑"));
		return false;
	}

	const int32 Index = Buildings.IndexOfByPredicate(
		[Building](const FAsceBuiltBuilding& Record) { return Record.Entity == Building; });
	if (Index == INDEX_NONE)
	{
		SetError(OutError, TEXT("该实体不是本系统登记的建筑"));
		return false;
	}

	UMassEntitySubsystem* MassSub = World->GetSubsystem<UMassEntitySubsystem>();
	if (!MassSub)
	{
		SetError(OutError, TEXT("MassEntitySubsystem 不可用"));
		return false;
	}
	MassSub->GetMutableEntityManager().DestroyEntity(Building);
	Buildings.RemoveAt(Index);
	return true;
}

// ── 校验 / 创建 ──

bool UAsceBuildingSubsystem::ValidatePlacement(const FAsceBuildingPlacement& Placement, FString* OutError, const FAsceTickContext* Ctx) const
{
	if (!Placement.BuildingConfig)
	{
		SetError(OutError, TEXT("未指定 BuildingConfig"));
		return false;
	}
	const UAsceEtherBuildingConfig* Cfg = Placement.BuildingConfig.GetDefaultObject();
	if (!Cfg)
	{
		SetError(OutError, TEXT("BuildingConfig 的 CDO 取不到"));
		return false;
	}

	const UAsceSpatialGridSubsystem* Grid = (Ctx && Ctx->Grid) ? Ctx->Grid : UAsceSpatialGridSubsystem::Get(this);
	if (!Grid || !Grid->IsBuildingsGridReady())
	{
		SetError(OutError, TEXT("建筑网格未就绪（ConstructBuildingsGrid 没跑？）"));
		return false;
	}

	FIntPoint Min, Max;
	Placement.GetOccupiedBounds(Min, Max);
	const FIntPoint Dim = Grid->GetBuildingsLogicalDim();
	if (Min.X < 0 || Min.Y < 0 || Max.X >= Dim.X || Max.Y >= Dim.Y)
	{
		SetError(OutError, FString::Printf(TEXT("越界：占用格 (%d,%d)-(%d,%d) 超出建筑网格 %dx%d"),
			Min.X, Min.Y, Max.X, Max.Y, Dim.X, Dim.Y));
		return false;
	}

	TArray<FMassEntityHandle> Hits;
	Grid->FindBuildingsInLogicalRect(Min, Max, Hits);
	if (Hits.Num() > 0)
	{
		SetError(OutError, FString::Printf(TEXT("重叠：占用格 (%d,%d)-(%d,%d) 内已有 %d 个建筑"),
			Min.X, Min.Y, Max.X, Max.Y, Hits.Num()));
		return false;
	}
	return true;
}

FMassEntityHandle UAsceBuildingSubsystem::CreateBuildingEntity(const FAsceBuildingPlacement& Placement, const FAsceTickContext* Ctx)
{
	UWorld* World = GetWorld();
	UMassEntitySubsystem* MassSub = (Ctx && Ctx->Mass)
		? Ctx->Mass
		: (World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr);
	const UAsceEtherBuildingConfig* Cfg = Placement.BuildingConfig ? Placement.BuildingConfig.GetDefaultObject() : nullptr;
	if (!MassSub || !Cfg)
	{
		return FMassEntityHandle();
	}

	// Ether 登记（Trait 路径由 Trait 自己登记；本路径是"实例表创建"，必须手动登记）
	const FName DefId = Placement.BuildingDefId.IsNone()
		? Placement.BuildingConfig->GetFName()
		: Placement.BuildingDefId;
	if (UAsceEtherNetworkSubsystem* Net = UAsceEtherNetworkSubsystem::Get(this))
	{
		Net->RegisterBuildingConfig(DefId, Placement.BuildingConfig);
	}

	// 实体位置 = footprint 几何中心（Z 暂为 0；需要贴地时再接高度场）；朝向 = Rotation（供后续表现/Ether 定向用）
	const FVector2D CenterCell = Placement.GetCenterCell();
	const FVector CenterWorld((CenterCell.X + 0.5) * GRID_SIZE, (CenterCell.Y + 0.5) * GRID_SIZE, 0.f);

	// footprint 写**旋转后的最终 AABB**（与 costfield/视野/建筑层同一口径）
	FIntPoint FootMin, FootMax;
	Placement.GetOccupiedBounds(FootMin, FootMax);

	// 初值一次给全：Mass 的 Add 观察者在创建时立即执行，走裸 Archetype 会让观察者读到空值
	TArray<FInstancedStruct> Fragments;
	{
		FTransformFragment& Transform = Fragments.Add_GetRef(FInstancedStruct::Make<FTransformFragment>())
			.GetMutable<FTransformFragment>();
		Transform.GetMutableTransform().SetTranslation(CenterWorld);
		Transform.GetMutableTransform().SetRotation(FQuat(FRotator(0.f, Placement.Rotation * 90.f, 0.f)));
	}
	{
		FAsceGridFootprintFragment& Foot = Fragments.Add_GetRef(FInstancedStruct::Make<FAsceGridFootprintFragment>())
			.GetMutable<FAsceGridFootprintFragment>();
		Foot.OriginGridLocation = FootMin;
		Foot.SizeX = FootMax.X - FootMin.X + 1;
		Foot.SizeY = FootMax.Y - FootMin.Y + 1;
		Foot.OcclusionHeightCm = Cfg->OcclusionHeightCm;
	}
	{
		FEtherBuildingFragment& Ether = Fragments.Add_GetRef(FInstancedStruct::Make<FEtherBuildingFragment>())
			.GetMutable<FEtherBuildingFragment>();
		Ether.BuildingDefId = DefId;
	}
	{
		FAsceTeamFragment& Team = Fragments.Add_GetRef(FInstancedStruct::Make<FAsceTeamFragment>())
			.GetMutable<FAsceTeamFragment>();
		Team.TeamKey = Placement.TeamKey;
	}

	FMassEntityManager& EntityManager = MassSub->GetMutableEntityManager();
	const FMassEntityHandle Entity = EntityManager.CreateEntity(Fragments);
	if (!Entity.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Building] CreateEntity 失败（cfg=%s）"), *DefId.ToString());
		return Entity;
	}
	// tag 不能在"实例表创建"路径上一次带上（那段 API 只吃 fragment）
	EntityManager.AddTagToEntity(Entity, FAsceBuildingTag::StaticStruct());

	FAsceBuiltBuilding& Record = Buildings.AddDefaulted_GetRef();
	Record.Entity = Entity;
	Record.Placement = Placement;
	return Entity;
}
