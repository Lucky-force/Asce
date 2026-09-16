// AsceEtherBuildingConfig.h
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "AsceMass/Ether/AsceEtherTypes.h"
#include "AsceEtherBuildingConfig.generated.h"

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

	/** 端口类型 id：**端口类型表（MDT）的行名**（玩家运行时可改；改完标脏重建）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether")
	FName PortTypeId;

	/** 分流策略默认值（玩家运行时可改）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether")
	EAsceEtherSplitMode SplitMode = EAsceEtherSplitMode::Ordered;
};

/**
 * 建筑**配置**（共享数据：蓝图子类 CDO，不实例化）。
 * - 只有"布局 + 挂载槽位 + 用哪个 ProcessCore"，**不含**任何实例数据；
 * - 器件 / 传输器之分看 `DeviceType` + `ProcessCoreClass`（传输器的 core 只需把所有端口设成 Any）。
 */
UCLASS(BlueprintType, Blueprintable, Abstract)
class UAsceEtherBuildingConfig : public UObject
{
	GENERATED_BODY()

public:
	/** 组合（传输器）还是时序（器件）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether")
	EAsceEtherDeviceType DeviceType = EAsceEtherDeviceType::Combinational;

	/** 行为类：时序器件每 tick 调用它的 Tick；组合建筑只调用 OnInit（传输器 = UAsceEtherTransporterCore）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether")
	TSubclassOf<UAsceEtherProcessCore> ProcessCoreClass;

	/** 建筑占格（与 footprint 一致），端口槽位基于建筑原点格。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether")
	int32 SizeX = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether")
	int32 SizeY = 1;

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
