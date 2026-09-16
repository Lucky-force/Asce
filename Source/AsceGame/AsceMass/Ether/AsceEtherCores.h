// AsceEtherCores.h
#pragma once

#include "CoreMinimal.h"
#include "AsceMass/Ether/AsceEtherProcessCore.h"
#include "AsceEtherCores.generated.h"

/** 配方里的一项（BP 友好：int32）。 */
USTRUCT(BlueprintType)
struct FAsceEtherRecipeItem
{
	GENERATED_BODY()

	/** 有符号编号（族 = 去掉符号位）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether")
	int32 SignalType = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether")
	int32 AmountPerTick = 0;
};

/** 传输器：无状态；`OnInit` 把所有端口设成 `Any`（建图时由内部布线结构定向），不跑 Tick。 */
UCLASS(BlueprintType, Blueprintable)
class UAsceEtherTransporterCore : public UAsceEtherProcessCore
{
	GENERATED_BODY()

public:
	UAsceEtherTransporterCore();

	virtual void OnInit(UAsceEtherNetworkSubsystem& Net, int32 NodeId) override;
};

/**
 * 配方设施：扣 `RecipeInputs` → 产 `RecipeOutput`。
 * 端口约定：入 = `TransportType::Input`（按 `SignalType` 匹配），出 = 第一个 `Output` 端口。
 * **空配方 = 免费源**（矿机/发电机：不扣料、自由产出，仅受输出端口吞吐限制）。
 */
UCLASS(BlueprintType, Blueprintable)
class UAsceEtherFacilityCore : public UAsceEtherProcessCore
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether")
	TArray<FAsceEtherRecipeItem> RecipeInputs;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether")
	FAsceEtherRecipeItem RecipeOutput;

	virtual void OnInit(UAsceEtherNetworkSubsystem& Net, int32 NodeId) override;
	virtual void Tick(UAsceEtherNetworkSubsystem& Net, int32 NodeId) override;
};

/** 汇：每 tick 吃掉入端口（没声明入端口就吃所有端口）。 */
UCLASS(BlueprintType, Blueprintable)
class UAsceEtherSinkCore : public UAsceEtherProcessCore
{
	GENERATED_BODY()

public:
	virtual void OnInit(UAsceEtherNetworkSubsystem& Net, int32 NodeId) override;
	virtual void Tick(UAsceEtherNetworkSubsystem& Net, int32 NodeId) override;
};

/**
 * 记忆单元（Latch）。**有状态**（bInstance = true）。
 * - 写入：入端口出现"非 0 号"的料 ⇒ 消耗掉它、覆盖**编号**（信息，不守恒）；
 * - 读取：用 `0 号 ether`（能量）重铸编号 ⇒ 产出（`1 份 = abs(编号)` 份能量；不足则不产出、不累积）。
 */
UCLASS(BlueprintType, Blueprintable)
class UAsceEtherLatchCore : public UAsceEtherProcessCore
{
	GENERATED_BODY()

public:
	UAsceEtherLatchCore();

	/** 当前存储的编号（信息，非物质）。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ether|State")
	int32 StoredType = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ether|State")
	bool bHasStored = false;

	virtual void OnInit(UAsceEtherNetworkSubsystem& Net, int32 NodeId) override;
	virtual void Tick(UAsceEtherNetworkSubsystem& Net, int32 NodeId) override;
	virtual FString GetDebugString() const override;
};

/** 时钟：每 `HalfPeriodTicks` 翻转一次编号，每 tick 用能量重铸出去（= 自驱动的记忆单元）。 */
UCLASS(BlueprintType, Blueprintable)
class UAsceEtherClockCore : public UAsceEtherProcessCore
{
	GENERATED_BODY()

public:
	UAsceEtherClockCore();

	/** 输出编号（正号；会自己翻成互补态）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether")
	int32 OutputSignalType = 1;

	/** 半周期 tick 数。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether", meta = (ClampMin = "1"))
	int32 HalfPeriodTicks = 10;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ether|State")
	int32 CurrentType = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ether|State")
	int32 Phase = 0;

	virtual void OnInit(UAsceEtherNetworkSubsystem& Net, int32 NodeId) override;
	virtual void Tick(UAsceEtherNetworkSubsystem& Net, int32 NodeId) override;
	virtual FString GetDebugString() const override;
};

/** 计数器：入端口每来 1 份"非 0 号"料算 1 个脉冲（消耗掉），累计满阈值 ⇒ 用能量重铸 1 份编号。 */
UCLASS(BlueprintType, Blueprintable)
class UAsceEtherCounterCore : public UAsceEtherProcessCore
{
	GENERATED_BODY()

public:
	UAsceEtherCounterCore();

	/** 产出的编号。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether")
	int32 OutputSignalType = 1;

	/** 每累计这么多份脉冲产 1 份。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether", meta = (ClampMin = "1"))
	int32 Threshold = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ether|State")
	int32 Progress = 0;

	virtual void OnInit(UAsceEtherNetworkSubsystem& Net, int32 NodeId) override;
	virtual void Tick(UAsceEtherNetworkSubsystem& Net, int32 NodeId) override;
	virtual FString GetDebugString() const override;
};

/**
 * 寄存器（≤8 位）。**有状态**。
 * 端口约定（slot 顺序）：`[bit0..bitN-1 (Input), bit0..bitN-1 (Output), enable (Any)]`。
 * - 使能 = enable 端口收到"正 ether"（非 0 号、非互补态）；
 * - 写 = 并行加载（有料覆盖、没料清空该位，消耗来料）；
 * - 读 = 每 tick 把有值的位用 `0 号` 能量重铸到对应输出口。
 */
UCLASS(BlueprintType, Blueprintable)
class UAsceEtherRegisterCore : public UAsceEtherProcessCore
{
	GENERATED_BODY()

public:
	UAsceEtherRegisterCore();

	/** 位宽（≤8）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether", meta = (ClampMin = "1", ClampMax = "8"))
	int32 BitCount = 8;

	/** 寄存器位（编号；0 = 空位）。**非反射**内部状态（固定 8 位，无堆分配）；显示见 `GetDebugString`。 */
	int32 StoredBits[8];

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ether|State")
	bool bHasStored = false;

	virtual void OnInit(UAsceEtherNetworkSubsystem& Net, int32 NodeId) override;
	virtual void Tick(UAsceEtherNetworkSubsystem& Net, int32 NodeId) override;
	virtual FString GetDebugString() const override;
};

/** 湮灭发电机：一组同族异号（+X 与 −X）⇒ `abs(X)` 份 `0 号 ether`（能量的唯一来源）。 */
UCLASS(BlueprintType, Blueprintable)
class UAsceEtherAnnihilatorCore : public UAsceEtherProcessCore
{
	GENERATED_BODY()

public:
	UAsceEtherAnnihilatorCore();

	virtual void OnInit(UAsceEtherNetworkSubsystem& Net, int32 NodeId) override;
	virtual void Tick(UAsceEtherNetworkSubsystem& Net, int32 NodeId) override;
};
