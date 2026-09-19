#include "AsceRTSUnitTrait.h"

#include "MassEntityTemplateRegistry.h"
#include "MassCommonFragments.h"
#include "MassMovementFragments.h"
#include "../Fragments/AsceCoreFragments.h"
#include "../Fragments/AsceOrderFragments.h"
#include "../Fragments/AsceInteractionFragments.h"
#include "../Fragments/AsceMovementFragments.h"
#include "../Vision/AsceVisionFragments.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceRTSUnitTrait)

void UAsceRTSUnitTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	// 移动 fragment（结构）：速度/力/期望速度 + 代码驱动标记
	BuildContext.AddFragment<FMassVelocityFragment>();
	BuildContext.AddFragment<FMassForceFragment>();
	BuildContext.AddFragment<FMassDesiredMovementFragment>();
	BuildContext.AddTag<FMassCodeDrivenMovementTag>();

	// 移动调参（per-entity）；这里的值是默认值，生成时会被实体定义表覆盖
	FAsceMovementTuningFragment& Tuning = BuildContext.AddFragment_GetRef<FAsceMovementTuningFragment>();
	Tuning.MaxSpeed = MaxSpeed;
	Tuning.MaxAcceleration = MaxAcceleration;
	Tuning.DefaultDesiredSpeed = DefaultDesiredSpeed;

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
		BuildContext.AddFragment<FAsceAttackOrderFragment>();
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

	// 自定义 Tag：可移动单位（SpatialGrid "单位层"按它收录，避免以后子弹/投射物等被误收）
	BuildContext.AddTag<FAsceMovableUnitTag>();
}
