#include "AsceRTSUnitTrait.h"

#include "MassEntityTemplateRegistry.h"
#include "MassCommonFragments.h"
#include "MassMovementFragments.h"
#include "MassReplicationFragments.h"
#include "../Fragments/AsceCoreFragments.h"
#include "../Fragments/AsceOrderFragments.h"
#include "../Fragments/AsceInteractionFragments.h"
#include "../Fragments/AsceMovementFragments.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceRTSUnitTrait)

void UAsceRTSUnitTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	// 内置 Fragment：变换、速度
	BuildContext.AddFragment<FTransformFragment>();
	BuildContext.AddFragment<FMassVelocityFragment>();
	BuildContext.AddFragment<FMassForceFragment>();
	BuildContext.AddFragment<FMassDesiredMovementFragment>();
	
	// 内置 Tag：启用代码驱动移动（DesiredVelocity → Velocity）
	BuildContext.AddTag<FMassCodeDrivenMovementTag>();

	// 内置共享参数：移动速度/加速度
	FMassMovementParameters MovementParams;
	MovementParams.MaxSpeed = MaxSpeed;
	MovementParams.MaxAcceleration = MaxAcceleration;
	MovementParams.DefaultDesiredSpeed = DefaultDesiredSpeed;
	BuildContext.AddConstSharedFragment(FConstSharedStruct::Make(MovementParams));

	FAgentRadiusFragment& RadiusFragment = BuildContext.AddFragment_GetRef<FAgentRadiusFragment>();
	RadiusFragment.Radius = Radius;
	FAsceHeightFragment& HeightFragment = BuildContext.AddFragment_GetRef<FAsceHeightFragment>();
	HeightFragment.Height = Height;

	// 通用属性：血量、阵营
	FAsceHealthFragment& HealthFragment = BuildContext.AddFragment_GetRef<FAsceHealthFragment>();
	HealthFragment.MaxHealth = MaxHealth;
	HealthFragment.CurrentHealth = MaxHealth;
	BuildContext.AddFragment_GetRef<FAsceTeamFragment>().TeamID = TeamID;

	// 联机同步用的NetID（服务器权威分配，见AsceNetIDRegistrySubsystem）
	BuildContext.AddFragment<FMassNetworkIDFragment>();

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

	// 自定义 Tag：初始存活
	BuildContext.AddTag<FAsceAliveTag>();
}
