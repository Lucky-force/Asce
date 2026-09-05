#pragma once

#include "GameFramework/PlayerStart.h"
#include "GameplayTagContainer.h"

#include "AscePlayerStart.generated.h"

#define UE_API ASCEGAME_API

class AController;
class UObject;

//当前的出生点位置占有情况
enum class EAscePlayerStartLocationOccupancy
{
	Empty,
	Partial,
	Full
};

/**
 * AAscePlayerStart
 * 
 * Base player starts that can be used by a lot of modes.
 */
UCLASS(MinimalAPI, Config = Game)
class AAscePlayerStart : public APlayerStart
{
	GENERATED_BODY()

public:
	UE_API AAscePlayerStart(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	const FGameplayTagContainer& GetGameplayTags() { return StartPointTags; }

	//获取当前这个出生点所在位置的占有情况（检测这个位置是不是有其他实体占据了空间）
	UE_API EAscePlayerStartLocationOccupancy GetLocationOccupancy(AController* const ControllerPawnToFit) const;

	/** Did this player start get claimed by a controller already? */
	// 是否这个玩家生成点已经被一个controller宣称要用来生成新的pawn了？
	UE_API bool IsClaimed() const;

	/** If this PlayerStart was not claimed, claim it for ClaimingController */
	// 如果这个玩家生成点没有被宣传要用来生成一个新的pawn，让这个controller宣称它
	UE_API bool TryClaim(AController* OccupyingController);

protected:
	/** Check if this PlayerStart is still claimed */
	// 检查是否已经解除了宣称
	UE_API void CheckUnclaimed();

	/** The controller that claimed this PlayerStart */
	// 存放当前宣称这个出生点的Controller
	UPROPERTY(Transient)
	TObjectPtr<AController> ClaimingController = nullptr;

	/** Interval in which we'll check if this player start is not colliding with anyone anymore */
	// 检测时间间隔, 用于每隔一段时间检测是否可以接触宣称了
	UPROPERTY(EditDefaultsOnly, Category = "Player Start Claiming")
	float ExpirationCheckInterval = 1.f;

	/** Tags to identify this player start */
	UPROPERTY(EditAnywhere)
	FGameplayTagContainer StartPointTags;

	/** Handle to track expiration recurring timer */
	FTimerHandle ExpirationTimerHandle;
};

#undef UE_API
