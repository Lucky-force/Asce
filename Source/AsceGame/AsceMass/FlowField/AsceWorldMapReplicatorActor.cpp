// AsceWorldMapReplicatorActor.cpp
#include "AsceWorldMapReplicatorActor.h"
#include "AscePathFindingSubsystem.h"
#include "Net/UnrealNetwork.h"

AAsceWorldMapReplicatorActor::AAsceWorldMapReplicatorActor()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	PrimaryActorTick.bCanEverTick = false;
}

void AAsceWorldMapReplicatorActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAsceWorldMapReplicatorActor, WorldMapData);
}

void AAsceWorldMapReplicatorActor::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		// TODO: 关卡静态障碍物/地形分析结果灌入WorldMapData的真正入口。
		// 这里先给一个占位尺寸，后续接关卡摆放系统时替换成真实扇区数+批量SetCostField。
		WorldMapData.InitWorldMapData(4, 4);
	}

	if (UAscePathFindingSubsystem* Subsystem = UAscePathFindingSubsystem::Get(this))
	{
		Subsystem->RegisterWorldMapReplicator(this);
	}
}

void AAsceWorldMapReplicatorActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UAscePathFindingSubsystem* Subsystem = UAscePathFindingSubsystem::Get(this))
	{
		Subsystem->UnregisterWorldMapReplicator(this);
	}
	Super::EndPlay(EndPlayReason);
}