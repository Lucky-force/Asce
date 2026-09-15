// AsceEtherBuildingTrait.h
#pragma once

#include "CoreMinimal.h"
#include "MassEntityTraitBase.h"
#include "AsceEtherBuildingTrait.generated.h"

class UAsceEtherBuildingData;

/**
 * 建筑 ether 功能 Trait：给建筑实体加 ether 的网络 fragment。
 * 建筑类型在配置里组合 Trait（基础 Trait + 本 Trait + 其它功能 Trait），不需要为每个类型写 C++。
 */
UCLASS(BlueprintType, meta = (DisplayName = "Asce Ether Building"))
class UAsceEtherBuildingTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

public:
	/** 该建筑类型的 ether 静态数据（端口布局/配方/等级）。 */
	UPROPERTY(EditAnywhere, Category = "Ether")
	TObjectPtr<UAsceEtherBuildingData> BuildingData;

	/** 运行时用于反查的唯一 id（可空白，默认取资产名）。 */
	UPROPERTY(EditAnywhere, Category = "Ether")
	FName BuildingDefId;

protected:
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
};
