// AsceEtherBuildingTrait.h
#pragma once

#include "CoreMinimal.h"
#include "MassEntityTraitBase.h"
#include "AsceEtherBuildingTrait.generated.h"

class UAsceEtherBuildingConfig;

/**
 * 建筑 ether 功能 Trait：给建筑实体加 ether 的标记 fragment（+ 登记建筑配置）。
 * 建筑类型在配置里组合 Trait（基础 Trait + 本 Trait + 其它功能 Trait），不需要为每个类型写 C++；
 * 具体"是什么器件 / 有哪些端口"由 `BuildingConfig`（蓝图子类）与它的 `ProcessCore` 决定。
 */
UCLASS(BlueprintType, meta = (DisplayName = "Asce Ether Building"))
class UAsceEtherBuildingTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

public:
	/** 该建筑类型的配置（`UAsceEtherBuildingConfig` 的蓝图子类）。 */
	UPROPERTY(EditAnywhere, Category = "Ether")
	TSubclassOf<UAsceEtherBuildingConfig> BuildingConfig;

	/** 运行时用于反查的唯一 id（可空白，默认取配置类名）。 */
	UPROPERTY(EditAnywhere, Category = "Ether")
	FName BuildingDefId;

protected:
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
};
