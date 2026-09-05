// Fill out your copyright notice in the Description page of Project Settings.
#pragma once

#include "Components/ControllerComponent.h"
#include "AsceCharacterPartTypes.h"

#include "AsceControllerComponent_CharacterParts.generated.h"

class APawn;
class UAscePawnComponent_CharacterParts;
class UObject;
struct FFrame;

// 部件的来源
enum class ECharacterPartSource : uint8
{
	Natural,//自然获得的部件

	NaturalSuppressedViaCheat,//此时被作弊屏蔽的自然获得的部件

	AppliedViaDeveloperSettingsCheat,//通过开发者设置作弊获得的部件

	AppliedViaCheatManager//通过作弊管理器作弊获得的部件
};

//////////////////////////////////////////////////////////////////////

// A character part requested on a controller component
USTRUCT()
struct FAsceControllerCharacterPartEntry
{
	GENERATED_BODY()

	FAsceControllerCharacterPartEntry()
	{}

public:
	// 要添加的部件信息
	UPROPERTY(EditAnywhere, meta=(ShowOnlyInnerProperties))
	FAsceCharacterPart Part;

	// 添加部件后返回的句柄
	FAsceCharacterPartHandle Handle;

	// 部件的来源
	ECharacterPartSource Source = ECharacterPartSource::Natural;
};

//////////////////////////////////////////////////////////////////////

// A component that configure what cosmetic actors to spawn for the owning controller when it possesses a pawn
// 挂载在Controller上，当Controller占据一个pawn的时候，决定要生成哪些cosmetic actors
UCLASS(meta = (BlueprintSpawnableComponent))
class UAsceControllerComponent_CharacterParts : public UControllerComponent
{
	GENERATED_BODY()

public:
	UAsceControllerComponent_CharacterParts(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	//~UActorComponent interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~End of UActorComponent interface

	// Adds a character part to the actor that owns this customization component, should be called on the authority only
	// 只在权威端可调用，为具有Cutomization组件的Pawn添加部件
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category=Cosmetics)
	void AddCharacterPart(const FAsceCharacterPart& NewPart);

	// Removes a previously added character part from the actor that owns this customization component, should be called on the authority only
	// 为具有Customization组件的Pawn移除部件
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category=Cosmetics)
	void RemoveCharacterPart(const FAsceCharacterPart& PartToRemove);

	// Removes all added character parts, should be called on the authority only
	// 移除所有的角色部件
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category=Cosmetics)
	void RemoveAllCharacterParts();

	// Applies relevant developer settings if in PIE
	// PIE模式下应用相关的开发者设置
	void ApplyDeveloperSettings();

protected:
	//存放所有添加到这个controller占有的Pawn上的部件信息
	UPROPERTY(EditAnywhere, Category=Cosmetics)
	TArray<FAsceControllerCharacterPartEntry> CharacterParts;

private:
	//获取Pawn身上的Customize组件
	UAscePawnComponent_CharacterParts* GetPawnCustomizer() const;

	//占用的Pawn改变的时候调用，为新的pawn添加部件
	UFUNCTION()
	void OnPossessedPawnChanged(APawn* OldPawn, APawn* NewPawn);
	
	void AddCharacterPartInternal(const FAsceCharacterPart& NewPart, ECharacterPartSource Source);

	void AddCheatPart(const FAsceCharacterPart& NewPart, bool bSuppressNaturalParts);
	void ClearCheatParts();

	void SetSuppressionOnNaturalParts(bool bSuppressed);

	friend class UAsceCosmeticCheats;
};
