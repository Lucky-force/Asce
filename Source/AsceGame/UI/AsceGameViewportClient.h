#pragma once

#include "CommonGameViewportClient.h"

#include "AsceGameViewportClient.generated.h"

class UGameInstance;
class UObject;

//仅仅为了使用CommonUI的功能，但是直接用CommonViewportClient不太好，继承一下方便之后加代码
UCLASS(BlueprintType)
class UAsceGameViewportClient : public UCommonGameViewportClient
{
	GENERATED_BODY()

public:
	UAsceGameViewportClient();

	virtual void Init(struct FWorldContext& WorldContext, UGameInstance* OwningGameInstance, bool bCreateNewAudioDevice = true) override;
};
