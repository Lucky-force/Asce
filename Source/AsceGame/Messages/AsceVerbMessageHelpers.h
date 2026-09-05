#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"

#include "AsceVerbMessageHelpers.generated.h"

#define UE_API ASCEGAME_API

struct FGameplayCueParameters;
struct FAsceVerbMessage;

class APlayerController;
class APlayerState;
class UObject;
struct FFrame;


UCLASS(MinimalAPI)
class UAsceVerbMessageHelpers : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Asce")
	static UE_API APlayerState* GetPlayerStateFromObject(UObject* Object);

	UFUNCTION(BlueprintCallable, Category = "Asce")
	static UE_API APlayerController* GetPlayerControllerFromObject(UObject* Object);

	UFUNCTION(BlueprintCallable, Category = "Asce")
	static UE_API FGameplayCueParameters VerbMessageToCueParameters(const FAsceVerbMessage& Message);

	UFUNCTION(BlueprintCallable, Category = "Asce")
	static UE_API FAsceVerbMessage CueParametersToVerbMessage(const FGameplayCueParameters& Params);
};

#undef UE_API
