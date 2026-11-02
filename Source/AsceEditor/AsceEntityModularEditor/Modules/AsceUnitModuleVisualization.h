// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "AsceEntityModularEditor/AsceUnitModule.h"

#include "AsceUnitModuleVisualization.generated.h"

class UStaticMesh;
class UMassEntityConfigAsset;

/**
 * 可视化模块：写 **EntityTemplate 上的 `UMassMovableVisualizationTrait`**。
 *
 * - `StaticMesh` / `LocalTransform` 写到 `StaticMeshInstanceDesc.Meshes[0]`；
 * - 没有 Mesh 数组时自动补一个元素；
 * - 运行时模板生成器会确保模板带 DistanceLODCollector / MassMovableVisualizationTrait。
 */
USTRUCT()
struct FAsceUnitModuleVisualization : public FAsceUnitModule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Visualization")
	TSoftObjectPtr<UStaticMesh> StaticMesh;

	/** 整个可视化的 TransformOffset（缩放/旋转/整体偏移）；不再写 MeshDesc.LocalTransform */
	UPROPERTY(EditAnywhere, Category = "Visualization", meta = (DisplayName = "Transform Offset"))
	FTransform LocalTransform = FTransform::Identity;

	virtual FName GetModuleId() const override;
	virtual FText GetDisplayName() const override;
	virtual void Validate(TArray<FText>& OutErrors) const override;
	virtual void WriteToTemplate(UMassEntityConfigAsset& ConfigAsset) const override;
};
