// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "AsceAnimDef.generated.h"

class USkeleton;
class USkeletalMesh;
class UStaticMesh;
class UMaterialInterface;
class UAnimSequence;
class UTexture2D;

/**
 * 一段烘焙好的动画（Bone 模式）。
 *
 * 帧语义与 AnimToTexture 一致：`StartFrame/EndFrame` 是**烘焙贴图内的绝对帧**（直接从插件的
 * AnimToTexture DataAsset 的 `Animations[]` 条目抄）；
 * **是否循环由播放方按动作状态决定**（Idle/Move/Channel 循环、Attack/Casting/Dying 一次性，
 * 见 `AsceCombatVisualProcessor`）⇒ 不进本数据；播放速度也由播放方给（服务器 PlayRate）。
 * `AnimIndex` 只作元数据/调试（播放本身只用绝对帧区间）。
 */
USTRUCT(BlueprintType)
struct FAsceAnimClip
{
	GENERATED_BODY()

	/** 烘焙里的动画序号（AnimToTexture DataAsset 的 `Animations` 数组下标；仅元数据/调试用） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 AnimIndex = 0;

	/** 段起始帧（烘焙贴图内的**绝对帧**） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 StartFrame = 0;

	/** 段结束帧（烘焙贴图内的**绝对帧**） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 EndFrame = 0;
};

/**
 * 每个**骨架**一份的动画集（Bone 模式：同骨架下所有 mesh 共用一份烘焙）。
 *
 * - 本资产**不懂游戏语义**：`Clips` 只是"动画资产 → 烘焙帧段"的库（纯动画库）；
 * - 语义（哪个状态播哪条动画）放在**单位 def**（Idle/Move/Attack/Dying）与**技能表**
 *   （`CastConfig` 的 Cast/Channel 动画），由 `AsceCombatVisualProcessor` 解析；没配就不播；
 * - 单位类型到本资产的映射：单位 def 表的 `AnimSet` 字段（客户端通过复制的 `UniqueTypeId` 查表解析）。
 */
/** VAT 模式（v1 只支持 Bone；枚举定义在我们这里，避免游戏模块依赖插件类型） */
UENUM(BlueprintType)
enum class EAsceVATMode : uint8
{
	Bone   UMETA(DisplayName = "Bone（骨骼矩阵烘焙，同骨架共享）"),
	Vertex UMETA(DisplayName = "Vertex（逐顶点烘焙，per-mesh）"),
};

/** 烘焙精度 */
UENUM(BlueprintType)
enum class EAsceVATPrecision : uint8
{
	EightBits   UMETA(DisplayName = "8bit"),
	SixteenBits UMETA(DisplayName = "16bit"),
};

/** 图集的一页布局（不分页就只有 1 条；材质参数要用） */
USTRUCT(BlueprintType)
struct FAsceAnimPageInfo
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 NumFrames = 0;

	/** 每帧占的行数（= ceil(NumBones / 宽)） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 RowsPerFrame = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 SizeX = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 SizeY = 0;
};

/** 一个外观（mesh）的烘焙产物：插件 DA 只有一个 mesh 的这两个字段，我们扩成数组 */
USTRUCT(BlueprintType)
struct FAsceVATMeshEntry
{
	GENERATED_BODY()

	/** 烘焙输出的静态网格（**含绑定 UV**，实例化用它；与权重纹理必须来自同一次烘焙） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSoftObjectPtr<UStaticMesh> Mesh;

	/** 该 mesh 的蒙皮权重纹理（per-mesh；材质采样用） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSoftObjectPtr<UTexture2D> BoneWeightTexture;

	/** 该 mesh 用的材质实例（其动画纹理参数应指向本 AnimSet 的共享动画纹理） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSoftObjectPtr<UMaterialInterface> Material;
};

UCLASS(BlueprintType)
class UAsceAnimSet : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 本动画集对应的骨架（同骨架可共用一份烘焙） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asce 配置")
	TSoftObjectPtr<USkeleton> Skeleton;

	// （原 `BoneData`（插件 DA 引用）已移除：插件 DA 只作临时烘焙中间件，数据全部搬进本资产）

	/** 烘焙帧率（帧/秒）= 插件 DA 的 `SampleRate`；表现 processor 用它把"经过时间"换算成帧号 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asce 配置")
	float FrameRate = 30.f;

	/** 动画资产 → 烘焙帧段（键走 `FSoftObjectPath` 查找/哈希，**不加载资产**；由 `RebuildFromSources` 填） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asce 产物")
	TMap<TSoftObjectPtr<UAnimSequence>, FAsceAnimClip> Clips;

	// ──────────────────────────────────────────
	// 以下为"把插件 DA 的字段搬进来 + 扩展"（插件 DA 只作**临时烘焙中间件**，不进运行时）
	// ──────────────────────────────────────────

	// ── 烘焙参数（编辑期用；重烘时用它们 + `Sources` 填临时 DA）──
	/** 源骨骼网格（采样用） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asce 配置")
	TSoftObjectPtr<USkeletalMesh> SkeletalMesh;

	/** 烘焙**输入**：按顺序的动画序列（约定：一个动作 = 一条独立资产，**clip 名 = 资产名**，禁止用自定义帧段切段） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asce 配置")
	TArray<TSoftObjectPtr<UAnimSequence>> Sources;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asce 配置")
	EAsceVATMode Mode = EAsceVATMode::Bone;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asce 配置")
	EAsceVATPrecision Precision = EAsceVATPrecision::EightBits;

	/** 每顶点骨骼影响数（权重纹理格式 + 材质静态开关） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asce 配置")
	int32 NumBoneInfluences = 4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asce 配置")
	int32 MaxWidth = 4096;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asce 配置")
	int32 MaxHeight = 4096;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asce 配置")
	bool bEnforcePowerOfTwo = false;

	// ── 烘焙**产物**（**不用配**，重烘自动生成，只读查看）：共享（Bone 模式：同骨架所有 mesh 共用一份）──
	/** 动画数据：骨骼位置图集（分页时改数组） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asce 产物")
	TSoftObjectPtr<UTexture2D> BonePositionTexture;

	/** 动画数据：骨骼旋转图集 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asce 产物")
	TSoftObjectPtr<UTexture2D> BoneRotationTexture;

	/** 图集布局（不分页 1 条） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asce 产物")
	TArray<FAsceAnimPageInfo> Pages;

	/** 生成 MI 用的**父材质**（空 = 用插件自带的 `M_Body_BoneAnimation`）。
	 *  重烘时会为每个外观建/更新 `<名>_MeshN_MI` 并写回 `Meshes[N].Material`，参数全自动（纹理/骨骼数/行数/包围盒/AutoPlay=false…） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asce 配置")
	TSoftObjectPtr<UMaterialInterface> MaterialParent;

	// ── 每个外观一条（**可留空**：重烘会自动补一条；`Material` 留空会自动生成 MI）──
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asce 配置")
	TArray<FAsceVATMeshEntry> Meshes;

#if WITH_EDITOR
	/**
	 * **一键重烘 + 导入**（编辑器按钮，显示在细节面板）：用 `Sources` + 上面的烘焙参数，
	 * 建一个**临时**插件 `UAnimToTextureDataAsset` → 烘焙 → 把产物（段表/图集布局/纹理引用）读回本资产 → 删临时 DA。
	 *
	 * 约定与前提（见 `技能系统设计.md` §7/§7.1）：
	 *  - `Clips` 键 = `Sources[i]` 的**资产软引用**；一个动作一条独立序列（不用自定义帧段切段）；
	 *  - 纹理资产由本工具**先建/复用**（空 `UTexture2D`）再烘焙，因为插件的 `WriteToTexture` 只往
	 *    "已存在的纹理对象"里写内容（字段为空 ⇒ 静默失败）；
	 *  - 每个 mesh 各薄烘一次（只放 1 条 1 帧序列）来产出它的 `StaticMesh` + `BoneWeightTexture`；
	 *    薄烘完按同一批参数**自动建/更新该外观的 MI**（`<名>_MeshN_MI`，父材质 = `MaterialParent` 或
	 *    插件默认 `M_Body_BoneAnimation`）写进 `Meshes[N].Material` —— 材质参数不用手配；
	 *  - `Meshes` 为空会自动补一条；`BoneWeightTexture` 为空会自动建；
	 *  - **引用优先、不重复建**：字段（网格/权重纹理/共享纹理/MI）有引用且能加载 ⇒ 直接用；
	 *    引用为空或失效 ⇒ 先按"同目录 + 约定名"找，找到就用；都没有才新建（移动目录后不会另建）；
	 *  - **幂等**：重复重烘不会产生新资产，只更新内容/参数。
	 */
	UFUNCTION(CallInEditor, Category = "Asce 配置")
	void RebuildFromSources();

	/**
	 * **校验**（编辑器按钮）：`Sources` 是否都烘进了 `Clips`、段是否越界（对比图集帧数）、
	 * `Meshes[]` 条目是否完整、`SkeletalMesh`/各 mesh 的骨架是否与资产一致。
	 * `RebuildFromSources` 末尾会自动跑一次。
	 */
	UFUNCTION(CallInEditor, Category = "Asce 配置")
	void ValidateAnimSet();
#endif
};
