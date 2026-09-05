// Fill out your copyright notice in the Description page of Project Settings.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "AsceCharacterPartTypes.generated.h"

class UChildActorComponent;
class UAscePawnComponent_CharacterParts;
struct FAsceCharacterPartList;

//////////////////////////////////////////////////////////////////////

// 生成的部件碰撞配置
UENUM()
enum class ECharacterCustomizationCollisionMode : uint8
{
	// 禁用生成部件的碰撞
	NoCollision,

	// 使用这个部件Actor自身的碰撞
	UseCollisionFromCharacterPart
};

//////////////////////////////////////////////////////////////////////

// A handle created by adding a character part entry, can be used to remove it later
// 添加部件的时候返回的handle，可以用来在之后移除部件
USTRUCT(BlueprintType)
struct FAsceCharacterPartHandle
{
	GENERATED_BODY()

	void Reset()
	{
		PartHandle = INDEX_NONE;
	}

	bool IsValid() const
	{
		return PartHandle != INDEX_NONE;
	}

private:
	UPROPERTY()
	int32 PartHandle = INDEX_NONE;

	friend FAsceCharacterPartList;
};

//////////////////////////////////////////////////////////////////////
// 角色部件

USTRUCT(BlueprintType)
struct FAsceCharacterPart
{
	GENERATED_BODY()

	// 要生成的部位类型
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSubclassOf<AActor> PartClass;

	// 这个部位要attach到的mesh的socket
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName SocketName;

	// 这个部位的碰撞
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	ECharacterCustomizationCollisionMode CollisionMode = ECharacterCustomizationCollisionMode::NoCollision;

	// 比较函数
	static bool AreEquivalentParts(const FAsceCharacterPart& A, const FAsceCharacterPart& B)
	{
		return (A.PartClass == B.PartClass) && (A.SocketName == B.SocketName);
	}
};
