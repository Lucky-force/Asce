// AsceSimulationSubsystem.cpp
#include "AsceSimulationSubsystem.h"

#include "AsceTickContext.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "MassEntitySubsystem.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "GameModes/AsceExperienceManagerComponent.h"
#include "AsceMass/SpatialGrid/AsceSpatialGridSubsystem.h"
#include "AsceMass/Building/AsceBuildingSubsystem.h"
#include "AsceMass/Ether/AsceEtherNetworkSubsystem.h"
#include "AsceMass/FlowField/AsceWorldMapReplicatorActor.h"
#include "AsceMass/FlowField/AscePathFindingSubsystem.h"
#include "AsceMass/Movement/AsceMassMovementSimulationSubsystem.h"
#include "AsceMass/Vision/AsceFogSubsystem.h"
#include "AsceMass/Vision/AsceFogRenderSubsystem.h"
#include "AsceMass/UI/AsceFragmentSamplerSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceSimulationSubsystem)

bool UAsceSimulationSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	UWorld* World = Cast<UWorld>(Outer);
	if (!World)
	{
		return false;
	}
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	return World->IsGameWorld() || World->IsPlayInEditor();
}

void UAsceSimulationSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// 阶段系统指针解析一次（世界子系统此时都已创建；缺的保持 null ⇒ 该阶段跳过）
	TickContext.Buildings = InWorld.GetSubsystem<UAsceBuildingSubsystem>();
	TickContext.PathFinding = InWorld.GetSubsystem<UAscePathFindingSubsystem>();
	TickContext.Grid = InWorld.GetSubsystem<UAsceSpatialGridSubsystem>();
	TickContext.Movement = InWorld.GetSubsystem<UAsceMassMovementSimulationSubsystem>();
	TickContext.Fog = InWorld.GetSubsystem<UAsceFogSubsystem>();
	TickContext.Ether = InWorld.GetSubsystem<UAsceEtherNetworkSubsystem>();
	TickContext.FogRender = InWorld.GetSubsystem<UAsceFogRenderSubsystem>();
}

TStatId UAsceSimulationSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UAsceSimulationSubsystem, STATGROUP_Tickables);
}

UAsceSimulationSubsystem* UAsceSimulationSubsystem::Get(const UObject* WorldContextObject)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	return World ? World->GetSubsystem<UAsceSimulationSubsystem>() : nullptr;
}

// ── 世界初始化（loading 依赖它）──

void UAsceSimulationSubsystem::RegisterWorldDataSource(AAsceWorldMapReplicatorActor* Source)
{
	WorldDataSource = Source;
	TryRunWorldInit();   // 注册即初始化：不依赖 Tick（loading 期间世界可能不 tick）
}

void UAsceSimulationSubsystem::UnregisterWorldDataSource(AAsceWorldMapReplicatorActor* Source)
{
	if (WorldDataSource.Get() == Source)
	{
		WorldDataSource = nullptr;
	}
}

void UAsceSimulationSubsystem::NotifyLoadingFinished()
{
	bLoadingFinished = true;
}

void UAsceSimulationSubsystem::TryRunWorldInit()
{
	if (bWorldInitialized)
	{
		return;
	}

	AAsceWorldMapReplicatorActor* Source = WorldDataSource.Get();
	if (!Source)
	{
		// fail-closed：等数据源注册；缺了就一直等（loading 界面也不会消失），2 秒后报一次错免得是配置问题还不知道
		const UWorld* World = GetWorld();
		if (!bWarnedNoDataSource && World && World->GetTimeSeconds() > 2.0)
		{
			bWarnedNoDataSource = true;
			UE_LOG(LogTemp, Error, TEXT("[AsceSim] 关卡里没有 AAsceWorldMapReplicatorActor（或它没注册）：世界数据无法初始化，模拟不会启动"));
		}
		return;
	}

	Source->InitializeMapDimensions();
	bWorldInitialized = true;

	UE_LOG(LogTemp, Log, TEXT("[AsceSim] 世界数据初始化完成（sim 阶段链开始运行）"));
}

void UAsceSimulationSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 世界数据没初始化完 / loading 没结束：不跑任何 sim 阶段
	// （`TryRunWorldInit` 是幂等的；这里再兜一次是为了"数据源晚注册"的情况，比如流式关卡）
	if (!bWorldInitialized)
	{
		TryRunWorldInit();
		return;
	}
	// loading 门控：Experience（如果这个地图有）Loaded 之前不跑阶段链；无组件的测试地图直接放行。
	// 每帧复查是为了兜住"先 Tick 后 Loaded"的时序（Experience 组件是 GameState 的默认子对象，GameState 一到就查得到）；
	// 放行后上面那个 `bLoadingFinished` 短路，只剩一次 bool 判断。
	if (!bLoadingFinished)
	{
		const AGameStateBase* GameState = World->GetGameState();
		const UAsceExperienceManagerComponent* Experience = GameState
			? GameState->FindComponentByClass<UAsceExperienceManagerComponent>()
			: nullptr;
		if (Experience && !Experience->IsExperienceLoaded())
		{
			return;
		}
		bLoadingFinished = true;
	}

	// ── 每帧刷新上下文（各阶段都从这里取"跨系统数据"，不再各自查子系统表/存指针）──
	TickContext.World = World;
	TickContext.DeltaTime = DeltaTime;
	TickContext.Frame = GFrameCounter;
	TickContext.bAuthority = World->GetNetMode() != NM_Client;
	if (!TickContext.Mass)
	{
		TickContext.Mass = World->GetSubsystem<UMassEntitySubsystem>();
	}
	TickContext.EntityManager = TickContext.Mass ? &TickContext.Mass->GetMutableEntityManager() : nullptr;

	if (!TickContext.Buildings && !TickContext.PathFinding && !TickContext.Grid && !TickContext.Movement
		&& !TickContext.Fog && !TickContext.Ether && !TickContext.FogRender)
	{
		return;
	}

	// 前置（不在本阶段链里）：结构变化（footprint 增删/搬家、Ether 注册）由 TG_PrePhysics 的 Mass observer 处理，
	// 早于本调度器；它们 push 的命令会在阶段 5 的唯一 drain 点被消费（此时建筑层已是本轮最新）。

	// 阶段 0：建筑引导（一次性；就绪后把关卡里预摆的建筑落地成实体，然后销毁摆放 Actor）
	if (TickContext.Buildings)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(AsceSim_BuildingBootstrap);
		TickContext.Buildings->RunBootstrapStage(TickContext);
	}

	// 阶段 0b：建造推进（工期到期 ⇒ 补 footprint/Ether 标记 ⇒ 观察者随之生效）
	if (TickContext.Buildings)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(AsceSim_BuildingConstruction);
		TickContext.Buildings->RunConstructionStage(TickContext);
	}

	// 阶段 1：流场/寻路（goal-LOS 重算预算；移动管线的 steering 要读流场）
	if (TickContext.PathFinding)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(AsceSim_FlowField);
		TickContext.PathFinding->RunFlowFieldStage(TickContext);
	}

	// 阶段 2：空间数据（单位层 + 建筑层；processor 内部并行）
	//         单位层吃"当前 Transform"（= 上一帧末位置）：先建索引、移动碰撞再吃索引是标准做法
	if (TickContext.Grid)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(AsceSim_SpatialGridStages);
		TickContext.Grid->RunGridStages(TickContext);
	}

	// 阶段 3：移动管线（7 个 processor 顺序 Run；产出本帧位置/朝向/到达事件）
	if (TickContext.Movement)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(AsceSim_MovementPipeline);
		TickContext.Movement->RunMovementStage(TickContext);
	}

	// 阶段 4：视野/雾逻辑（吃本帧位置 + 本帧遮挡数据）
	if (TickContext.Fog)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(AsceSim_FogLogic);
		TickContext.Fog->RunFogLogicStage(TickContext);
	}

	// 阶段 5：Ether（命令 drain（**唯一入口**）→ 模拟）
	if (TickContext.Ether)
	{
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(AsceSim_EtherCommandDrain);
			TickContext.Ether->RunCommandDrainStage(TickContext);
		}
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(AsceSim_EtherSimulation);
			TickContext.Ether->RunSimulationStage(TickContext);
		}
	}

	// 阶段 6：雾渲染上传（吃阶段 4 的结果）
	if (TickContext.FogRender)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(AsceSim_FogRenderUpload);
		TickContext.FogRender->RunFogRenderStage(TickContext);
	}

	// 阶段 7：UI 字段采样（**最后**：本帧数据都已更新完）。
	// 只读 fragment + 对"变了的字段"广播（绑定即广播初值）；细节与不变量见 `AsceMass/UI/AsceEntityFragmentSampler.h`。
	// 命令回流：采样期间 UI 回调发出的命令只入队，下一帧的阶段 5 才 drain（不存在同帧反馈）。
	if (UAsceFragmentSamplerSubsystem* FragmentSampler = World->GetSubsystem<UAsceFragmentSamplerSubsystem>())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(AsceSim_FragmentSampler);
		FragmentSampler->RunSamplingStage();
	}
}
