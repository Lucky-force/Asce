// AsceBuildingSubsystem.cpp
#include "AsceBuildingSubsystem.h"

#include "AsceBuildingPlacementActor.h"
#include "AsceMass/Building/AsceBuildingDef.h"
#include "AsceMass/Building/AsceBuildingFragments.h"
#include "AsceMass/Data/AsceEntityDefLibrary.h"
#include "AsceMass/Ether/AsceEtherBuildingDef.h"
#include "AsceMass/Ether/AsceEtherFragments.h"
#include "AsceMass/Ether/AsceEtherNetworkSubsystem.h"
#include "AsceMass/FlowField/AsceFlowFieldCores.h"
#include "AsceMass/FlowField/AscePathFinder.h"
#include "AsceMass/FlowField/AscePathFindingSubsystem.h"
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
				CreateConstructingBuilding(Pending.Placement);
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
			if (CreateConstructingBuilding(Pending.Placement, &Ctx).IsValid())
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

	if (!CreateConstructingBuilding(Placement).IsValid())
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
	// 建造中的工地：销毁实体即可（没有 footprint ⇒ 观察者不会触发）；占位立刻释放
	PendingBuilds.RemoveSingle(Building);
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

	const UAscePathFindingSubsystem* PathFinding = UAscePathFindingSubsystem::Get(this);
	const UPathFinder* Finder = PathFinding ? PathFinding->GetPathFinder() : nullptr;
	const FWorldMapData* Map = Finder ? Finder->GetWorldMapData() : nullptr;
	if (!Map)
	{
		SetError(OutError, TEXT("世界地图数据未就绪（AAsceWorldMapReplicatorActor / 世界初始化没跑？）"));
		return false;
	}

	FIntPoint Min, Max;
	Placement.GetOccupiedBounds(Min, Max);
	const int32 DimX = Map->GetGridCountX();
	const int32 DimY = Map->GetGridCountY();
	if (Min.X < 0 || Min.Y < 0 || Max.X >= DimX || Max.Y >= DimY)
	{
		SetError(OutError, FString::Printf(TEXT("越界：占用格 (%d,%d)-(%d,%d) 超出地图 %dx%d"),
			Min.X, Min.Y, Max.X, Max.Y, DimX, DimY));
		return false;
	}

	// ① 目标格必须是"纯可通行"（cost == 1）：既排除地形墙/地形代价，也排除已建成建筑（建筑写阻挡值）
	for (int32 y = Min.Y; y <= Max.Y; ++y)
	{
		for (int32 x = Min.X; x <= Max.X; ++x)
		{
			if (Map->IsCellBlocked(x, y))
			{
				SetError(OutError, FString::Printf(TEXT("目标格 (%d,%d) 被阻挡（地形或已有建筑）"), x, y));
				return false;
			}
			if (!Map->IsCellCleanPassable(x, y))
			{
				SetError(OutError, FString::Printf(TEXT("目标格 (%d,%d) 不是纯可通行格（地形代价/特殊占用），暂不可建"), x, y));
				return false;
			}
		}
	}

	// ② 不与"建造中"的工地重叠（它们还没写 costfield，占位由 pending 列表兜）
	if (const UMassEntitySubsystem* MassSub = GetWorld() ? GetWorld()->GetSubsystem<UMassEntitySubsystem>() : nullptr)
	{
		const FMassEntityManager& EntityManager = MassSub->GetEntityManager();
		for (const FMassEntityHandle Pending : PendingBuilds)
		{
			const FAsceConstructingFragment* Constructing = EntityManager.GetFragmentDataPtr<FAsceConstructingFragment>(Pending);
			if (!Constructing)
			{
				continue;
			}
			FAsceBuildingPlacement PendingPlacement;
			PendingPlacement.OriginGridLocation = Constructing->OriginGridLocation;
			PendingPlacement.Size = Constructing->Size;
			PendingPlacement.Rotation = Constructing->Rotation;

			FIntPoint PendingMin, PendingMax;
			PendingPlacement.GetOccupiedBounds(PendingMin, PendingMax);
			if (PendingMin.X <= Max.X && Min.X <= PendingMax.X && PendingMin.Y <= Max.Y && Min.Y <= PendingMax.Y)
			{
				SetError(OutError, FString::Printf(TEXT("与建造中的工地重叠（占用格 (%d,%d)-(%d,%d)）"),
					PendingMin.X, PendingMin.Y, PendingMax.X, PendingMax.Y));
				return false;
			}
		}
	}

	return true;
}

FMassEntityHandle UAsceBuildingSubsystem::CreateConstructingBuilding(const FAsceBuildingPlacement& Placement, const FAsceTickContext* Ctx)
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
	// **结构契约**：Transform / 身份 / 阵营都是"实体基础"（`UAsceRTSEntityTrait` 提供，单位与建筑都需要），
	// 建筑 def 行的 `EntityTemplate` 必须挂它；缺了就是配置错误 ⇒ ensure 报错（缺 Transform 时位置会留在原点，
	// 缺 `FAsceUnitDefFragment` 时 Ether 注册观察者根本不匹配 ⇒ 建筑对 Ether 完全不可见且毫无其它提示）。
	const FName DefIdForLog = Placement.BuildingDefId;
	const FVector2D CenterCell = Placement.GetCenterCell();

	// 位置 = footprint 几何中心；**Z 由共享的 `UAsceTerrainHeightProcessor` 负责贴地**
	// （它的查询只要 `FTransformFragment` ⇒ 单位与建筑都会被采样；见 `AsceTerrainHeightProcessor.cpp`）。
	// 这里先给 0，同一帧的移动阶段就会把它贴到地形高度上。
	const FVector CenterWorld((CenterCell.X + 0.5) * GRID_SIZE, (CenterCell.Y + 0.5) * GRID_SIZE, 0.f);
	FTransformFragment* Transform = EntityManager.GetFragmentDataPtr<FTransformFragment>(Entity);
	ensureMsgf(Transform, TEXT("[Building] EntityTemplate 缺 FTransformFragment：建筑 def '%s' 的模板必须包含 UAsceRTSEntityTrait"), *DefIdForLog.ToString());
	if (Transform)
	{
		Transform->SetTransform(FTransform(FQuat(FRotator(0.f, Placement.Rotation * 90.f, 0.f)), CenterWorld));
	}

	// 类型 id（覆盖模板里 trait 的默认值；**必须在 Ether 标记添加前写**，observer 靠它推 AddNode）
	// 行名非法（非 1..65535 数字串）**直接崩**：纯数据错误，静默变 0 会让建筑在 Ether 里"没有端口"，极难排查
	const uint16 DefNumericId = UAsceEntityDefLibrary::ParseEntityDefIdChecked(Placement.BuildingDefId);
	FAsceUnitDefFragment* UnitDef = EntityManager.GetFragmentDataPtr<FAsceUnitDefFragment>(Entity);
	ensureMsgf(UnitDef, TEXT("[Building] EntityTemplate 缺 FAsceUnitDefFragment：建筑 def '%s' 的模板必须包含 UAsceRTSEntityTrait（否则 Ether 注册观察者不匹配，建筑对 Ether 不可见）"), *DefIdForLog.ToString());
	if (UnitDef)
	{
		UnitDef->UniqueTypeId = DefNumericId;
	}

	// 阵营（归属由摆放数据决定）
	FAsceTeamFragment* Team = EntityManager.GetFragmentDataPtr<FAsceTeamFragment>(Entity);
	ensureMsgf(Team, TEXT("[Building] EntityTemplate 缺 FAsceTeamFragment：建筑 def '%s' 的模板必须包含 UAsceRTSEntityTrait"), *DefIdForLog.ToString());
	if (Team)
	{
		Team->TeamKey = Placement.TeamKey;
	}

	// **建造中**：实体只带 `FAsceConstructingFragment`（**不写 footprint**）⇒ costfield/视野/Ether 的观察者
	// 这一帧都不会触发（建造期不挡路）。完工由调度器的施工阶段（`RunConstructionStage`）推进。
	const double Now = World->GetTimeSeconds();
	{
		TArray<FInstancedStruct> AddFragments;
		FAsceConstructingFragment& Constructing = AddFragments.Add_GetRef(FInstancedStruct::Make<FAsceConstructingFragment>())
			.GetMutable<FAsceConstructingFragment>();
		Constructing.BuildingDefId = Placement.BuildingDefId;
		Constructing.OriginGridLocation = Placement.OriginGridLocation;
		Constructing.Size = Placement.Size;
		Constructing.Rotation = static_cast<uint8>(Placement.Rotation);
		Constructing.OcclusionHeightCm = Def->OcclusionHeightCm;
		Constructing.bEtherBuilding = bEtherBuilding;
		Constructing.StartTimeSeconds = Now;
		Constructing.BuildTimeSeconds = FMath::Max(0.f, Def->BuildTimeSeconds);
		EntityManager.AddFragmentInstanceListToEntity(Entity, AddFragments);
	}

	FAsceBuiltBuilding& Record = Buildings.AddDefaulted_GetRef();
	Record.Entity = Entity;
	Record.Placement = Placement;
	Record.bComplete = false;

	// 建造中占位（放置校验用）：footprint 的 Add 观察者跑过之后由 `ConsumePendingBuild` 释放
	PendingBuilds.Add(Entity);

	// 建筑行为 core（可选；子系统持有，随建筑生灭）
	if (UClass* CoreClass = Def->CoreClass.LoadSynchronous())
	{
		Record.Core = NewObject<UAsceBuildingCore>(this, CoreClass);
		Record.Core->Initialize(Entity);
	}
	return Entity;
}

bool UAsceBuildingSubsystem::CompleteConstruction(FMassEntityHandle Building, const FAsceTickContext* Ctx)
{
	UMassEntitySubsystem* MassSub = (Ctx && Ctx->Mass)
		? Ctx->Mass
		: (GetWorld() ? GetWorld()->GetSubsystem<UMassEntitySubsystem>() : nullptr);
	if (!MassSub)
	{
		return false;
	}
	FMassEntityManager& EntityManager = MassSub->GetMutableEntityManager();
	const FAsceConstructingFragment* Constructing = EntityManager.GetFragmentDataPtr<FAsceConstructingFragment>(Building);
	if (!Constructing)
	{
		return false;
	}

	// 建造 fragment 里存的是**未旋转**占位 ⇒ 按它换算最终 AABB 再写 footprint
	FAsceBuildingPlacement Placement;
	Placement.OriginGridLocation = Constructing->OriginGridLocation;
	Placement.Size = Constructing->Size;
	Placement.Rotation = Constructing->Rotation;
	const int32 OcclusionHeightCm = Constructing->OcclusionHeightCm;
	const bool bEtherBuilding = Constructing->bEtherBuilding;

	FIntPoint FootMin, FootMax;
	Placement.GetOccupiedBounds(FootMin, FootMax);

	// footprint（+ Ether 标记）：**带真实值后加** ⇒ Add observer 在正确值上触发（costfield/视野/Ether AddNode）
	TArray<FInstancedStruct> AddFragments;
	{
		FAsceGridFootprintFragment& Foot = AddFragments.Add_GetRef(FInstancedStruct::Make<FAsceGridFootprintFragment>())
			.GetMutable<FAsceGridFootprintFragment>();
		Foot.OriginGridLocation = FootMin;
		Foot.SizeX = FootMax.X - FootMin.X + 1;
		Foot.SizeY = FootMax.Y - FootMin.Y + 1;
		Foot.OcclusionHeightCm = OcclusionHeightCm;
	}
	if (bEtherBuilding)
	{
		AddFragments.Add(FInstancedStruct::Make<FEtherBuildingFragment>());
	}
	EntityManager.AddFragmentInstanceListToEntity(Building, AddFragments);

	// 保险：模板没挂 `UAsceRTSBuildingTrait` 时补 tag
	{
		const FMassEntityView View(EntityManager, Building);
		if (!View.HasTag<FAsceBuildingTag>())
		{
			EntityManager.AddTagToEntity(Building, FAsceBuildingTag::StaticStruct());
		}
	}

	// 删掉建造 fragment（结构变化 ⇒ 此后不要再持有它的指针）
	EntityManager.RemoveFragmentFromEntity(Building, FAsceConstructingFragment::StaticStruct());

	if (FAsceBuiltBuilding* Record = Buildings.FindByPredicate(
		[Building](const FAsceBuiltBuilding& R) { return R.Entity == Building; }))
	{
		Record->bComplete = true;
	}
	return true;
}

void UAsceBuildingSubsystem::RunConstructionStage(const FAsceTickContext& Ctx)
{
	if (PendingBuilds.Num() == 0)
	{
		return;
	}
	UMassEntitySubsystem* MassSub = Ctx.Mass
		? Ctx.Mass
		: (GetWorld() ? GetWorld()->GetSubsystem<UMassEntitySubsystem>() : nullptr);
	if (!MassSub)
	{
		return;
	}
	FMassEntityManager& EntityManager = MassSub->GetMutableEntityManager();
	const UWorld* World = Ctx.World ? Ctx.World : GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;

	// 倒序：只删"无效"项，不受完工导致的 archetype 变化影响
	for (int32 i = PendingBuilds.Num() - 1; i >= 0; --i)
	{
		const FMassEntityHandle Entity = PendingBuilds[i];
		if (!EntityManager.IsEntityValid(Entity))
		{
			PendingBuilds.RemoveAt(i);   // 实体没了（被销毁/异常）：占位直接丢掉
			continue;
		}

		const FAsceConstructingFragment* Constructing = EntityManager.GetFragmentDataPtr<FAsceConstructingFragment>(Entity);
		if (!Constructing)
		{
			// 没有建造 fragment：多半是已完工、正等 footprint 观察者消费（占位继续保留到那时）
			continue;
		}
		if (Constructing->BuildTimeSeconds > 0.f
			&& (Now - Constructing->StartTimeSeconds) < static_cast<double>(Constructing->BuildTimeSeconds))
		{
			continue;   // 还在建
		}

		// 先存工期：`CompleteConstruction` 会删掉建造 fragment，之后不能再解引用 `Constructing`
		const float BuildTimeSeconds = Constructing->BuildTimeSeconds;
		if (CompleteConstruction(Entity, &Ctx))
		{
			UE_LOG(LogTemp, Log, TEXT("[Building] 完工：entity=(%d,%d) 工期=%.1fs（占位等 footprint 观察者释放）"),
				Entity.Index, Entity.SerialNumber, BuildTimeSeconds);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[Building] 完工失败：entity=(%d,%d)"), Entity.Index, Entity.SerialNumber);
		}
	}
}

void UAsceBuildingSubsystem::ConsumePendingBuild(FMassEntityHandle Building)
{
	PendingBuilds.RemoveSingle(Building);
}
