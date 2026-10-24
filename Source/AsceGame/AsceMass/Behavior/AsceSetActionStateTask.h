// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassStateTreeTypes.h"

#include "AsceMass/Combat/AsceCombatDef.h"   // EAsceActionState（UPROPERTY 枚举需要完整定义）

#include "AsceSetActionStateTask.generated.h"

struct FAsceActionFragment;
namespace UE::MassBehavior
{
	struct FStateTreeDependencyBuilder;
}

/**
 * **由行为树写动作状态**（`Combat/技能编辑器设计.md` §10.2 的"树完成/被打断 ⇒ 系统写 fragment"）。
 *
 * 为什么需要：施法推进改成"树驱动"之后，`FAsceActionFragment` 仍必须被**正确写**（它是复制的唯一真相：
 * 动作态通道、表现 processor、客户端预测都读它）。所以树在状态转移时调本 task，把动作态写回去。
 *
 * ★ **只在服务器写**（客户端直接 `Succeeded`；客户端的动作态来自复制）。
 *
 * 典型串法：技能子树末尾挂 `Asce Set Action State (Idle)`；持续施法进入循环时挂 `(Channel)`。
 */
USTRUCT()
struct FAsceSetActionStateTaskInstanceData
{
	GENERATED_BODY()

	/** 要写的动作状态 */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	EAsceActionState State = EAsceActionState::Idle;

	/** 是否 `++Counter`（客户端据此"播一次"；真的换了动作才用） */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bBumpCounter = true;
};

USTRUCT(meta = (DisplayName = "Asce Set Action State", Category = "Asce"))
struct FAsceSetActionStateTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FAsceSetActionStateTaskInstanceData;

	FAsceSetActionStateTask();

protected:
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transitions) const override;
	virtual void GetDependencies(UE::MassBehavior::FStateTreeDependencyBuilder& Builder) const override;

	/** 要写的动作 fragment（**ReadWrite**：本 task 就是它的写者之一） */
	TStateTreeExternalDataHandle<FAsceActionFragment, EStateTreeExternalDataRequirement::Optional> ActionHandle;
};
