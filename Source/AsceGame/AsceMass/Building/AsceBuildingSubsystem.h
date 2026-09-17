// AsceBuildingSubsystem.h
#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "AsceMass/Building/AsceBuildingTypes.h"
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

	// ── 运行期（服务器权威）──

	/** 建一个建筑（校验 → 建实体；observer 链自动生效：costfield/遮挡/建筑层/Ether）。 */
	bool PlaceBuilding(const FAsceBuildingPlacement& Placement, FString* OutError = nullptr);

	/** 拆一个建筑（销毁实体，全链自动复原）。 */
	bool DemolishBuilding(FMassEntityHandle Building, FString* OutError = nullptr);

	/** 校验（**纯查询、无副作用**）：配置有效 / 不越界 / 不与已有建筑重叠。权威端创建前 + 客户端放置前都用它。
	 *  `Ctx`（可空）= tick 阶段里的共享上下文；为空时（非 tick 的公开 API 调用）自行解析需要的子系统。 */
	bool ValidatePlacement(const FAsceBuildingPlacement& Placement, FString* OutError = nullptr, const FAsceTickContext* Ctx = nullptr) const;

	int32 GetBuildingCount() const { return Buildings.Num(); }
	bool IsBootstrapped() const { return bBootstrapped; }
	const TArray<FAsceBuiltBuilding>& GetBuildings() const { return Buildings; }

private:
	/** 创建建筑实体（初值一次给全：Transform + footprint + Ether 标记 + 阵营；tag 创建后补）。 */
	FMassEntityHandle CreateBuildingEntity(const FAsceBuildingPlacement& Placement, const FAsceTickContext* Ctx = nullptr);

	/** 已建成建筑（Entity ↔ 摆放数据）。 */
	UPROPERTY(Transient)
	TArray<FAsceBuiltBuilding> Buildings;

	/** 已报到、等引导的摆放项。 */
	UPROPERTY(Transient)
	TArray<FAscePendingPlacement> PendingPlacements;

	bool bBootstrapped = false;
	bool bWarnedNotReady = false;
};
