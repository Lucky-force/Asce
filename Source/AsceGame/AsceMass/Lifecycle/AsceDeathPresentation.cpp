// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceMass/Lifecycle/AsceDeathPresentation.h"

#include "Engine/World.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"

#include "AsceMass/Fragments/AsceCoreFragments.h"

namespace AsceDeath
{
	void PlayLocalDeath(UWorld& World, const FMassEntityHandle Entity)
	{
		UMassEntitySubsystem* Mass = World.GetSubsystem<UMassEntitySubsystem>();
		if (!Mass)
		{
			return;
		}
		FMassEntityManager& EntityManager = Mass->GetMutableEntityManager();
		if (!EntityManager.IsEntityValid(Entity))
		{
			return;   // 本端已经没有它了（被视野裁掉 / Remove 先到）：就算了
		}

		// 置 Dying ⇒ `AsceLocalFogVisibilityProcessor` 每帧据此把表现 LOD 覆写成 Off（权威端死亡现场已置，幂等）
		if (FAsceLifecycleFragment* Lifecycle = EntityManager.GetFragmentDataPtr<FAsceLifecycleFragment>(Entity))
		{
			Lifecycle->SetDying(true);
		}

		// TODO(死亡特效)：按实体 def 生成"客户端本地死亡特效实体"（本地标记 + 本地寿命 processor，播 DyingAnim）。
		UE_LOG(LogTemp, Verbose, TEXT("[Death] 本地死亡表现 Entity=%lld"), static_cast<int64>(Entity.AsNumber()));
	}
}
