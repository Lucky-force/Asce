// AsceSimulationSubsystem.h
#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "AsceTickContext.h"
#include "AsceSimulationSubsystem.generated.h"

class AAsceWorldMapReplicatorActor;

/**
 * Asce 模拟调度器：把各 sim 系统的"每帧实现"从它们自己的 Tick 里挪出来，在这里**按固定顺序连续**执行。
 *
 * 目的：
 *  - 不再依赖 UE 的 tick group / priority / 注册顺序；
 *  - 阶段之间没有别的游戏代码插入 ⇒ "结构变更 → 空间数据 → 消费者"在**同一轮内**是硬顺序；
 *  - 命令只在**唯一 drain 点**被消费（见下），于是不需要版本号/脏标记/延后一帧之类的兜底。
 *
 * 阶段顺序（新增阶段只能往这条链上插）：
 *  0. 建筑引导：`UAsceBuildingSubsystem::RunBootstrapStage`（预摆建筑落地，一次性）；
 *  1. 流场/寻路：`UAscePathFindingSubsystem::RunFlowFieldStage`；
 *  2. 空间数据：`UAsceSpatialGridSubsystem::RunGridStages`（单位层 + 建筑层；processor 内部并行）；
 *  3. 移动管线：`UAsceMassMovementSimulationSubsystem::RunMovementStage`；
 *  4. 视野/雾逻辑：`UAsceFogSubsystem::RunFogLogicStage`；
 *  5. 命令 drain：`UAsceEtherNetworkSubsystem::RunCommandDrainStage`（**唯一入口**）→ `RunSimulationStage`；
 *  6. 雾渲染上传：`UAsceFogRenderSubsystem::RunFogRenderStage`。
 *
 * 跨系统数据（各阶段系统指针、MassEntity 子系统/实体管理器、本帧时间/帧号/权威端）统一放在
 * `TickContext` 里，每帧准备好后**按引用**传给各阶段 ⇒ 各子系统不需要自己存/查别的子系统。
 */
UCLASS(MinimalAPI)
class UAsceSimulationSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;

	virtual TStatId GetStatId() const override;

	static UAsceSimulationSubsystem* Get(const UObject* WorldContextObject);

	/** 本帧的共享上下文（只在该世界的 Tick 期间有效）。 */
	const FAsceTickContext& GetTickContext() const { return TickContext; }

	/** 阶段 -1（帧首、阶段 2 之前）：销毁"请求销毁 且 bubble 引用计数归 0"的实体（仅权威端）。 */
	void RunDestroyStage();

	// ── 世界初始化（loading 依赖它）──

	/** 关卡里的世界数据源（`AAsceWorldMapReplicatorActor`）在 BeginPlay 注册；**注册即尝试初始化**（不依赖 Tick）。 */
	void RegisterWorldDataSource(AAsceWorldMapReplicatorActor* Source);
	void UnregisterWorldDataSource(AAsceWorldMapReplicatorActor* Source);

	/** 世界数据初始化是否完成（loading 系统据此决定能不能结束 loading / 收 loading 界面）。 */
	bool IsWorldInitialized() const { return bWorldInitialized; }

	/** loading（Experience）结束：之后才允许跑 sim 阶段。完整工程（AsceGame）在
	 *  `UAsceExperienceManagerComponent` 走到 Loaded 时调这一句；没有这条通知也可靠 Tick 里的
	 *  每帧复查放行（无 Experience 组件的测试地图直接视为已加载）。 */
	void NotifyLoadingFinished();

private:
	/** **幂等**：数据源就绪就跑 `AAsceWorldMapReplicatorActor::InitializeMapDimensions()`（世界数据 + 墙簇 +
	 *  路径地图注册 + 雾高度场 + 两个网格构建）；成功置 `bWorldInitialized`。
	 *  没数据源就返回（fail-closed：模拟不启动、loading 界面不消失，>2s 报一次错）。 */
	void TryRunWorldInit();

	TWeakObjectPtr<AAsceWorldMapReplicatorActor> WorldDataSource;
	bool bWorldInitialized = false;
	/** loading 门控：默认**关**，Experience Loaded 后（`NotifyLoadingFinished` 或 Tick 复查）才开。 */
	bool bLoadingFinished = false;
	bool bWarnedNoDataSource = false;

	FAsceTickContext TickContext;
};
