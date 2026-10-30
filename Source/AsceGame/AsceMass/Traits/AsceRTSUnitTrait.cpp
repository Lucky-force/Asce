#include "AsceRTSUnitTrait.h"

#include "MassEntityTemplateRegistry.h"
#include "MassCommonFragments.h"
#include "MassMovementFragments.h"
#include "../Fragments/AsceCoreFragments.h"
#include "../Fragments/AsceOrderFragments.h"
#include "../Fragments/AsceInteractionFragments.h"
#include "../Fragments/AsceMovementFragments.h"
#include "../Vision/AsceVisionFragments.h"
#include "../Combat/AsceCombatFragments.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceRTSUnitTrait)

void UAsceRTSUnitTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	// 移动 fragment（结构）：速度/力/期望速度 + 代码驱动标记
	BuildContext.AddFragment<FMassVelocityFragment>();
	BuildContext.AddFragment<FMassForceFragment>();
	BuildContext.AddFragment<FMassDesiredMovementFragment>();
	BuildContext.AddTag<FMassCodeDrivenMovementTag>();

	FAgentRadiusFragment& RadiusFragment = BuildContext.AddFragment_GetRef<FAgentRadiusFragment>();
	RadiusFragment.Radius = Radius;
	FAsceHeightFragment& HeightFragment = BuildContext.AddFragment_GetRef<FAsceHeightFragment>();
	HeightFragment.Height = Height;

	// 按开关挂命令类Fragment
	if (bCanMove)
	{
		BuildContext.AddFragment<FAsceMoveOrderFragment>();
		// 是否要跑完整本地模拟（流场+RVO），战争迷雾接入前先占位默认true，
		// 见AsceMovementFragments.h注释。
		BuildContext.AddFragment<FAsceMovementSimStateFragment>();
		// 客户端网络位置误差校正用的"待修正目标"状态，见AsceMovementFragments.h注释。
		// Server端也挂这个Fragment只是为了让Archetype两端一致，不会被写入/读取
		// （UAsceMovementCorrectionProcessor只在Client执行）。
		BuildContext.AddFragment<FAscePositionCorrectionFragment>();
		
		// ── 视野模块（AsceMass/Vision）新增：会移动的单位都提供视野 ──
		// FAsceVisionViewFragment（视野半径/眼高，参与复制，供双端确定性视野计算）；
		// FAsceVisionFogStateFragment（本地上帧所在格，不复制，FogProcessor 换格检测用）。
		FAsceVisionViewFragment& VisionView = BuildContext.AddFragment_GetRef<FAsceVisionViewFragment>();
		VisionView.SightRadius = static_cast<uint16>(FMath::Clamp(SightRadius, 1, MAX_uint16));
		VisionView.EyeHeightCm = static_cast<uint16>(FMath::Clamp(Height, 0.f, static_cast<float>(MAX_uint16)));
		BuildContext.AddFragment<FAsceVisionFogStateFragment>();
	}
	if (bCanAttack)
	{
		// 战斗属性（走值复制通道下发，客户端要拿攻速算动画 PlayRate、拿射程画范围圈）
		// + 战斗运行时（服务器专有：指定目标 / 扫描节流）
		// 这里的值是默认值，生成时被实体定义表覆盖。见 `Combat/技能系统设计.md`
		BuildContext.AddFragment<FAsceCombatAttributesFragment>();
		BuildContext.AddFragment<FAsceCombatFragment>();
	}
	if (bIsInteractable)
	{
		BuildContext.AddFragment<FAsceInteractableFragment>();
		if (bPlayerOnlyInteraction)
		{
			BuildContext.AddTag<FAscePlayerOnlyInteractableTag>();
		}
		else
		{
			BuildContext.AddTag<FAsceGenericInteractableTag>();
		}
	}

	// 动作状态（**服务器权威**，唯一真相；客户端表现靠动作态通道复制下来的边沿）——
	// 所有单位都挂（Idle/Move 也是动作态，见 `Combat/技能系统设计.md`）
	BuildContext.AddFragment<FAsceActionFragment>();
	// 客户端表现：动作态（动作态复制通道写入；驱动转身/VAT），见 AsceCombatFragments.h
	BuildContext.AddFragment<FAsceCombatVisualFragment>();
	// 动画播放状态（表现 processor 维护；见 `Combat/AsceCombatVisualProcessor.h`）
	BuildContext.AddFragment<FAsceAnimPlaybackFragment>();
	// 技能槽位（**真实数据**：指令只发槽位号；要复制到客户端）+ 蓝量
	BuildContext.AddFragment<FAsceSkillSlotsFragment>();
	FAsceManaFragment& ManaFragment = BuildContext.AddFragment_GetRef<FAsceManaFragment>();
	ManaFragment.MaxMana = 100.f;
	ManaFragment.CurrentMana = 100.f;

	// 自定义 Tag：可移动单位（SpatialGrid "单位层"按它收录，避免以后子弹/投射物等被误收）
	BuildContext.AddTag<FAsceMovableUnitTag>();
}
