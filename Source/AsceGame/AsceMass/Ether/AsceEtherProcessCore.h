// AsceEtherProcessCore.h
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "AsceEtherProcessCore.generated.h"

class UAsceEtherNetworkSubsystem;

/**
 * 器件**行为**（时序单元功能）。子类用蓝图 / C++ 继承实现；
 * 对应建筑在生成 / 重建时调用一次 `OnInit`，时序器件每 tick 调用 `Tick`。
 *
 * - `bInstance = false`（默认）：**不实例化**，共用 CDO 只跑函数 —— 无状态 core（传输器、纯算子）；
 * - `bInstance = true`：Subsystem 为每个建筑 `NewObject` 一个实例，core 可把**自己的状态**放成员变量
 *   （`Latch` / `Clock` / `Counter` / `Register` 这类有记忆的器件就该是 true）。
 *
 * 约定：core 通过 `UAsceEtherNetworkSubsystem` 的端口访问接口读写**自己建筑**的端口缓冲；
 * `TransportType` 只是元数据（建图定向那一次 + 自校验 + 表现层），tick 推流不看它。
 *
 * **并行契约（重要）**：`Tick` 会被**多线程并行**调用（按器件分片）。因此：
 *  - `bInstance == false` 的 core（共享 CDO）**禁止写任何成员/共享状态**，只能读写"自己建筑的端口"和只读成员；
 *  - 需要状态就 `bInstance = true`（每建筑一个实例，天然线程安全）；
 *  - 不要在 `Tick` 里改网络结构（注册/注销/改布线都会标脏，应在主线程做）。
 */
UCLASS(BlueprintType, Blueprintable, Abstract)
class UAsceEtherProcessCore : public UObject
{
	GENERATED_BODY()

public:
	/** true = 给每个建筑建实例（可带状态）；false = 共用 CDO 仅跑函数。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ether")
	bool bInstance = false;

	/** 建筑生成 / 重建时调用一次：可在这里设置自己端口的 TransportType / SplitMode 等默认值。
	 *  **注意**：重建会再次调用；不要在这里重置自己的运行状态（状态活到建筑销毁）。 */
	virtual void OnInit(UAsceEtherNetworkSubsystem& Net, int32 NodeId);

	/** 每 tick（仅时序器件）：读入端口、更新自己的状态、写出端口。 */
	virtual void Tick(UAsceEtherNetworkSubsystem& Net, int32 NodeId);

	/** 调试显示（可选）。 */
	virtual FString GetDebugString() const;
};
