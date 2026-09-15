// AsceEtherBuildingData.h
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AsceEtherTypes.h"
#include "AsceEtherBuildingData.generated.h"

/** 端口类型：决定该端口的每 tick 吞吐（流量上限）。可自由给端口选不同类型。 */
UCLASS(BlueprintType)
class UAsceEtherPortType : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Ether")
	FName TypeId;

	/** 每 tick 吞吐（也是该端口缓冲上限）。 */
	UPROPERTY(EditAnywhere, Category = "Ether")
	uint16 ThroughputPerTick = 1;
};

/** 端口定义（相对建筑原点的格偏移 + 方向 + 维度 + 类型）。 */
USTRUCT(BlueprintType)
struct FAsceEtherPortDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Ether")
	int32 CellOffsetX = 0;

	UPROPERTY(EditAnywhere, Category = "Ether")
	int32 CellOffsetY = 0;

	/** AsceEther::EDir */
	UPROPERTY(EditAnywhere, Category = "Ether")
	uint8 Dir = 0;

	/** 端口所在维度（须 < 建筑的 SupportedDimCount）。 */
	UPROPERTY(EditAnywhere, Category = "Ether")
	uint8 Dim = 0;

	/** 端口类型（决定每 tick 吞吐）。 */
	UPROPERTY(EditAnywhere, Category = "Ether")
	TObjectPtr<UAsceEtherPortType> Type;

	/** 组合 op（默认 Wire = 直通/路由）。 */
	UPROPERTY(EditAnywhere, Category = "Ether")
	EAsceEtherPortOp Op = EAsceEtherPortOp::Wire;

	/** 该端口作为分流源时的分配策略。 */
	UPROPERTY(EditAnywhere, Category = "Ether")
	EAsceEtherSplitMode SplitMode = EAsceEtherSplitMode::Ordered;
};

/** 内部布线的一项：端口 FromPort -> 端口 ToPort（同一建筑内的端口下标；不允许自连）。 */
USTRUCT(BlueprintType)
struct FAsceEtherLinkDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Ether")
	int32 FromPort = 0;

	UPROPERTY(EditAnywhere, Category = "Ether")
	int32 ToPort = 0;
};

/** 配方里的一项：某 SignalType 每 tick 的数量。 */
USTRUCT(BlueprintType)
struct FAsceEtherRecipeItem
{
	GENERATED_BODY()

	/** 有符号：最高位 = 互补标志（族 = 去掉符号位）。 */
	UPROPERTY(EditAnywhere, Category = "Ether")
	int16 SignalType = 0;

	UPROPERTY(EditAnywhere, Category = "Ether")
	uint16 AmountPerTick = 0;
};

/**
 * 建筑类型的 ether 静态数据（一份建筑类型一个资产）。
 * - bIsDevice：false = 传输器（纯组合，入/出由内部布线决定）；true = 器件（时序单元）；
 * - 器件用 InputPorts/OutputPorts 角色表描述端口职责；传输器只靠 InternalLinks；
 * - 每个端口自由选类型（每 tick 吞吐）。
 */
UCLASS(BlueprintType)
class UAsceEtherBuildingData : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 是否是器件（时序单元）。false = 传输器（纯组合）。 */
	UPROPERTY(EditAnywhere, Category = "Ether")
	bool bIsDevice = false;

	/** 器件 op（bIsDevice = true 时有效）。 */
	UPROPERTY(EditAnywhere, Category = "Ether")
	EAsceEtherDeviceOp DeviceOp = EAsceEtherDeviceOp::Facility;

	/** 建筑占格（与 footprint 一致），端口偏移基于建筑原点格。 */
	UPROPERTY(EditAnywhere, Category = "Ether")
	int32 SizeX = 1;

	UPROPERTY(EditAnywhere, Category = "Ether")
	int32 SizeY = 1;

	/** 支持挂载端口的维度数量（端口 Dim 须 < 它）。 */
	UPROPERTY(EditAnywhere, Category = "Ether", meta = (ClampMin = "1", ClampMax = "8"))
	uint8 SupportedDimCount = 1;

	/** 端口集合（传输器多个；器件按需）。 */
	UPROPERTY(EditAnywhere, Category = "Ether")
	TArray<FAsceEtherPortDef> Ports;

	/** 内部布线：同建筑端口之间（可任意连、可合流/分流；不允许自连）。传输器用。 */
	UPROPERTY(EditAnywhere, Category = "Ether")
	TArray<FAsceEtherLinkDef> InternalLinks;

	/** 器件端口角色表：输入端口下标（bIsDevice = true 时有效；传输器由布线决定）。 */
	UPROPERTY(EditAnywhere, Category = "Ether")
	TArray<int32> InputPorts;

	/** 器件端口角色表：输出端口下标。 */
	UPROPERTY(EditAnywhere, Category = "Ether")
	TArray<int32> OutputPorts;

	/** 器件参数：编号（`Clock` = 输出的编号；`Counter` = 产出的编号）。 */
	UPROPERTY(EditAnywhere, Category = "Ether")
	int16 DeviceParamSignal = 1;

	/** 器件参数 A（`Clock` = 半周期 tick 数；`Counter` = 每累计这么多份脉冲产 1 份）。 */
	UPROPERTY(EditAnywhere, Category = "Ether")
	uint16 DeviceParamA = 1;

	/** 配方输入（通用 + 特殊）。生产者用。 */
	UPROPERTY(EditAnywhere, Category = "Ether")
	TArray<FAsceEtherRecipeItem> Inputs;

	/** 配方输出。生产者用。 */
	UPROPERTY(EditAnywhere, Category = "Ether")
	FAsceEtherRecipeItem Output;
};
