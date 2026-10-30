// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceEntityModularEditor/AsceUnitModulePreviewViewport.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "EditorViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "PreviewScene.h"
#include "PrimitiveDrawingUtils.h"
#include "SceneManagement.h"
#include "UObject/StrongObjectPtr.h"

#include "AsceMass/Combat/AsceAnimDef.h"

#include "AsceEntityModularEditor/AsceUnitModuleConfig.h"

FAsceUnitModulePreviewClient::FAsceUnitModulePreviewClient(FPreviewScene* InPreviewScene, const TSharedPtr<SEditorViewport>& InViewportWidget)
	: FEditorViewportClient(nullptr, InPreviewScene, InViewportWidget)
{
	SetViewLocation(FVector(-650.f, -150.f, 600.f));
	SetViewRotation(FRotator(-35.f, 20.f, 0.f));
	SetRealtime(true);
}

void FAsceUnitModulePreviewClient::ApplyPreview(const FAsceUnitModulePreviewActions& Actions)
{
	Circles = Actions.Circles;
	Capsules = Actions.Capsules;
	Boxes = Actions.Boxes;
	Invalidate();
}

void FAsceUnitModulePreviewClient::Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	FEditorViewportClient::Draw(View, PDI);

	const FVector Base(0.f, 0.f, 0.f);

	// 1m 网格（尺度参考）
	{
		constexpr float GridCm = 100.f;
		constexpr int32 HalfCells = 10;
		const FLinearColor GridColor(0.22f, 0.22f, 0.22f);
		for (int32 Index = -HalfCells; Index <= HalfCells; ++Index)
		{
			const float Offset = Index * GridCm;
			PDI->DrawLine(FVector(Offset, -HalfCells * GridCm, 0.f), FVector(Offset, HalfCells * GridCm, 0.f), GridColor, SDPG_World);
			PDI->DrawLine(FVector(-HalfCells * GridCm, Offset, 0.f), FVector(HalfCells * GridCm, Offset, 0.f), GridColor, SDPG_World);
		}
	}

	// 各模块提供的几何
	for (const FAsceUnitModulePreviewActions::FCirclePreview& Circle : Circles)
	{
		DrawCircle(PDI, Base + FVector(0.f, 0.f, Circle.Z + 2.f), FVector::ForwardVector, FVector::RightVector
			, Circle.Color, Circle.RadiusCm, 64, SDPG_World);
	}
	for (const FAsceUnitModulePreviewActions::FCapsulePreview& Capsule : Capsules)
	{
		DrawWireCapsule(PDI, Base, FVector::ForwardVector, FVector::RightVector, FVector::UpVector
			, Capsule.Color, Capsule.RadiusCm, Capsule.HalfHeightCm, 16, SDPG_World);
	}
	for (const FAsceUnitModulePreviewActions::FBoxPreview& Box : Boxes)
	{
		DrawWireBox(PDI, FBox(-Box.ExtentCm, Box.ExtentCm), Box.Color, SDPG_World);
	}
}

void FAsceUnitModulePreviewClient::SetAnim(UAsceAnimSet* InAnimSet, const FAsceAnimClip& Clip)
{
	AnimSet.Reset();
	ClipFirstFrame = 0;
	ClipLastFrame = 0;
	FrameAccumulator = 0.f;
	CurrentFrame = INDEX_NONE;
	PreviousFrame = INDEX_NONE;

	if (InAnimSet)
	{
		AnimSet.Reset(InAnimSet);
		ClipFirstFrame = Clip.StartFrame;
		ClipLastFrame = FMath::Max(Clip.EndFrame, ClipFirstFrame);
		ClipFrameRate = FMath::Max(InAnimSet->FrameRate, 1.f);
		PlayRate = 1.f;
		EnsureMeshComponent(InAnimSet);
		ApplyFrame(/*bForce*/ true);
		return;
	}

	// 清掉预览网格
	if (UInstancedStaticMeshComponent* Component = MeshComponent.Get())
	{
		Component->ClearInstances();
	}
}

void FAsceUnitModulePreviewClient::EnsureMeshComponent(const UAsceAnimSet* InAnimSet)
{
	UInstancedStaticMeshComponent* Component = MeshComponent.Get();
	FPreviewScene* Scene = GetPreviewScene();
	UWorld* World = Scene ? Scene->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}
	if (!Component)
	{
		Component = NewObject<UInstancedStaticMeshComponent>(World);
		Component->NumCustomDataFloats = 2;   // [0]=Frame, [1]=PrevFrame
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Scene->AddComponent(Component, FTransform::Identity);
		MeshComponent = Component;
	}

	if (!Component)
	{
		return;
	}

	Component->ClearInstances();

	// 优先用 AnimSet 里烘焙好的（Mesh + MI）；没有就只显示几何
	if (InAnimSet->Meshes.Num() > 0)
	{
		const FAsceVATMeshEntry& Entry = InAnimSet->Meshes[0];
		if (UStaticMesh* Mesh = Entry.Mesh.LoadSynchronous())
		{
			Component->SetStaticMesh(Mesh);
			if (UMaterialInterface* Material = Entry.Material.LoadSynchronous())
			{
				Component->SetMaterial(0, Material);
			}
			Component->AddInstance(FTransform::Identity, /*bWorldSpace*/ true);
		}
	}
}

void FAsceUnitModulePreviewClient::ApplyFrame(const bool bForce)
{
	UInstancedStaticMeshComponent* Component = MeshComponent.Get();
	if (!Component || Component->GetInstanceCount() == 0 || ClipLastFrame < ClipFirstFrame)
	{
		return;
	}

	if (!bForce && CurrentFrame == INDEX_NONE)
	{
		return;
	}

	Component->SetCustomDataValue(0, 0, static_cast<float>(CurrentFrame), /*bMarkRenderStateDirty*/ false);
	Component->SetCustomDataValue(0, 1, static_cast<float>(PreviousFrame), true);
}

void FAsceUnitModulePreviewClient::Tick(const float DeltaSeconds)
{
	FEditorViewportClient::Tick(DeltaSeconds);

	if (ClipLastFrame > ClipFirstFrame)
	{
		const int32 FrameCount = ClipLastFrame - ClipFirstFrame + 1;
		FrameAccumulator += DeltaSeconds * ClipFrameRate * PlayRate;

		const int32 Advance = FMath::FloorToInt(FrameAccumulator);
		if (Advance > 0)
		{
			FrameAccumulator -= static_cast<float>(Advance);

			const int32 Previous = (CurrentFrame == INDEX_NONE) ? ClipFirstFrame : CurrentFrame;
			PreviousFrame = Previous;
			CurrentFrame = ClipFirstFrame + ((Previous - ClipFirstFrame + Advance) % FrameCount);
			ApplyFrame(/*bForce*/ false);
		}
		else if (CurrentFrame == INDEX_NONE)
		{
			CurrentFrame = ClipFirstFrame;
			PreviousFrame = ClipFirstFrame;
			ApplyFrame(/*bForce*/ false);
		}
	}
}

void SAsceUnitModulePreviewViewport::Construct(const FArguments& InArgs)
{
	PreviewScene = MakeShared<FPreviewScene>(FPreviewScene::ConstructionValues());
	SEditorViewport::Construct(SEditorViewport::FArguments());
}

TSharedRef<FEditorViewportClient> SAsceUnitModulePreviewViewport::MakeEditorViewportClient()
{
	PreviewClient = MakeShared<FAsceUnitModulePreviewClient>(PreviewScene.Get(), SharedThis(this));
	return PreviewClient.ToSharedRef();
}

void SAsceUnitModulePreviewViewport::ApplyPreview(const FAsceUnitModulePreviewActions& Actions)
{
	if (PreviewClient.IsValid())
	{
		PreviewClient->ApplyPreview(Actions);
	}
}

void SAsceUnitModulePreviewViewport::SetAnim(UAsceAnimSet* InAnimSet, const FAsceAnimClip& Clip)
{
	if (PreviewClient.IsValid())
	{
		PreviewClient->SetAnim(InAnimSet, Clip);
	}
}
