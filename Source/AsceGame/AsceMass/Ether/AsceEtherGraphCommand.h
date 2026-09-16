// AsceEtherGraphCommand.h
#pragma once

#include "CoreMinimal.h"
#include "Mass/EntityHandle.h"
#include "AsceMass/Ether/AsceEtherTypes.h"
#include "AsceMass/Ether/AsceEtherBuildingConfig.h"
#include "AsceEtherGraphCommand.generated.h"

/**
 * 端口身份：**建筑默认朝向下的相对几何**（移动/旋转/重排 slot 都不改变身份）。
 */
USTRUCT(BlueprintType)
struct FAsceEtherSlotKey
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether")
	uint8 Direction = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether")
	uint8 Offset = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether")
	uint8 Dimension = 0;

	bool operator==(const FAsceEtherSlotKey& Other) const
	{
		return Direction == Other.Direction && Offset == Other.Offset && Dimension == Other.Dimension;
	}
};

/** 端口实例字段**补丁**（掩码：只改置位的字段；以后端口加属性就加一个位+字段，命令不变）。 */
USTRUCT(BlueprintType)
struct FAsceEtherPortPatch
{
	GENERATED_BODY()

	enum : uint8
	{
		MaskPortTypeId = 1 << 0,
		MaskDimension  = 1 << 1,
		MaskSplitMode  = 1 << 2,
	};

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether")
	uint8 Mask = 0;

	/** 端口类型（端口类型表的行名）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether")
	FName PortTypeId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether")
	uint8 Dimension = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether")
	EAsceEtherSplitMode SplitMode = EAsceEtherSplitMode::Ordered;
};

/** 节点补丁（换建筑类型 / 移动旋转；目前 `MaskPlacement` 仅触发重建，改 footprint 待接）。 */
USTRUCT(BlueprintType)
struct FAsceEtherNodePatch
{
	GENERATED_BODY()

	enum : uint8
	{
		MaskBuildingId = 1 << 0,
		MaskPlacement  = 1 << 1,
	};

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether")
	uint8 Mask = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether")
	FName BuildingId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether")
	FIntPoint Origin = FIntPoint::ZeroValue;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether")
	uint8 Rotation = 0;
};

// ─────────────────────────────────────────────────────────────
// 命令集（**设施无关**的最小集；新增设施/器件都不需要加命令）
// ─────────────────────────────────────────────────────────────

/** 新增建筑（含 core 实例创建 + `OnInit`）。 */
USTRUCT(BlueprintType)
struct FAsceEtherCmd_AddNode
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether") FMassEntityHandle Entity;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether") FName BuildingId;
};

/** 拆除建筑（回收端口、撤销它的边/布线/段引用）。 */
USTRUCT(BlueprintType)
struct FAsceEtherCmd_RemoveNode
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether") FMassEntityHandle Entity;
};

/** 改建筑：换类型 / 移动 / 旋转。 */
USTRUCT(BlueprintType)
struct FAsceEtherCmd_PatchNode
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether") FMassEntityHandle Entity;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether") FAsceEtherNodePatch Patch;
};

/** 改建筑内部布线（实例数组整体替换）。 */
USTRUCT(BlueprintType)
struct FAsceEtherCmd_SetNodeLinks
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether") FMassEntityHandle Entity;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether") TArray<FAsceEtherLinkDef> Links;
};

/** 改某个端口实例的若干字段（按相对几何身份定位）。 */
USTRUCT(BlueprintType)
struct FAsceEtherCmd_PatchPort
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether") FMassEntityHandle Entity;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether") FAsceEtherSlotKey Slot;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether") FAsceEtherPortPatch Patch;
};

/** 表/数据内容变化 ⇒ 重解析端口类型缓存（`RowStruct = nullptr` 表示全部）。 */
USTRUCT(BlueprintType)
struct FAsceEtherCmd_RefreshData
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether") TObjectPtr<UScriptStruct> RowStruct;
};

/** 兜底 / 批量（存档加载、校验失败、暂未局部化的命令）。 */
USTRUCT(BlueprintType)
struct FAsceEtherCmd_FullRebuild
{
	GENERATED_BODY()
};
