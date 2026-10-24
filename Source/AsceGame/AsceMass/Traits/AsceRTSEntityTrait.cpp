// AsceRTSEntityTrait.cpp
#include "AsceRTSEntityTrait.h"

#include "MassCommonFragments.h"
#include "MassReplicationTrait.h"
#include "MassReplicationFragments.h"   // FMassReplicationParameters（写入调优）
#include "MassLODTypes.h"               // EMassLOD
#include "MassEntityUtils.h"            // UE::Mass::Utils::GetEntityManagerChecked（行为树共享 fragment）
#include "MassStateTreeFragments.h"     // FMassStateTreeSharedFragment / FMassStateTreeInstanceFragment
#include "StateTree.h"                  // UStateTree::LoadSynchronous / IsReadyToRun
#include "UObject/Package.h"            // GetTransientPackage
#include "AsceMass/Behavior/AsceBehaviorFragment.h"
#include "AsceMass/Data/AsceEntityDefLibrary.h"
#include "AsceMass/Networking/Core/AsceUnitReplicator.h"
#include "AsceMass/Networking/Capabilities/Transform/AsceTransformReplication.h"
#include "../Fragments/AsceCoreFragments.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceRTSEntityTrait)

namespace
{
	/** 把项目调优写进引擎复制参数（ReplicatorClass/BubbleInfoClass 两个类字段固定为项目约定） */
	void ApplyReplicationTuning(FMassReplicationParameters& Params, const FAsceReplicationTuning& Tuning)
	{
		Params.LODDistance[EMassLOD::High] = Tuning.LODDistanceHigh;
		Params.LODDistance[EMassLOD::Medium] = Tuning.LODDistanceMedium;
		Params.LODDistance[EMassLOD::Low] = Tuning.LODDistanceLow;
		Params.LODDistance[EMassLOD::Off] = Tuning.LODDistanceOff;

		Params.BufferHysteresisOnDistancePercentage = Tuning.BufferHysteresisOnDistancePercentage;

		Params.LODMaxCount[EMassLOD::High] = Tuning.LODMaxCountHigh;
		Params.LODMaxCount[EMassLOD::Medium] = Tuning.LODMaxCountMedium;
		Params.LODMaxCount[EMassLOD::Low] = Tuning.LODMaxCountLow;
		Params.LODMaxCount[EMassLOD::Off] = Tuning.LODMaxCountOff;

		Params.LODMaxCountPerViewer[EMassLOD::High] = Tuning.LODMaxCountPerViewerHigh;
		Params.LODMaxCountPerViewer[EMassLOD::Medium] = Tuning.LODMaxCountPerViewerMedium;
		Params.LODMaxCountPerViewer[EMassLOD::Low] = Tuning.LODMaxCountPerViewerLow;
		Params.LODMaxCountPerViewer[EMassLOD::Off] = Tuning.LODMaxCountPerViewerOff;

		Params.UpdateInterval[EMassLOD::High] = Tuning.UpdateIntervalHigh;
		Params.UpdateInterval[EMassLOD::Medium] = Tuning.UpdateIntervalMedium;
		Params.UpdateInterval[EMassLOD::Low] = Tuning.UpdateIntervalLow;
		Params.UpdateInterval[EMassLOD::Off] = Tuning.UpdateIntervalOff;
	}
}

void UAsceRTSEntityTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	BuildContext.AddFragment<FTransformFragment>();

	// 身份 id：行名必须是 1..65535 的数字（不查表，构建期零外部依赖）。
	// 非法/未配置**直接崩**：静默按 0 处理会让实体在 Ether 里"没有端口"/注册成错误类型，排查成本极高。
	FAsceUnitDefFragment& DefFragment = BuildContext.AddFragment_GetRef<FAsceUnitDefFragment>();
	DefFragment.UniqueTypeId = UAsceEntityDefLibrary::ParseEntityDefIdChecked(UniqueType.RowName);

	// 阵营 fragment（结构）：值由生成方按归属写入（工厂 TeamKey 参数 / 建筑 placement），这里只给默认值
	BuildContext.AddFragment<FAsceTeamFragment>();

	// 生命周期（服务端专用、不复制）：销毁请求 + bubble 引用计数。见 `AsceCoreFragments.h`
	BuildContext.AddFragment<FAsceLifecycleFragment>();

	// ── 行为树（Mass × StateTree）：**两端都挂**（客户端也跑表现 task，见 `Combat/技能编辑器设计.md` §3.3）──
	// 树来自该类型的实体定义行（`FAsceEntityDefBase::StateTree`）；空 = 该类型没有状态树（行为不变）。
	// 注意：**不能**照抄引擎 `UMassStateTreeTrait` 的 `Standalone|Server` 门控，否则两端 archetype 不一致。
	if (!BuildContext.IsInspectingData())
	{
		FAsceEntityDefRow DefRow;
		const bool bHasRow = UAsceEntityDefLibrary::GetEntityDefRowByName(UniqueType.RowName, DefRow);
		const FAsceEntityDefBase* DefBase = bHasRow ? DefRow.Def.GetPtr<FAsceEntityDefBase>() : nullptr;
		UStateTree* BehaviorTree = DefBase ? DefBase->StateTree.LoadSynchronous() : nullptr;
		if (DefBase && !DefBase->StateTree.IsNull() && BehaviorTree == nullptr)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Behavior] 实体类型 %s 配了 StateTree 但加载失败（软引用无效/资产缺失）—— 该类型不会有行为树"),
				*UniqueType.RowName.ToString());
		}
		if (BehaviorTree)
		{
			if (!BehaviorTree->IsReadyToRun())
			{
				UE_LOG(LogTemp, Warning, TEXT("[Behavior] 实体类型 %s 的 StateTree %s 还没编译（IsReadyToRun=false）—— 该类型不会有行为树"),
					*UniqueType.RowName.ToString(), *BehaviorTree->GetName());
			}
			else
			{
				FMassStateTreeSharedFragment SharedStateTree;
				SharedStateTree.StateTree = BehaviorTree;
				FMassEntityManager& EntityManager = UE::Mass::Utils::GetEntityManagerChecked(World);
				BuildContext.AddConstSharedFragment(EntityManager.GetOrCreateConstSharedFragment(SharedStateTree));
				BuildContext.AddFragment<FMassStateTreeInstanceFragment>();
				// 外层动作态 → 树事件的边沿检测（`UAsceStateTreeNotifyProcessor`）
				BuildContext.AddFragment<FAsceBehaviorFragment>();
			}
		}
	}

	if (bAddHealth)
	{
		FAsceHealthFragment& Health = BuildContext.AddFragment_GetRef<FAsceHealthFragment>();
		Health.MaxHealth = MaxHealth;
		Health.CurrentHealth = MaxHealth;
	}
	if (bAddNetworkID)
	{
		BuildContext.AddFragment<FMassNetworkIDFragment>();
	}

	BuildContext.AddTag<FAsceAliveTag>();

	// ── 复制组：**委托引擎 `UMassReplicationTrait`**（等价于手动加那个 trait，但参数由我们填）──
	// 为什么用瞬态实例代跑：`FMassReplicationParameters` / `FMassReplicationSharedFragment` 的构造函数
	// 没有导出符号（MassReplication 模块私有），跨模块直接构造会 LNK2019；而 trait 的 `BuildTemplate`
	// 是导出的，构造与共享 fragment 都在引擎模块内完成。
	// 单机（Standalone）跳过由引擎 trait 自己判断（它内部会直接 return）。
	if (bReplicateEntity)
	{
		UMassReplicationTrait* RepTrait = NewObject<UMassReplicationTrait>(GetTransientPackage(), NAME_None, RF_Transient);
		RepTrait->Params.ReplicatorClass = UAsceUnitReplicator::StaticClass();
		RepTrait->Params.BubbleInfoClass = AAsceTransformClientBubbleInfo::StaticClass();
		ApplyReplicationTuning(RepTrait->Params, ReplicationTuning);
		RepTrait->BuildTemplate(BuildContext, World);
	}
}
