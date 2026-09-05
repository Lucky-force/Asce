
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

#include "AsceVerbMessage.generated.h"
// Represents a generic message of the form Instigator Verb Target (in Context, with Magnitude)
// 用来传递动作消息，比如 ：玩家A对玩家B使用某个技能造成了n点伤害
// 轻量级的消息
USTRUCT(BlueprintType)
struct FAsceVerbMessage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category=Gameplay)
	FGameplayTag Verb;

	UPROPERTY(BlueprintReadWrite, Category=Gameplay)
	TObjectPtr<UObject> Instigator = nullptr;

	UPROPERTY(BlueprintReadWrite, Category=Gameplay)
	TObjectPtr<UObject> Target = nullptr;

	UPROPERTY(BlueprintReadWrite, Category=Gameplay)
	FGameplayTagContainer InstigatorTags;

	UPROPERTY(BlueprintReadWrite, Category=Gameplay)
	FGameplayTagContainer TargetTags;

	UPROPERTY(BlueprintReadWrite, Category=Gameplay)
	FGameplayTagContainer ContextTags;

	UPROPERTY(BlueprintReadWrite, Category=Gameplay)
	double Magnitude = 1.0;

	// Returns a debug string representation of this message
	ASCEGAME_API FString ToString() const;
};