// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Components/PawnComponent.h"
#include "Cosmetics/AsceCosmeticAnimationTypes.h"
#include "AsceCharacterPartTypes.h"
#include "Net/Serialization/FastArraySerializer.h"

#include "AscePawnComponent_CharacterParts.generated.h"

class UAscePawnComponent_CharacterParts;
namespace EEndPlayReason { enum Type : int; }
struct FGameplayTag;
struct FAsceCharacterPartList;

class AActor;
class UChildActorComponent;
class UObject;
class USceneComponent;
class USkeletalMeshComponent;
struct FFrame;
struct FNetDeltaSerializeInfo;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAsceSpawnedCharacterPartsChanged, UAscePawnComponent_CharacterParts*, ComponentWithChangedParts);

//////////////////////////////////////////////////////////////////////

// A single applied character part
USTRUCT()
struct FAsceAppliedCharacterPartEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	FAsceAppliedCharacterPartEntry()
	{}

	FString GetDebugString() const;

private:
	friend FAsceCharacterPartList;
	friend UAscePawnComponent_CharacterParts;

private:
	// The character part being represented
	UPROPERTY()
	FAsceCharacterPart Part;

	// Handle index we returned to the user (server only)
	UPROPERTY(NotReplicated)
	int32 PartHandle = INDEX_NONE;

	// The spawned actor instance (client only)
	UPROPERTY(NotReplicated)
	TObjectPtr<UChildActorComponent> SpawnedComponent = nullptr;
};

//////////////////////////////////////////////////////////////////////

// Replicated list of applied character parts
USTRUCT(BlueprintType)
struct FAsceCharacterPartList : public FFastArraySerializer
{
	GENERATED_BODY()

	FAsceCharacterPartList()
		: OwnerComponent(nullptr)
	{
	}

public:
	//~FFastArraySerializer contract
	void PreReplicatedRemove(const TArrayView<int32> RemovedIndices, int32 FinalSize);
	void PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize);
	void PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize);
	//~End of FFastArraySerializer contract

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FAsceAppliedCharacterPartEntry, FAsceCharacterPartList>(Entries, DeltaParms, *this);
	}

	FAsceCharacterPartHandle AddEntry(FAsceCharacterPart NewPart);
	void RemoveEntry(FAsceCharacterPartHandle Handle);
	void ClearAllEntries(bool bBroadcastChangeDelegate);

	FGameplayTagContainer CollectCombinedTags() const;

	void SetOwnerComponent(UAscePawnComponent_CharacterParts* InOwnerComponent)
	{
		OwnerComponent = InOwnerComponent;
	}
	
private:
	friend UAscePawnComponent_CharacterParts;

	bool SpawnActorForEntry(FAsceAppliedCharacterPartEntry& Entry);
	bool DestroyActorForEntry(FAsceAppliedCharacterPartEntry& Entry);

private:
	// Replicated list of equipment entries
	UPROPERTY()
	TArray<FAsceAppliedCharacterPartEntry> Entries;

	// 拥有这个数组的组件
	UPROPERTY(NotReplicated)
	TObjectPtr<UAscePawnComponent_CharacterParts> OwnerComponent;

	// Handle计数器，并非static，意味着每个handle的作用域仅仅在这个数组对象内部
	int32 PartHandleCounter = 0;
};

template<>
struct TStructOpsTypeTraits<FAsceCharacterPartList> : public TStructOpsTypeTraitsBase2<FAsceCharacterPartList>
{
	enum { WithNetDeltaSerializer = true };
};

//////////////////////////////////////////////////////////////////////

// A component that handles spawning cosmetic actors attached to the owner pawn on all clients
UCLASS(meta=(BlueprintSpawnableComponent))
class UAscePawnComponent_CharacterParts : public UPawnComponent
{
	GENERATED_BODY()

public:
	UAscePawnComponent_CharacterParts(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	//~UActorComponent interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnRegister() override;
	//~End of UActorComponent interface

	// Adds a character part to the actor that owns this customization component, should be called on the authority only
	// 为拥有这个customization组件的Character添加一个部件
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category=Cosmetics)
	FAsceCharacterPartHandle AddCharacterPart(const FAsceCharacterPart& NewPart);

	// Removes a previously added character part from the actor that owns this customization component, should be called on the authority only
	// 移除一个已添加到这个Pawn身上的部件
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category=Cosmetics)
	void RemoveCharacterPart(FAsceCharacterPartHandle Handle);

	// Removes all added character parts, should be called on the authority only
	// 移除所有的部件
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category=Cosmetics)
	void RemoveAllCharacterParts();

	// Gets the list of all spawned character parts from this component
	// 获取所有的部件
	UFUNCTION(BlueprintCallable, BlueprintPure=false, BlueprintCosmetic, Category=Cosmetics)
	TArray<AActor*> GetCharacterPartActors() const;

	// If the parent actor is derived from ACharacter, returns the Mesh component, otherwise nullptr
	// 如果Owner是一个ACharacter，返回其Mesh
	USkeletalMeshComponent* GetParentMeshComponent() const;

	// Returns the scene component to attach the spawned actors to
	// If the parent actor is derived from ACharacter, we'll use the Mesh component, otherwise the root component
	// 获取这些部件要Attach到的组件，如果Owner是ACharacter，这个组件是meshcomponent，否则为根组件
	USceneComponent* GetSceneComponentToAttachTo() const;

	// Returns the set of combined gameplay tags from attached character parts, optionally filtered to only tags that start with the specified root
	// 返回过滤后的部件Tag
	UFUNCTION(BlueprintCallable, BlueprintPure=false, BlueprintCosmetic, Category=Cosmetics)
	FGameplayTagContainer GetCombinedTags(FGameplayTag RequiredPrefix) const;

	void BroadcastChanged();

public:
	// Delegate that will be called when the list of spawned character parts has changed
	// 生成的部件修改时广播
	UPROPERTY(BlueprintAssignable, Category=Cosmetics, BlueprintCallable)
	FAsceSpawnedCharacterPartsChanged OnCharacterPartsChanged;

private:
	// List of character parts
	// 部件列表，复制到客户端
	UPROPERTY(Replicated, Transient)
	FAsceCharacterPartList CharacterPartList;

	// Rules for how to pick a body style mesh for animation to play on, based on character part cosmetics tags
	// 
	UPROPERTY(EditAnywhere, Category=Cosmetics)
	FAsceAnimBodyStyleSelectionSet BodyMeshes;
};
