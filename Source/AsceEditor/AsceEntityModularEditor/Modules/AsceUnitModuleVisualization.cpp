// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceEntityModularEditor/Modules/AsceUnitModuleVisualization.h"

#include "Engine/StaticMesh.h"
#include "MassEntityConfigAsset.h"
#include "MassMovableVisualizationTrait.h"
#include "MassRepresentationTypes.h"

FName FAsceUnitModuleVisualization::GetModuleId() const
{
	return FName(TEXT("Visualization"));
}

FText FAsceUnitModuleVisualization::GetDisplayName() const
{
	return INVTEXT("可视化");
}

void FAsceUnitModuleVisualization::Validate(TArray<FText>& OutErrors) const
{
	if (StaticMesh.IsNull())
	{
		OutErrors.Add(INVTEXT("可视化模块没有选择 StaticMesh"));
	}
}

void FAsceUnitModuleVisualization::WriteToTemplate(UMassEntityConfigAsset& ConfigAsset) const
{
	UMassMovableVisualizationTrait* Trait = Cast<UMassMovableVisualizationTrait>(
		ConfigAsset.AddTrait(UMassMovableVisualizationTrait::StaticClass()));
	if (!Trait)
	{
		return;
	}

	Trait->Modify();
	FStaticMeshInstanceVisualizationDesc& Desc = Trait->StaticMeshInstanceDesc;
	if (Desc.Meshes.Num() == 0)
	{
		Desc.Meshes.AddDefaulted();
	}

	// 整模缩放/旋转走 Desc.TransformOffset：
	// MassUpdateISMProcessor 会把它同时乘到 current/prev 两帧；MeshDesc.LocalTransform
	// 在 moving 路径只乘 current，非单位变换会闪。
	Desc.bUseTransformOffset = !LocalTransform.Equals(FTransform::Identity);
	Desc.TransformOffset = LocalTransform;

	FMassStaticMeshInstanceVisualizationMeshDesc& MeshDesc = Desc.Meshes[0];
	MeshDesc.Mesh = StaticMesh.LoadSynchronous();
	MeshDesc.LocalTransform = FTransform::Identity;
}
