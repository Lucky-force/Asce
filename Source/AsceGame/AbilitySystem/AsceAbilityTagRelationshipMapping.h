// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"

#include "AsceAbilityTagRelationshipMapping.generated.h"

class UObject;

/** Struct that defines the relationship between different ability tags */
// 用来定义不同能力的tag之间关系的结构体
USTRUCT()
struct FAsceAbilityTagRelationship
{
	GENERATED_BODY()

	/** The tag that this container relationship is about. Single tag, but abilities can have multiple of these */
	// 代表这个关系的tag， 每个能力可以拥有多个关系，每个关系由一个tag来标识
	UPROPERTY(EditAnywhere, Category = Ability, meta = (Categories = "Gameplay.Action"))
	FGameplayTag AbilityTag;

	/** The other ability tags that will be blocked by any ability using this tag */
	//拥有此关系的tag将要block的所有其他tag
	UPROPERTY(EditAnywhere, Category = Ability)
	FGameplayTagContainer AbilityTagsToBlock;

	/** The other ability tags that will be canceled by any ability using this tag */
	//拥有此关系的tag将要取消的所有其他tag
	UPROPERTY(EditAnywhere, Category = Ability)
	FGameplayTagContainer AbilityTagsToCancel;

	/** If an ability has the tag, this is implicitly added to the activation required tags of the ability */
	//如果能力拥有这个关系的tag，那么激活这个能力需要以下的tag
	UPROPERTY(EditAnywhere, Category = Ability)
	FGameplayTagContainer ActivationRequiredTags;

	/** If an ability has the tag, this is implicitly added to the activation blocked tags of the ability */
	//如果能力拥有这个关系的tag，那么拥有如下容器中的任何一个tag将会block该能力的激活
	UPROPERTY(EditAnywhere, Category = Ability)
	FGameplayTagContainer ActivationBlockedTags;
};


/** Mapping of how ability tags block or cancel other abilities */
// 能力如何阻止或者取消其他能力的关系映射
UCLASS()
class UAsceAbilityTagRelationshipMapping : public UDataAsset
{
	GENERATED_BODY()

private:
	/** The list of relationships between different gameplay tags (which ones block or cancel others) */
	// 关系列表, 以下所有函数都是根据这个关系列表中的关系来返回值
	UPROPERTY(EditAnywhere, Category = Ability, meta=(TitleProperty="AbilityTag"))
	TArray<FAsceAbilityTagRelationship> AbilityTagRelationships;

public:
	/** Given a set of ability tags, parse the tag relationship and fill out tags to block and cancel 
	 * 传入一个tag集合，解析这个集合的tag的关系，返回将要block的tag和cancel的tag
	 */
	void GetAbilityTagsToBlockAndCancel(const FGameplayTagContainer& AbilityTags, FGameplayTagContainer* OutTagsToBlock, FGameplayTagContainer* OutTagsToCancel) const;

	/** Given a set of ability tags, add additional required and blocking tags 
	 * 传入一个tag集合，获取激活这些tag对应的能力需要满足的前置tag和不能拥有的tag
	 */
	void GetRequiredAndBlockedActivationTags(const FGameplayTagContainer& AbilityTags, FGameplayTagContainer* OutActivationRequired, FGameplayTagContainer* OutActivationBlocked) const;

	/** Returns true if the specified ability tags are canceled by the passed in action tag */
	// 传入一个能力的tag集合，检测当前拥有这些tag的能力是否会被ActionTag给取消
	bool IsAbilityCancelledByTag(const FGameplayTagContainer& AbilityTags, const FGameplayTag& ActionTag) const;
};
