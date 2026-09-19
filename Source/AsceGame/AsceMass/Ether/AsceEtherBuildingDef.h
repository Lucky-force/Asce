// AsceEtherBuildingDef.h
#pragma once

#include "CoreMinimal.h"
#include "AsceMass/Building/AsceBuildingDef.h"
#include "AsceMass/Ether/AsceEtherTypes.h"
#include "DataTableSystem/AsceRowNameWrapper.h"
#include "AsceEtherBuildingDef.generated.h"

class UAsceEtherProcessCore;

/** 内部布线的一项（建筑内端口下标 = slot 下标；不允许自连）。 */
USTRUCT(BlueprintType)
struct FAsceEtherLinkDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether")
	int32 FromPort = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether")
	int32 ToPort = 0;
};

/** 建筑上可挂端口的**槽位**：初始化默认端口（摆放 + 维度 + 型号 + 分流策略默认值）。 */
USTRUCT(BlueprintType)
struct FAsceEtherPortSlot
{
	GENERATED_BODY()

	/** AsceEther::EDir（0=Left,1=Up,2=Right,3=Down；建筑默认朝向）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether")
	uint8 Direction = 0;

	/** 该方向边上、从左手侧起顺时针第几格（0 起）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether")
	uint8 Offset = 0;

	/** 该端口占用的维度（Dimension < SupportedDimCount）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether")
	uint8 Dimension = 0;

	/** 端口类型：**端口类型表（MDT）的行名**（编辑器下拉选行）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether", meta = (DataTableBinding = "AsceEtherPortTypeRow"))
	FAsceRowNameWrapper PortType;

	/** 分流策略默认值（玩家运行时可改）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether")
	EAsceEtherSplitMode SplitMode = EAsceEtherSplitMode::Ordered;
};

/**
 * **Ether 建筑**的类型定义（`FAsceEntityDefRow::Def` 的一种；来自实体定义表）。
 *
 * - 建筑通用字段（占地/遮挡/CoreClass）在基类 `FAsceBuildingDefBase`；
 * - 本结构只放 Ether 专属：器件/传输器、行为 process core、端口槽位、默认内布线。
 */
USTRUCT(BlueprintType)
struct FAsceEtherBuildingDef : public FAsceBuildingDefBase
{
	GENERATED_BODY()

	/** 组合（传输器）还是时序（器件）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether")
	EAsceEtherDeviceType DeviceType = EAsceEtherDeviceType::Combinational;

	/** 行为类：时序器件每 tick 调用它的 Tick；组合建筑只调用 OnInit（传输器 = UAsceEtherTransporterCore）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether")
	TSoftClassPtr<UAsceEtherProcessCore> ProcessCoreClass;

	/** 支持挂载端口的维度数量（端口 Dimension 须 < 它）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether", meta = (ClampMin = "1", ClampMax = "8"))
	uint8 SupportedDimCount = 1;

	/** 端口槽位清单（数量不写死）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether")
	TArray<FAsceEtherPortSlot> PortSlots;

	/** 内部布线**默认值**（实例数据，玩家可改；传输器用）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether")
	TArray<FAsceEtherLinkDef> DefaultInternalLinks;
};
