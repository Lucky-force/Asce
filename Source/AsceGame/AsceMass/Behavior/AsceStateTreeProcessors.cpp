// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceMass/Behavior/AsceStateTreeProcessors.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceStateTreeProcessors)

UAsceStateTreeProcessor::UAsceStateTreeProcessor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 引擎默认 Standalone|Server（AI 假设）⇒ 这里扩到三端，客户端也要跑表现 task
	ExecutionFlags = static_cast<uint8>(EProcessorExecutionFlags::AllNetModes);
}

UAsceStateTreeActivationProcessor::UAsceStateTreeActivationProcessor()
{
	// 注：引擎默认的那个已经在 DefaultMass.ini 里关掉自动注册（否则听服上会重复分配实例数据）
	ExecutionFlags = static_cast<uint8>(EProcessorExecutionFlags::AllNetModes);
}

UAsceStateTreeFragmentDestructor::UAsceStateTreeFragmentDestructor()
{
	ExecutionFlags = static_cast<uint8>(EProcessorExecutionFlags::AllNetModes);
}
