// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "AsceReplicationBootstrapSubsystem.generated.h"

/**
 * 唯一职责：在第一个客户端连进来之前，把AAsceClientBubbleInfo注册进UMassReplicationSubsystem。
 *
 * 见架构文档："RegisterBubbleInfoClass必须在AddClient之前调用"——插件本身不会帮你自动调用，
 * 项目必须自己找个足够早的时机调一次。这里选WorldSubsystem::PostInitialize，
 * 跟引擎自带的UMassCrowdSubsystem::PostInitialize()是同一个时机，足够早。
 */
UCLASS()
class UAsceReplicationBootstrapSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void PostInitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
};
