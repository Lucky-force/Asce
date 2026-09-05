#pragma once

#include "GameFramework/CharacterMovementComponent.h"
#include "NativeGameplayTags.h"

#include "AsceCharacterMovementComponent.generated.h"

#define UE_API ASCEGAME_API

class UObject;
struct FFrame;

ASCEGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Gameplay_MovementStopped);

/**
 * FAsceCharacterGroundInfo
 *
 *	Information about the ground under the character.  It only gets updated as needed.
 *	记录character脚下的地面信息（HitResult），仅在获取时更新
 */
USTRUCT(BlueprintType)
struct FAsceCharacterGroundInfo
{
	GENERATED_BODY()

	FAsceCharacterGroundInfo()
		: LastUpdateFrame(0)
		, GroundDistance(0.0f)
	{}

	uint64 LastUpdateFrame;

	UPROPERTY(BlueprintReadOnly)
	FHitResult GroundHitResult;

	UPROPERTY(BlueprintReadOnly)
	float GroundDistance;
};


/**
 * UAsceCharacterMovementComponent
 *
 *	The base character movement component class used by this project.
 */
UCLASS(MinimalAPI, Config = Game)
class UAsceCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:

	UE_API UAsceCharacterMovementComponent(const FObjectInitializer& ObjectInitializer);

	UE_API virtual void SimulateMovement(float DeltaTime) override;

	UE_API virtual bool CanAttemptJump() const override;

	// Returns the current ground info.  Calling this will update the ground info if it's out of date.
	//返回当前的地面信息，当调用此函数的时候，将更新当前的地面信息（如果在同一帧调用则只会更新一次）
	UFUNCTION(BlueprintCallable, Category = "Asce|CharacterMovement")
	UE_API const FAsceCharacterGroundInfo& GetGroundInfo();

	//设置加速度
	UE_API void SetReplicatedAcceleration(const FVector& InAcceleration);

	//~UMovementComponent interface
	UE_API virtual FRotator GetDeltaRotation(float DeltaTime) const override;
	UE_API virtual float GetMaxSpeed() const override;
	//~End of UMovementComponent interface

protected:

	UE_API virtual void InitializeComponent() override;

protected:

	// Cached ground info for the character.  Do not access this directly!  It's only updated when accessed via GetGroundInfo().
	FAsceCharacterGroundInfo CachedGroundInfo;

	UPROPERTY(Transient)
	bool bHasReplicatedAcceleration = false;
};

#undef UE_API
