// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceAnimDef.h"

#if WITH_EDITOR
#include "AnimToTextureDataAsset.h"      // 插件（编辑器构建才链接：见 Build.cs 的条件依赖）
#include "AnimToTextureBPLibrary.h"      // AnimationToTexture / ConvertSkeletalMeshToStaticMesh
#include "Animation/AnimSequence.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceAnimDef)

#if WITH_EDITOR

namespace
{
	FString MakeObjectPath(const FString& PackagePath, const FString& AssetName)
	{
		return FString::Printf(TEXT("%s/%s.%s"), *PackagePath, *AssetName, *AssetName);
	}

	/** 建/复用一张空的 `UTexture2D` 资产：插件烘焙只往"已存在的纹理对象"里写内容（字段为空 ⇒ 静默失败） */
	UTexture2D* FindOrCreateTextureAsset(const FString& PackagePath, const FString& AssetName)
	{
		if (UTexture2D* Existing = LoadObject<UTexture2D>(nullptr, *MakeObjectPath(PackagePath, AssetName)))
		{
			return Existing;
		}
		UPackage* Package = CreatePackage(*PackagePath);
		UTexture2D* NewTexture = NewObject<UTexture2D>(Package, *AssetName, RF_Public | RF_Standalone);
		if (NewTexture)
		{
			FAssetRegistryModule::AssetCreated(NewTexture);
			NewTexture->MarkPackageDirty();
		}
		return NewTexture;
	}

	/** 一次性纹理（不落盘、不注册）：只用于"薄烘"时接住不需要的位置/旋转输出，避免覆写共享动画纹理 */
	UTexture2D* MakeThrowawayTexture()
	{
		return NewObject<UTexture2D>(GetTransientPackage());
	}

	/** 把本资产的烘焙参数灌进（临时）插件 DA */
	void ApplyBakeParams(const UAsceAnimSet& AnimSet, UAnimToTextureDataAsset& DA)
	{
		DA.SkeletalMesh = AnimSet.SkeletalMesh;
		DA.Mode = (AnimSet.Mode == EAsceVATMode::Bone) ? EAnimToTextureMode::Bone : EAnimToTextureMode::Vertex;
		DA.Precision = (AnimSet.Precision == EAsceVATPrecision::SixteenBits)
			? EAnimToTexturePrecision::SixteenBits
			: EAnimToTexturePrecision::EightBits;
		DA.MaxWidth = AnimSet.MaxWidth;
		DA.MaxHeight = AnimSet.MaxHeight;
		DA.bEnforcePowerOfTwo = AnimSet.bEnforcePowerOfTwo;
		DA.SampleRate = FMath::Max(AnimSet.FrameRate, 1.f);
		// 注意：插件 `EAnimToTextureNumBoneInfluences` 的枚举值顺序未确认 ⇒ v1 不透传（用插件默认 Four），仅告警
		if (AnimSet.NumBoneInfluences != 4)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[AnimSet] %s: NumBoneInfluences=%d 暂未透传给插件（v1 固定 4）"), *AnimSet.GetName(), AnimSet.NumBoneInfluences);
		}
	}
}

void UAsceAnimSet::RebuildFromSources()
{
	// ① 校验
	if (!Skeleton || SkeletalMesh.IsNull() || Sources.Num() == 0 || Meshes.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[AnimSet] %s: 请先配 Skeleton / SkeletalMesh / Sources / Meshes"), *GetName());
		return;
	}
	USkeletalMesh* SourceMesh = SkeletalMesh.LoadSynchronous();
	if (!SourceMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("[AnimSet] %s: SkeletalMesh 加载失败"), *GetName());
		return;
	}

	const FString PackagePath = FPackageName::GetLongPackagePath(GetOutermost()->GetName());
	const FString AssetName = GetName();

	// ② 每个 mesh：生成/复用静态网格（烘焙输入 + 我们的产物）+ 建/复用它的权重纹理
	for (int32 MeshIndex = 0; MeshIndex < Meshes.Num(); ++MeshIndex)
	{
		FAsceVATMeshEntry& Entry = Meshes[MeshIndex];
		if (Entry.Mesh.IsNull())
		{
			// ⚠️ `LODIndex = -1` 用默认；将来要支持"从骨骼网格的哪个 LOD 采样"再补字段
			Entry.Mesh = UAnimToTextureBPLibrary::ConvertSkeletalMeshToStaticMesh(
				SourceMesh, FString::Printf(TEXT("%s/%s_Mesh%d"), *PackagePath, *AssetName, MeshIndex + 1), /*LODIndex=*/ -1);
		}
		if (Entry.BoneWeightTexture.IsNull())
		{
			Entry.BoneWeightTexture = FindOrCreateTextureAsset(
				PackagePath, FString::Printf(TEXT("%s_Mesh%d_Weight"), *AssetName, MeshIndex + 1));
		}
	}

	// ③ 共享动画纹理（先建/复用；内容随后被烘焙覆写）
	BonePositionTexture = BonePositionTexture.IsNull()
		? FindOrCreateTextureAsset(PackagePath, FString::Printf(TEXT("%s_BonePos"), *AssetName))
		: BonePositionTexture.LoadSynchronous();
	BoneRotationTexture = BoneRotationTexture.IsNull()
		? FindOrCreateTextureAsset(PackagePath, FString::Printf(TEXT("%s_BoneRot"), *AssetName))
		: BoneRotationTexture.LoadSynchronous();
	if (!BonePositionTexture.IsValid() || !BoneRotationTexture.IsValid()
		|| !BonePositionTexture.LoadSynchronous() || !BoneRotationTexture.LoadSynchronous())
	{
		UE_LOG(LogTemp, Error, TEXT("[AnimSet] %s: 共享动画纹理创建/加载失败"), *GetName());
		return;
	}

	// ④ 共享一轮烘焙：段表/图集布局/共享纹理都在这一轮产出
	UAnimToTextureDataAsset* Temp = NewObject<UAnimToTextureDataAsset>(GetTransientPackage());
	ApplyBakeParams(*this, *Temp);
	Temp->StaticMesh = Meshes[0].Mesh;
	Temp->BonePositionTexture = BonePositionTexture;
	Temp->BoneRotationTexture = BoneRotationTexture;
	Temp->BoneWeightTexture = Meshes[0].BoneWeightTexture;   // 参考 mesh 的权重也顺带产出
	Temp->AnimSequences.Reset();
	TArray<TSoftObjectPtr<UAnimSequence>> BakedSources;   // 与 Temp->AnimSequences 一一对应（读回时作 Clips 的键）
	for (const TSoftObjectPtr<UAnimSequence>& Source : Sources)
	{
		UAnimSequence* Sequence = Source.LoadSynchronous();
		if (!Sequence)
		{
			continue;
		}
		FAnimToTextureAnimSequenceInfo Info;
		Info.bEnabled = true;
		Info.AnimSequence = Sequence;
		Temp->AnimSequences.Add(Info);
		BakedSources.Add(Source);
	}

	if (!UAnimToTextureBPLibrary::AnimationToTexture(Temp))
	{
		UE_LOG(LogTemp, Error, TEXT("[AnimSet] %s: 共享烘焙失败（检查 SkeletalMesh/StaticMesh/Sources 与图集尺寸上限）"), *GetName());
		Temp->MarkAsGarbage();
		return;
	}

	// 读回：段表（键 = 源动画资产的软引用）+ 图集布局
	{
		TMap<TSoftObjectPtr<UAnimSequence>, FAsceAnimClip> NewClips;
		const int32 NumAnims = FMath::Min3(Temp->AnimSequences.Num(), Temp->Animations.Num(), BakedSources.Num());
		for (int32 Index = 0; Index < NumAnims; ++Index)
		{
			const TSoftObjectPtr<UAnimSequence>& ClipRef = BakedSources[Index];
			if (ClipRef.IsNull())
			{
				continue;
			}
			FAsceAnimClip Clip;
			Clip.AnimIndex = Index;
			Clip.StartFrame = Temp->Animations[Index].StartFrame;
			Clip.EndFrame = Temp->Animations[Index].EndFrame;
			NewClips.Add(ClipRef, Clip);
		}
		Clips = MoveTemp(NewClips);

		const UTexture2D* PositionTexture = BonePositionTexture.LoadSynchronous();
		Pages.Reset();
		FAsceAnimPageInfo Page;
		Page.NumFrames = Temp->NumFrames;
		Page.RowsPerFrame = FMath::Max(Temp->BoneRowsPerFrame, 1);
		Page.SizeX = PositionTexture ? PositionTexture->GetSizeX() : 0;
		Page.SizeY = PositionTexture ? PositionTexture->GetSizeY() : 0;
		Pages.Add(Page);
	}

	// ⑤ 每个 mesh 薄烘一次（只为产出各自的权重纹理；位置/旋转写进一次性纹理，避免覆写共享动画纹理）
	for (int32 MeshIndex = 0; MeshIndex < Meshes.Num(); ++MeshIndex)
	{
		UAnimToTextureDataAsset* Thin = NewObject<UAnimToTextureDataAsset>(GetTransientPackage());
		ApplyBakeParams(*this, *Thin);
		Thin->StaticMesh = Meshes[MeshIndex].Mesh;
		Thin->BoneWeightTexture = Meshes[MeshIndex].BoneWeightTexture;
		Thin->BonePositionTexture = MakeThrowawayTexture();
		Thin->BoneRotationTexture = MakeThrowawayTexture();
		Thin->AnimSequences.Reset();
		if (UAnimSequence* Sequence = Sources[0].LoadSynchronous())
		{
			FAnimToTextureAnimSequenceInfo Info;
			Info.bEnabled = true;
			Info.AnimSequence = Sequence;
			Info.bUseCustomRange = true;   // 只烘 1 帧（权重与动画无关）⇒ 省时间/磁盘
			Info.StartFrame = 0;
			Info.EndFrame = 0;
			Thin->AnimSequences.Add(Info);
		}
		if (!UAnimToTextureBPLibrary::AnimationToTexture(Thin))
		{
			UE_LOG(LogTemp, Warning, TEXT("[AnimSet] %s: mesh%d 的权重烘焙失败"), *GetName(), MeshIndex + 1);
		}
		Thin->MarkAsGarbage();
	}

	// ⑥ 删临时 DA（纹理/网格都是已存在的独立资产 ⇒ 不受影响）
	Temp->MarkAsGarbage();

	MarkPackageDirty();
	UE_LOG(LogTemp, Display, TEXT("[AnimSet] %s: 重烘完成（clip=%d，mesh=%d，帧数=%d）"),
		*GetName(), Clips.Num(), Meshes.Num(), Pages.Num() > 0 ? Pages[0].NumFrames : 0);

	// 烘完立刻自检（源未烘/段越界/引用为空/条目不完整都会报出来）
	ValidateAnimSet();
}

void UAsceAnimSet::ValidateAnimSet()
{
	int32 NumErrors = 0;
	int32 NumWarnings = 0;
	auto ReportError = [this, &NumErrors](const FString& Message)
	{
		++NumErrors;
		UE_LOG(LogTemp, Error, TEXT("[AnimSet校验] %s: %s"), *GetName(), *Message);
	};
	auto ReportWarning = [this, &NumWarnings](const FString& Message)
	{
		++NumWarnings;
		UE_LOG(LogTemp, Warning, TEXT("[AnimSet校验] %s: %s"), *GetName(), *Message);
	};

	// 骨架 / 源网格
	if (!Skeleton)
	{
		ReportError(TEXT("没配 Skeleton"));
	}
	if (SkeletalMesh.IsNull())
	{
		ReportError(TEXT("没配 SkeletalMesh"));
	}
	else if (Skeleton)
	{
		if (const USkeletalMesh* SourceMesh = SkeletalMesh.LoadSynchronous())
		{
			if (SourceMesh->GetSkeleton() != Skeleton)
			{
				ReportWarning(TEXT("SkeletalMesh 的 Skeleton 与资产的 Skeleton 不一致"));
			}
		}
	}

	// Sources：非空、无 null、路径唯一（Clips 键 = 资产软引用 ⇒ 同一资产重复会互相覆盖）
	{
		if (Sources.Num() == 0)
		{
			ReportError(TEXT("Sources 为空（没有要烘的动画）"));
		}
		TSet<FSoftObjectPath> SequencePaths;
		for (int32 Index = 0; Index < Sources.Num(); ++Index)
		{
			if (Sources[Index].IsNull())
			{
				ReportError(FString::Printf(TEXT("Sources[%d] 为空"), Index));
				continue;
			}
			const FSoftObjectPath Path = Sources[Index].ToSoftObjectPath();
			if (SequencePaths.Contains(Path))
			{
				ReportError(FString::Printf(TEXT("Sources[%d] 的资产 %s 重复（同一动画只烘一次）"),
					Index, *Path.GetAssetName()));
			}
			SequencePaths.Add(Path);
		}
	}

	// Clips：键（源资产）有效 + 区间合法 + 落在图集帧数内
	{
		if (Clips.Num() == 0)
		{
			ReportError(TEXT("Clips 为空（没重烘过？）"));
		}
		const int32 NumFrames = (Pages.Num() > 0) ? Pages[0].NumFrames : 0;
		for (const TPair<TSoftObjectPtr<UAnimSequence>, FAsceAnimClip>& Pair : Clips)
		{
			if (Pair.Key.IsNull())
			{
				ReportError(TEXT("Clips 里有一条空的动画引用 —— 是否忘记重烘？"));
				continue;
			}
			const FAsceAnimClip& Clip = Pair.Value;
			const FString ClipName = Pair.Key.ToSoftObjectPath().GetAssetName();
			if (Clip.StartFrame < 0 || Clip.EndFrame < Clip.StartFrame)
			{
				ReportError(FString::Printf(TEXT("clip %s 帧区间非法 [%d, %d]"), *ClipName, Clip.StartFrame, Clip.EndFrame));
			}
			else if (NumFrames > 0 && Clip.EndFrame >= NumFrames)
			{
				ReportError(FString::Printf(TEXT("clip %s 越界（[%d, %d] ≥ 图集帧数 %d）—— 是否忘记重烘？"),
					*ClipName, Clip.StartFrame, Clip.EndFrame, NumFrames));
			}
		}
		// Sources 必须都烘进 Clips（否则单位 def 引用了也解析不到）
		for (int32 Index = 0; Index < Sources.Num(); ++Index)
		{
			if (!Sources[Index].IsNull() && !Clips.Contains(Sources[Index]))
			{
				ReportWarning(FString::Printf(TEXT("Sources[%d]（%s）没有烘进 Clips —— 是否忘记重烘？"),
					Index, *Sources[Index].ToSoftObjectPath().GetAssetName()));
			}
		}
	}

	// Meshes：条目完整
	if (Meshes.Num() == 0)
	{
		ReportError(TEXT("Meshes 为空"));
	}
	for (int32 Index = 0; Index < Meshes.Num(); ++Index)
	{
		const FAsceVATMeshEntry& Entry = Meshes[Index];
		if (Entry.Mesh.IsNull())
		{
			ReportError(FString::Printf(TEXT("Meshes[%d] 没配 Mesh"), Index));
		}
		if (Entry.BoneWeightTexture.IsNull())
		{
			ReportError(FString::Printf(TEXT("Meshes[%d] 没配 BoneWeightTexture"), Index));
		}
	}

	UE_LOG(LogTemp, Display, TEXT("[AnimSet校验] %s: %d 个错误、%d 个警告"), *GetName(), NumErrors, NumWarnings);
}

#endif // WITH_EDITOR
