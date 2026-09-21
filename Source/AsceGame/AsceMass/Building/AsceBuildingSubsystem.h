// AsceBuildingSubsystem.h
#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "AsceMass/Building/AsceBuildingTypes.h"
#include "AsceMass/Building/AsceBuildingCore.h"
#include "AsceMass/Simulation/AsceTickContext.h"
#include "AsceBuildingSubsystem.generated.h"

class AAsceBuildingPlacementActor;

/**
 * 建筑系统（最小闭环，见 `AsceMass/Building/建筑系统设计.md`）：
 * - 摆放 Actor 在 BeginPlay 向本系统**报到**（两端都报到）；
 * - `RunBootstrapStage`（调度器**阶段 0**）：一切就绪后，权威端把预摆建筑一次性落地成真实实体，
 *   然后**两端都销毁**摆放 Actor（表现后续由实体复制统一做）；
 * - 运行期增删走 `PlaceBuilding` / `DemolishBuilding`（**服务器权威**；校验不许重叠）。
 *
 * 注意：本系统不做"表现"、不做"存档"（后续统一做），只维护"建筑集合"这一份真相。
 */
UCLASS(MinimalAPI)
class UAsceBuildingSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UAsceBuildingSubsystem();

	static UAsceBuildingSubsystem* Get(const UObject* WorldContextObject);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;

	/** 摆放 Actor 报到（BeginPlay 调用；两端都报到，只有权威端会真的建）。 */
	void RegisterPlacement(AAsceBuildingPlacementActor* PlacementActor);

	/** **调度器阶段 0**：一切就绪后落地预摆建筑（权威端）+ 两端销毁摆放 Actor；一次性，之后零开销。 */
	void RunBootstrapStage(const FAsceTickContext& Ctx);

	/** **调度器阶段 0b**：推进"建造中"建筑（工期到期 ⇒ 补 footprint/Ether 标记，观察者随之生效）。 */
	void RunConstructionStage(const FAsceTickContext& Ctx);

	/** 建造中占位释放：由 `UAsceBuildingPendingObserverProcessor`（footprint Add 观察者）在 costfield 写完之后调用。
	 *  **不要**在完工那一刻自己清 —— 否则会出现"占位已清、costfield 还没写"的空窗（同帧后续校验会钻过去）。 */
	void ConsumePendingBuild(FMassEntityHandle Building);

	/** 是否是"建造中"（还没 footprint）。 */
	bool IsConstructing(FMassEntityHandle Building) const { return PendingBuilds.Contains(Building); }

	// ── 运行期（服务器权威）──

	/** 建一个建筑：创建"建造中实体"（只有 `FAsceConstructingFragment`，不挡路）；完工时由施工阶段补 footprint。 */
	bool PlaceBuilding(const FAsceBuildingPlacement& Placement, FString* OutError = nullptr);

	/** 拆一个建筑（建造中 = 直接取消；已完工 = 销毁实体，costfield/遮挡/建筑层/Ether 由观察者复原）。 */
	bool DemolishBuilding(FMassEntityHandle Building, FString* OutError = nullptr);

	/** 校验（**纯查询、无副作用**）：类型有效 / 不越界 / 目标格是"纯可通行"（地形 + 已建成建筑都没占）/
	 *  不与"建造中"的工地重叠。权威端创建前 + 客户端放置前都用它。
	 *  `Ctx`（可空）= tick 阶段里的共享上下文；为空时（非 tick 的公开 API 调用）自行解析需要的子系统。 */
	bool ValidatePlacement(const FAsceBuildingPlacement& Placement, FString* OutError = nullptr, const FAsceTickContext* Ctx = nullptr) const;

	int32 GetBuildingCount() const { return Buildings.Num(); }
	bool IsBootstrapped() const { return bBootstrapped; }
	const TArray<FAsceBuiltBuilding>& GetBuildings() const { return Buildings; }
	const TArray<FMassEntityHandle>& GetPendingBuilds() const { return PendingBuilds; }

	// ── 建筑行为 core（UI/功能入口） ──

	/** 建筑行为 core（定义里没配 CoreClass 则为空）。 */
	UAsceBuildingCore* GetBuildingCore(FMassEntityHandle Building) const;

	/** 收集建筑功能面板按键（选中检视器用；内部转发给 core）。 */
	void CollectBuildingActions(FMassEntityHandle Building, TArray<FAsceBuildingAction>& OutActions) const;

	/** 执行建筑功能按键（UI 点击 → 对应 core 的 `ExecuteAction`）。 */
	bool ExecuteBuildingAction(FMassEntityHandle Building, FName ActionId, FString* OutError = nullptr);

	/** 选中状态变化（转发给 core，供其开始/停止收集数据）。 */
	void SetBuildingSelected(FMassEntityHandle Building, bool bSelected);

private:
	/** 创建"建造中实体"（模板生成 + Transform/身份/阵营初值 + `FAsceConstructingFragment`）。 */
	FMassEntityHandle CreateConstructingBuilding(const FAsceBuildingPlacement& Placement, const FAsceTickContext* Ctx = nullptr);

	/** 完工：按 `FAsceConstructingFragment` 补 footprint（+ Ether 标记 + tag），删掉建造 fragment。 */
	bool CompleteConstruction(FMassEntityHandle Building, const FAsceTickContext* Ctx = nullptr);

	/** 已登记建筑（Entity ↔ 摆放数据；含建造中）。 */
	UPROPERTY(Transient)
	TArray<FAsceBuiltBuilding> Buildings;

	/** **建造中占位**（还没 footprint 的工地；放置校验用；由 footprint Add 观察者消费）。 */
	UPROPERTY(Transient)
	TArray<FMassEntityHandle> PendingBuilds;

	/** 已报到、等引导的摆放项。 */
	UPROPERTY(Transient)
	TArray<FAscePendingPlacement> PendingPlacements;

	bool bBootstrapped = false;
	bool bWarnedNotReady = false;
};
