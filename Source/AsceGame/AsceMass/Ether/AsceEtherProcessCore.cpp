// AsceEtherProcessCore.cpp
#include "AsceEtherProcessCore.h"

#include "AsceMass/Ether/AsceEtherNetworkSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceEtherProcessCore)

void UAsceEtherProcessCore::OnInit(UAsceEtherNetworkSubsystem& Net, int32 NodeId)
{
	// 默认什么都不做；子类按需设置自己端口的默认值。
}

void UAsceEtherProcessCore::Tick(UAsceEtherNetworkSubsystem& Net, int32 NodeId)
{
	// 默认什么都不做（组合建筑不会调用 Tick）。
}

FString UAsceEtherProcessCore::GetDebugString() const
{
	return FString();
}
