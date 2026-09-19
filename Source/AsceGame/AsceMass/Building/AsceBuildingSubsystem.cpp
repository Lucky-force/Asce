// AsceBuildingSubsystem.cpp
#include "AsceBuildingSubsystem.h"

#include "AsceBuildingPlacementActor.h"
#include "AsceMass/Building/AsceBuildingDef.h"
#include "AsceMass/Data/AsceEntityDefLibrary.h"
#include "AsceMass/Ether/AsceEtherBuildingDef.h"
#include "AsceMass/Ether/AsceEtherFragments.h"
#include "AsceMass/Ether/AsceEtherNetworkSubsystem.h"
#include "AsceMass/FlowField/AsceFlowFieldCores.h"
#include "AsceMass/Fragments/AsceCoreFragments.h"
#include "AsceMass/Fragments/AsceFootprintFragment.h"
#include "AsceMass/SpatialGrid/AsceSpatialGridSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "MassCommonFragments.h"
#include "MassEntityConfigAsset.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassEntityTemplate.h"
#include "MassEntityView.h"
#include "MassSpawnerSubsystem.h"
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
	for (FAsceBuiltBuilding& Record : Buildings)
	{
		if (Record.Core)
		{
			Record.Core->Deinitialize();
			Record.Core = nullptr;
		}
	}
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
	if (Buildings[Index].Core)
	{
		Buildings[Index].Core->Deinitialize();
		Buildings[Index].Core = nullptr;
	}
	Buildings.RemoveAt(Index);
	return true;
}

// ── 建筑行为 core ──

UAsceBuildingCore* UAsceBuildingSubsystem::GetBuildingCore(FMassEntityHandle Building) const
{
	const int32 Index = Buildings.IndexOfByPredicate(
		[Building](const FAsceBuiltBuilding& Record) { return Record.Entity == Building; });
	return (Index != INDEX_NONE) ? Buildings[Index].Core : nullptr;
}

void UAsceBuildingSubsystem::CollectBuildingActions(FMassEntityHandle Building, TArray<FAsceBuildingAction>& OutActions) const
{
	if (const UAsceBuildingCore* Core = GetBuildingCore(Building))
	{
		Core->CollectActions(OutActions);
	}
}

bool UAsceBuildingSubsystem::ExecuteBuildingAction(FMassEntityHandle Building, FName ActionId, FString* OutError)
{
	UAsceBuildingCore* Core = GetBuildingCore(Building);
	if (!Core)
	{
		SetError(OutError, TEXT("该建筑没有行为 core"));
		return false;
	}
	Core->ExecuteAction(ActionId);
	return true;
}

void UAsceBuildingSubsystem::SetBuildingSelected(FMassEntityHandle Building, bool bSelected)
{
	if (UAsceBuildingCore* Core = GetBuildingCore(Building))
	{
		Core->SetSelected(bSelected);
	}
}

// ── 校验 / 创建 ──

bool UAsceBuildingSubsystem::ValidatePlacement(const FAsceBuildingPlacement& Placement, FString* OutError, const FAsceTickContext* Ctx) const
{
	if (Placement.BuildingDefId.IsNone())
	{
		SetError(OutError, TEXT("未指定建筑类型 id（BuildingDefId）"));
		return false;
	}
	FAsceBuildingDefBase Def;
	if (!UAsceEntityDefLibrary::GetBuildingDefByName(Placement.BuildingDefId, Def))
	{
		SetError(OutError, FString::Printf(TEXT("建筑类型 id '%s' 无效（不在实体定义表 / 不是建筑类型）"),
			*Placement.BuildingDefId.ToString()));
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
	if (!MassSub || !World)
	{
		return FMassEntityHandle();
	}

	// 定义行 = 建筑类型数据（建筑通用 + 可能的种类专属）+ 生成模板（EntityTemplate）
	FAsceEntityDefRow DefRow;
	if (!UAsceEntityDefLibrary::GetEntityDefRowByName(Placement.BuildingDefId, DefRow))
	{
		return FMassEntityHandle();
	}
	const FAsceBuildingDefBase* Def = DefRow.Def.GetPtr<FAsceBuildingDefBase>();
	if (!Def)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Building] 行 %s 的 Def 不是建筑类型（FAsceBuildingDefBase 子类）"), *Placement.BuildingDefId.ToString());
		return FMassEntityHandle();
	}
	// 只有 Ether 建筑才挂 FEtherBuildingFragment（以后加别的建筑种类不用改这里）
	const bool bEtherBuilding = (DefRow.Def.GetPtr<FAsceEtherBuildingDef>() != nullptr);

	UMassEntityConfigAsset* Config = DefRow.EntityTemplate.LoadSynchronous();
	UMassSpawnerSubsystem* SpawnerSubsystem = World->GetSubsystem<UMassSpawnerSubsystem>();
	if (!Config || !SpawnerSubsystem)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Building] 行 %s 没配 EntityTemplate（或 MassSpawnerSubsystem 不可用）"),
			*Placement.BuildingDefId.ToString());
		return FMassEntityHandle();
	}

	const FMassEntityTemplate& EntityTemplate = Config->GetOrCreateEntityTemplate(*World);
	if (!EntityTemplate.IsValid())
	{
		return FMassEntityHandle();
	}

	// **走模板生成**（和单位同一条路）：结构（fragment 组合 / 表现 trait）由模板决定
	TArray<FMassEntityHandle> SpawnedHandles;
	SpawnerSubsystem->SpawnEntities(EntityTemplate, 1, SpawnedHandles);
	if (SpawnedHandles.Num() == 0 || !SpawnedHandles[0].IsValid())
	{
		return FMassEntityHandle();
	}
	const FMassEntityHandle Entity = SpawnedHandles[0];
	FMassEntityManager& EntityManager = MassSub->GetMutableEntityManager();

	// 位置 = footprint 几何中心；朝向 = Rotation（Ether 定向/表现都用）
	const FVector2D CenterCell = Placement.GetCenterCell();
	const FVector CenterWorld((CenterCell.X + 0.5) * GRID_SIZE, (CenterCell.Y + 0.5) * GRID_SIZE, 0.f);
	if (FTransformFragment* Transform = EntityManager.GetFragmentDataPtr<FTransformFragment>(Entity))
	{
		Transform->SetTransform(FTransform(FQuat(FRotator(0.f, Placement.Rotation * 90.f, 0.f)), CenterWorld));
	}

	// 类型 id（覆盖模板里 trait 的默认值；**必须在 Ether 标记添加前写**，observer 靠它推 AddNode）
	const FName DefId = Placement.BuildingDefId;
	uint16 DefNumericId = 0;
	UAsceEntityDefLibrary::ParseEntityDefId(DefId, DefNumericId);
	if (FAsceUnitDefFragment* UnitDef = EntityManager.GetFragmentDataPtr<FAsceUnitDefFragment>(Entity))
	{
		UnitDef->UniqueTypeId = DefNumericId;
	}

	// 阵营（归属由摆放数据决定）
	if (FAsceTeamFragment* Team = EntityManager.GetFragmentDataPtr<FAsceTeamFragment>(Entity))
	{
		Team->TeamKey = Placement.TeamKey;
	}

	// footprint（+ Ether 标记）：**带真实值后加** ⇒ Add observer 在正确值上触发
	// （costfield/视野按真实占格写；Ether AddNode 用上面写好的类型 id）
	FIntPoint FootMin, FootMax;
	Placement.GetOccupiedBounds(FootMin, FootMax);
	TArray<FInstancedStruct> AddFragments;
	{
		FAsceGridFootprintFragment& Foot = AddFragments.Add_GetRef(FInstancedStruct::Make<FAsceGridFootprintFragment>())
			.GetMutable<FAsceGridFootprintFragment>();
		Foot.OriginGridLocation = FootMin;
		Foot.SizeX = FootMax.X - FootMin.X + 1;
		Foot.SizeY = FootMax.Y - FootMin.Y + 1;
		Foot.OcclusionHeightCm = Def->OcclusionHeightCm;
	}
	if (bEtherBuilding)
	{
		AddFragments.Add(FInstancedStruct::Make<FEtherBuildingFragment>());
	}
	EntityManager.AddFragmentInstanceListToEntity(Entity, AddFragments);

	// 保险：模板没挂 `UAsceRTSBuildingTrait` 时补 tag（正常建筑模板应包含）
	{
		const FMassEntityView View(EntityManager, Entity);
		if (!View.HasTag<FAsceBuildingTag>())
		{
			EntityManager.AddTagToEntity(Entity, FAsceBuildingTag::StaticStruct());
		}
	}

	FAsceBuiltBuilding& Record = Buildings.AddDefaulted_GetRef();
	Record.Entity = Entity;
	Record.Placement = Placement;

	// 建筑行为 core（可选；子系统持有，随建筑生灭）
	if (UClass* CoreClass = Def->CoreClass.LoadSynchronous())
	{
		Record.Core = NewObject<UAsceBuildingCore>(this, CoreClass);
		Record.Core->Initialize(Entity);
	}
	return Entity;
}
