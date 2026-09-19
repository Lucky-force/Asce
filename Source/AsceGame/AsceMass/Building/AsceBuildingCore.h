// AsceBuildingCore.h
#pragma once

#include "CoreMinimal.h"
#include "MassEntityHandle.h"
#include "AsceBuildingCore.generated.h"

/** 建筑功能面板的一个按键（core 在选中时收集给 UI；点击按 ActionId 回传 core 执行）。 */
USTRUCT(BlueprintType)
struct FAsceBuildingAction
{
	GENERATED_BODY()

	/** 稳定 id（UI 回传 `ExecuteBuildingAction` 用；同一个 core 内唯一）。 */
	UPROPERTY(BlueprintReadOnly, Category = "Building")
	FName ActionId;

	UPROPERTY(BlueprintReadOnly, Category = "Building")
	FText Label;

	UPROPERTY(BlueprintReadOnly, Category = "Building")
	FText Tooltip;

	UPROPERTY(BlueprintReadOnly, Category = "Building")
	TSoftObjectPtr<UTexture2D> Icon;

	UPROPERTY(BlueprintReadOnly, Category = "Building")
	bool bEnabled = true;
};

/**
 * 建筑行为 core 基类（每个建筑一个实例，由 `UAsceBuildingSubsystem` 创建/持有/销毁）：
 * - 生命周期：`Initialize`（实体创建后）/ `Deinitialize`（拆除或世界销毁）；
 * - UI：选中时 `SetSelected(true)` → UI 拉 `CollectActions`；点击回传 `ExecuteAction(ActionId)`；
 * - 事件驱动为主，不强求 Tick（以后有生产队列之类的再逐帧）。
 *
 * 注意：与 Ether 的 `UAsceEtherProcessCore` 是两个东西——后者是"网络处理进程"，由 Ether 网络持有，
 * 生命周期跟随节点/图；本类是"建筑实例的行为/功能入口"，两者用实体/节点弱查询桥接，不互相持有。
 */
UCLASS(Abstract, BlueprintType, Blueprintable)
class UAsceBuildingCore : public UObject
{
	GENERATED_BODY()

public:
	/** 建筑实体创建后调用（早于任何选中）。 */
	virtual void Initialize(FMassEntityHandle InBuilding) { Building = InBuilding; }

	/** 拆除 / 世界销毁前调用。 */
	virtual void Deinitialize() {}

	/** 选中状态变化（UI 可据此开始/停止收集数据，省开销）。 */
	virtual void SetSelected(bool bSelected) {}

	/** 收集功能面板按键（选中状态下调用；点击后由 `ExecuteAction` 处理）。 */
	virtual void CollectActions(TArray<FAsceBuildingAction>& OutActions) const {}

	/** 执行按键；`ActionId` 由本 core 在 `CollectActions` 里定义。 */
	virtual void ExecuteAction(FName ActionId) {}

	FMassEntityHandle GetBuilding() const { return Building; }

protected:
	UPROPERTY()
	FMassEntityHandle Building;
};
