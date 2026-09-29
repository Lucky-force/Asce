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
#include "Materials/MaterialInstanceConstant.h"
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

	/** 标准资产包路径 `<目录>/<资产名>`（一包一资产；不能拿"目录"本身当包名，否则 Content Browser 里看不到） */
	FString MakeStandardPackagePath(const FString& FolderPath, const FString& AssetName)
	{
		return FString::Printf(TEXT("%s/%s"), *FolderPath, *AssetName);
	}

	/** 旧版工具把产物塞进了"包名 = AnimSet 所在目录"的包（CB 看不到）：识别出来当"没有"，改到标准位置重建 */
	bool IsLegacyHiddenAsset(const UObject* Asset, const FString& FolderPath)
	{
		return Asset != nullptr && Asset->GetOutermost()->GetName() == FolderPath;
	}

	/** 建/复用一张空的 `UTexture2D` 资产（标准位置）：插件烘焙只往"已存在的纹理对象"里写内容（字段为空 ⇒ 静默失败） */
	UTexture2D* FindOrCreateTextureAsset(const FString& FolderPath, const FString& AssetName)
	{
		if (UTexture2D* Existing = LoadObject<UTexture2D>(nullptr, *MakeObjectPath(FolderPath, AssetName)))
		{
			return Existing;
		}
		UPackage* Package = CreatePackage(*MakeStandardPackagePath(FolderPath, AssetName));
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

	/** MI 的父材质：优先 `AnimSet.MaterialParent`，为空则用插件自带的 BoneAnimation 示例材质 */
	UMaterialInterface* ResolveMaterialParent(const UAsceAnimSet& AnimSet)
	{
		if (UMaterialInterface* Parent = AnimSet.MaterialParent.LoadSynchronous())
		{
			return Parent;
		}
		return LoadObject<UMaterialInterface>(nullptr,
			TEXT("/AnimToTexture/Characters/Mannequin/Materials/BoneAnimation/M_Body_BoneAnimation.M_Body_BoneAnimation"));
	}

	/** 建/复用 MaterialInstanceConstant 资产（与纹理同目录；已存在就复用、不重置父材质） */
	UMaterialInstanceConstant* FindOrCreateMaterialInstance(const FString& PackagePath, const FString& AssetName, UMaterialInterface* Parent)
	{
		if (UMaterialInstanceConstant* Existing = LoadObject<UMaterialInstanceConstant>(nullptr, *MakeObjectPath(PackagePath, AssetName)))
		{
			return Existing;
		}
		if (!Parent)
		{
			return nullptr;
		}
		UPackage* Package = CreatePackage(*MakeStandardPackagePath(PackagePath, AssetName));
		UMaterialInstanceConstant* NewMIC = NewObject<UMaterialInstanceConstant>(Package, *AssetName, RF_Public | RF_Standalone);
		if (NewMIC)
		{
			NewMIC->SetParentEditorOnly(Parent);
			FAssetRegistryModule::AssetCreated(NewMIC);
			NewMIC->MarkPackageDirty();
		}
		return NewMIC;
	}

	// 绑定数据（顶点 → 权重图 texel 的映射）写入的 UV 通道；与临时 DA 的默认 UVChannel 一致，
	// 也决定 MI 的 UseUVx 静态开关（见 UpdateMaterialInstanceFromDataAsset）
	constexpr int32 AnimBakeUVChannel = 1;

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
	// ① 校验（`Meshes` 可留空：自动补一条默认外观 ⇒ 只想填"骨骼 / 骨骼网格 / 动画"也能一键重烘）
	// 注意：软引用判"配没配"必须用 `IsNull()`——`operator bool` 是 `IsValid()`（资产是否已加载），
	// 编辑器重启后骨架不在内存里会把"配了的"误判成没配。
	if (Skeleton.IsNull() || SkeletalMesh.IsNull() || Sources.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[AnimSet] %s: 请先配 Skeleton / SkeletalMesh / Sources"), *GetName());
		return;
	}
	if (Meshes.Num() == 0)
	{
		Meshes.AddDefaulted();
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
	//    语义统一：**字段有引用且能加载 ⇒ 直接用；引用为空/失效 ⇒ 按"同目录 + 约定名"找；都没有才建**
	for (int32 MeshIndex = 0; MeshIndex < Meshes.Num(); ++MeshIndex)
	{
		FAsceVATMeshEntry& Entry = Meshes[MeshIndex];
		if (Entry.Mesh.LoadSynchronous() == nullptr)
		{
			// ⚠️ `LODIndex = -1` 用默认；将来要支持"从骨骼网格的哪个 LOD 采样"再补字段
			// （`ConvertSkeletalMeshToStaticMesh` 内部也是"同路径已存在就复用"，不会重复建）
			Entry.Mesh = UAnimToTextureBPLibrary::ConvertSkeletalMeshToStaticMesh(
				SourceMesh, FString::Printf(TEXT("%s/%s_Mesh%d"), *PackagePath, *AssetName, MeshIndex + 1), /*LODIndex=*/ -1);
		}
		if (UStaticMesh* BakedMesh = Entry.Mesh.LoadSynchronous())
		{
			// LightMap UV 冲突修复：生成的静态网格默认把 LightMap 生成到 UV1（DstLightmapIndex=1），
			// 而绑定数据（顶点→权重图 texel）正要写 UV1 ⇒ 插件 `CheckDataAsset` 会直接拒绝烘焙
			// （"Invalid StaticMesh UVChannel: 1. Already used by LightMap"）。单位走动态光，
			// 不需要 LightMap UV ⇒ 自动关掉（之前烘失败的旧网格也会被顺带修好）。
			if (BakedMesh->IsSourceModelValid(0))
			{
				const FStaticMeshSourceModel& SourceModel = BakedMesh->GetSourceModel(0);
				if (SourceModel.BuildSettings.bGenerateLightmapUVs
					&& SourceModel.BuildSettings.DstLightmapIndex == AnimBakeUVChannel)
				{
					UAnimToTextureBPLibrary::SetLightMapIndex(BakedMesh, /*LODIndex=*/0, AnimBakeUVChannel, /*bGenerateLightmapUVs=*/false);
				}
			}
		}
		if (Entry.BoneWeightTexture.LoadSynchronous() == nullptr || IsLegacyHiddenAsset(Entry.BoneWeightTexture.Get(), PackagePath))
		{
			Entry.BoneWeightTexture = FindOrCreateTextureAsset(
				PackagePath, FString::Printf(TEXT("%s_Mesh%d_Weight"), *AssetName, MeshIndex + 1));
		}
	}

	// ③ 共享动画纹理（字段有就用；空/失效/历史遗留不可见才按同目录约定名找，找不到才建；内容随后被烘焙覆写）
	if (BonePositionTexture.LoadSynchronous() == nullptr || IsLegacyHiddenAsset(BonePositionTexture.Get(), PackagePath))
	{
		BonePositionTexture = FindOrCreateTextureAsset(PackagePath, FString::Printf(TEXT("%s_BonePos"), *AssetName));
	}
	if (BoneRotationTexture.LoadSynchronous() == nullptr || IsLegacyHiddenAsset(BoneRotationTexture.Get(), PackagePath))
	{
		BoneRotationTexture = FindOrCreateTextureAsset(PackagePath, FString::Printf(TEXT("%s_BoneRot"), *AssetName));
	}
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

	// ⑤ 每个 mesh 薄烘一次（产出权重纹理；位置/旋转写进一次性纹理，避免覆写共享动画纹理），
	//    烘完立刻按同一批参数建/更新该外观的 MI（用户不用手配材质参数）
	for (int32 MeshIndex = 0; MeshIndex < Meshes.Num(); ++MeshIndex)
	{
		FAsceVATMeshEntry& Entry = Meshes[MeshIndex];

		UAnimToTextureDataAsset* Thin = NewObject<UAnimToTextureDataAsset>(GetTransientPackage());
		ApplyBakeParams(*this, *Thin);
		Thin->StaticMesh = Entry.Mesh;
		Thin->BoneWeightTexture = Entry.BoneWeightTexture;
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
			UE_LOG(LogTemp, Warning, TEXT("[AnimSet] %s: mesh%d 的权重烘焙失败（跳过 MI 参数）"), *GetName(), MeshIndex + 1);
		}
		else
		{
			// MI：`Meshes[N].Material` 已指着 MaterialInstanceConstant ⇒ 就地更新参数；
			// 没指/引用失效/历史遗留不可见 ⇒ 按"同目录 + 约定名"找；都没有才建（移动目录后不会另建）
			UMaterialInterface* ExistingMaterial = Entry.Material.LoadSynchronous();
			if (IsLegacyHiddenAsset(ExistingMaterial, PackagePath))
			{
				ExistingMaterial = nullptr;   // 旧版"目录包"遗留：CB 看不见 ⇒ 当作没有，重建到标准位置
			}
			UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(ExistingMaterial);
			if (ExistingMaterial && !MIC)
			{
				UE_LOG(LogTemp, Warning, TEXT("[AnimSet] %s: Meshes[%d].Material 不是 MaterialInstanceConstant，跳过自动套参数"),
					*GetName(), MeshIndex);
			}
			else
			{
				if (!MIC)
				{
					MIC = FindOrCreateMaterialInstance(PackagePath
						, FString::Printf(TEXT("%s_Mesh%d_MI"), *AssetName, MeshIndex + 1)
						, ResolveMaterialParent(*this));
				}
				if (MIC)
				{
					// 参数 DA：共享烘焙（骨骼数/行数/包围盒）+ 该 mesh 的权重行数 + 三类纹理；AutoPlay=false（我们手动写帧）
					// ⚠️ `NumFrames`/`SampleRate` 必须给：`UpdateMaterialInstanceFromDataAsset` 会把它们写进 MI，
					// 材质在非 AutoPlay 下靠 `NumFrames` 把帧号换算成 UV；给 0 的话会永远停在第 0 帧。
					UAnimToTextureDataAsset* MIData = NewObject<UAnimToTextureDataAsset>(GetTransientPackage());
					MIData->Mode = EAnimToTextureMode::Bone;
					MIData->UVChannel = AnimBakeUVChannel;   // 与烘焙一致（绑定信息写 UV1）
					MIData->bAutoPlay = false;          // 手动帧模式
					MIData->NumBoneInfluences = EAnimToTextureNumBoneInfluences::Four;
					MIData->NumBones = Temp->NumBones;
					MIData->BoneRowsPerFrame = Temp->BoneRowsPerFrame;
					MIData->BoneMinBBox = Temp->BoneMinBBox;
					MIData->BoneSizeBBox = Temp->BoneSizeBBox;
					MIData->BoneWeightRowsPerFrame = Thin->BoneWeightRowsPerFrame;
					MIData->BonePositionTexture = BonePositionTexture;
					MIData->BoneRotationTexture = BoneRotationTexture;
					MIData->BoneWeightTexture = Entry.BoneWeightTexture;
					MIData->NumFrames = Temp->NumFrames;                        // 图集总帧数（材质算 UV 用）
					MIData->SampleRate = FMath::Max(FrameRate, 1.f);            // 与烘焙一致

					UAnimToTextureBPLibrary::UpdateMaterialInstanceFromDataAsset(MIData, MIC);
					MIC->MarkPackageDirty();
					MIData->MarkAsGarbage();

					Entry.Material = MIC;
				}
				else
				{
					UE_LOG(LogTemp, Error,
						TEXT("[AnimSet] %s: 建 MI 失败（MaterialParent 与插件默认材质都加载不到）"), *GetName());
				}
			}
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
	if (Skeleton.IsNull())
	{
		ReportError(TEXT("没配 Skeleton"));
	}
	if (SkeletalMesh.IsNull())
	{
		ReportError(TEXT("没配 SkeletalMesh"));
	}
	else if (!Skeleton.IsNull())
	{
		if (const USkeletalMesh* SourceMesh = SkeletalMesh.LoadSynchronous())
		{
			if (SourceMesh->GetSkeleton() != Skeleton.LoadSynchronous())
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
