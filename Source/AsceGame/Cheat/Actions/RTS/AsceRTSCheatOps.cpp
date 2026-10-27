// Fill out your copyright notice in the Description page of Project Settings.

#include "Cheat/Actions/RTS/AsceRTSCheatOps.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "MassEntityHandle.h"

#include "AsceMass/Building/AsceBuildingDef.h"
#include "AsceMass/Building/AsceBuildingSubsystem.h"
#include "AsceMass/Building/AsceBuildingTypes.h"
#include "AsceMass/Data/AsceEntityDefLibrary.h"
#include "AsceMass/Data/AsceUnitDef.h"
#include "AsceMass/Lifecycle/AsceDestroyQueueSubsystem.h"
#include "AsceMass/Spawn/AsceSpawnQueueSubsystem.h"
#include "DataTableSystem/AsceDataTableSubsystem.h"

namespace
{
	/** 建筑逻辑网格边长（cm）—— 与 `FAsceGridFootprintFragment::OriginGridLocation` 同一套 */
	constexpr float LogicalCellSizeCm = 100.f;
}

bool AsceRTSCheatOps::ValidateDefRow(const FName DefRowName, const bool bWantBuilding, FAsceEntityDefRow& OutRow, FString& OutError)
{
	OutError.Reset();

	if (DefRowName.IsNone())
	{
		OutError = TEXT("缺少 DefRow");
		return false;
	}
	if (!UAsceEntityDefLibrary::GetEntityDefRowByName(DefRowName, OutRow))
	{
		OutError = FString::Printf(TEXT("找不到实体定义 [%s]（用 asce.cheat ListDefs 看可用行）"), *DefRowName.ToString());
		return false;
	}

	const UScriptStruct* DefStruct = OutRow.Def.GetScriptStruct();
	const bool bIsBuilding = DefStruct && DefStruct->IsChildOf(FAsceBuildingDefBase::StaticStruct());
	const bool bIsUnit = DefStruct && DefStruct->IsChildOf(FAsceRTSUnitDef::StaticStruct());
	if (bWantBuilding && !bIsBuilding)
	{
		OutError = FString::Printf(TEXT("[%s] 不是建筑定义"), *DefRowName.ToString());
		return false;
	}
	if (!bWantBuilding && !bIsUnit)
	{
		OutError = FString::Printf(TEXT("[%s] 不是单位定义"), *DefRowName.ToString());
		return false;
	}
	return true;
}

bool AsceRTSCheatOps::SpawnUnits(UWorld* World, const FName DefRowName, const FVector& Center, const int32 Count
	, const float SpreadRadius, const int32 TeamId, FString& OutSummary, FString& OutError)
{
	OutSummary.Reset();
	OutError.Reset();

	if (!World)
	{
		OutError = TEXT("没有 World");
		return false;
	}
	if (World->GetNetMode() == NM_Client)
	{
		OutError = TEXT("只能在服务器上生成");
		return false;
	}

	FAsceEntityDefRow DefRow;
	if (!ValidateDefRow(DefRowName, /*bWantBuilding*/ false, DefRow, OutError))
	{
		return false;
	}

	UAsceSpawnQueueSubsystem* SpawnQueue = World->GetSubsystem<UAsceSpawnQueueSubsystem>();
	if (!SpawnQueue)
	{
		OutError = TEXT("没有 UAsceSpawnQueueSubsystem");
		return false;
	}

	const int32 SafeCount = FMath::Clamp(Count, 1, UAsceSpawnQueueSubsystem::MaxRequestsPerFrame);
	const bool bSpread = SafeCount > 1 && SpreadRadius > 0.f;

	for (int32 Index = 0; Index < SafeCount; ++Index)
	{
		// 环形均分（不重叠）；单个就直接放在中心
		const float Angle = 2.f * PI * static_cast<float>(Index) / static_cast<float>(SafeCount);
		const FVector Offset = bSpread
			? FVector(FMath::Cos(Angle) * SpreadRadius, FMath::Sin(Angle) * SpreadRadius, 0.f)
			: FVector::ZeroVector;

		FAsceSpawnRequest Request;
		Request.DefRowName = DefRowName;
		Request.TeamId = TeamId;
		Request.Transform = FTransform(FRotator::ZeroRotator, Center + Offset);
		SpawnQueue->Enqueue(Request);
	}

	OutSummary = FString::Printf(TEXT("已入队 %d 个 [%s]（Team=%d，阶段 8 生成）"), SafeCount, *DefRowName.ToString(), TeamId);
	return true;
}

bool AsceRTSCheatOps::PlaceBuilding(UWorld* World, const FName DefRowName, const FIntPoint& CenterCell, const int32 Rotation
	, const int32 TeamId, FString& OutSummary, FString& OutError)
{
	OutSummary.Reset();
	OutError.Reset();

	if (!World)
	{
		OutError = TEXT("没有 World");
		return false;
	}
	if (World->GetNetMode() == NM_Client)
	{
		OutError = TEXT("只能在服务器上放置建筑");
		return false;
	}

	FAsceEntityDefRow DefRow;
	if (!ValidateDefRow(DefRowName, /*bWantBuilding*/ true, DefRow, OutError))
	{
		return false;
	}

	const FAsceBuildingDefBase* Def = DefRow.Def.GetPtr<FAsceBuildingDefBase>();
	if (!Def)
	{
		OutError = TEXT("建筑定义无效");
		return false;
	}

	UAsceBuildingSubsystem* Buildings = UAsceBuildingSubsystem::Get(World);
	if (!Buildings)
	{
		OutError = TEXT("没有 UAsceBuildingSubsystem");
		return false;
	}

	const FAsceBuildingPlacement Placement = FAsceBuildingPlacement::MakeFromDefCenter(
		DefRowName, FIntPoint(FMath::Max(1, Def->SizeX), FMath::Max(1, Def->SizeY)), CenterCell
		, FMath::Clamp(Rotation, 0, 3), TeamId);

	// 先校验（违规**永不强制放置**），再交子系统
	if (!Buildings->ValidatePlacement(Placement, &OutError, nullptr))
	{
		return false;
	}
	if (!Buildings->PlaceBuilding(Placement, &OutError))
	{
		return false;
	}

	OutSummary = FString::Printf(TEXT("已放置 [%s] @ 格(%d,%d) 旋转%d Team=%d"),
		*DefRowName.ToString(), CenterCell.X, CenterCell.Y, FMath::Clamp(Rotation, 0, 3), TeamId);
	return true;
}

bool AsceRTSCheatOps::DestroyEntity(UWorld* World, const FMassEntityHandle& Entity, FString& OutSummary, FString& OutError)
{
	OutSummary.Reset();
	OutError.Reset();

	if (!World || World->GetNetMode() == NM_Client)
	{
		OutError = TEXT("只能在服务器上删除");
		return false;
	}
	if (!Entity.IsValid())
	{
		OutError = TEXT("实体句柄无效");
		return false;
	}

	// 建筑（已建成 / 施工中）走建筑子系统（内部会统一转 `RequestDestroy`，还会清理 core）；其它直接请求销毁
	if (UAsceBuildingSubsystem* Buildings = UAsceBuildingSubsystem::Get(World))
	{
		if (Buildings->DemolishBuilding(Entity, nullptr))
		{
			OutSummary = FString::Printf(TEXT("已删除建筑 %s"), *Entity.DebugGetDescription());
			return true;
		}
	}

	UAsceDestroyQueueSubsystem* DestroyQueue = World->GetSubsystem<UAsceDestroyQueueSubsystem>();
	if (!DestroyQueue || !DestroyQueue->RequestDestroy(Entity))
	{
		OutError = FString::Printf(TEXT("删除失败（实体无效 / 已在销毁中？）%s"), *Entity.DebugGetDescription());
		return false;
	}

	OutSummary = FString::Printf(TEXT("已请求销毁 %s"), *Entity.DebugGetDescription());
	return true;
}

bool AsceRTSCheatOps::ListDefs(UWorld* World, const bool bUnitsOnly, const bool bBuildingsOnly, FString& OutSummary, FString& OutError)
{
	OutSummary.Reset();
	OutError.Reset();

	TMap<FName, FAsceEntityDefRow> Rows;
	if (!UAsceDataTableSubsystem::GetDefinitionMap<FAsceEntityDefRow>(Rows))
	{
		OutError = TEXT("实体定义表还没加载");
		return false;
	}

	FString Units;
	FString Buildings;
	for (const TPair<FName, FAsceEntityDefRow>& Pair : Rows)
	{
		const UScriptStruct* DefStruct = Pair.Value.Def.GetScriptStruct();
		if (!DefStruct)
		{
			continue;
		}
		const FString Line = FString::Printf(TEXT("\n  %s  %s"), *Pair.Key.ToString(),
			*Pair.Value.DisplayName.ToString());

		if (DefStruct->IsChildOf(FAsceBuildingDefBase::StaticStruct()))
		{
			Buildings += Line;
		}
		else if (DefStruct->IsChildOf(FAsceRTSUnitDef::StaticStruct()))
		{
			Units += Line;
		}
	}

	const bool bShowUnits = !bBuildingsOnly;
	const bool bShowBuildings = !bUnitsOnly;
	if (!bShowUnits && !bShowBuildings)
	{
		OutError = TEXT("UnitsOnly 和 BuildingsOnly 不能同时为 true");
		return false;
	}

	OutSummary = TEXT("可生成定义：");
	if (bShowUnits)
	{
		OutSummary += TEXT("\n[单位]") + (Units.IsEmpty() ? TEXT("（无）") : Units);
	}
	if (bShowBuildings)
	{
		OutSummary += TEXT("\n[建筑]") + (Buildings.IsEmpty() ? TEXT("（无）") : Buildings);
	}
	return true;
}

bool AsceRTSCheatOps::GetCursorWorldLocation(APlayerController* PC, FVector& OutLocation, FString& OutError)
{
	OutError.Reset();

	UWorld* World = PC ? PC->GetWorld() : nullptr;
	if (!PC || !World)
	{
		OutError = TEXT("没有请求者 PlayerController");
		return false;
	}

	// 鼠标 → 屏幕点 → 射线 → 地面（和移动/技能指令同一条取点思路）
	FVector2D MousePosition;
	if (PC->GetMousePosition(MousePosition.X, MousePosition.Y))
	{
		FVector RayOrigin;
		FVector RayDirection;
		if (PC->DeprojectScreenPositionToWorld(MousePosition.X, MousePosition.Y, RayOrigin, RayDirection))
		{
			FHitResult Hit;
			if (World->LineTraceSingleByChannel(Hit, RayOrigin, RayOrigin + RayDirection * 100000.f, ECC_Visibility))
			{
				OutLocation = Hit.ImpactPoint;
				return true;
			}
		}
	}

	// 兜底：请求者的 Pawn 位置
	if (const APawn* Pawn = PC->GetPawn())
	{
		OutLocation = Pawn->GetActorLocation();
		return true;
	}

	OutError = TEXT("光标没有命中地面，且请求者没有 Pawn（请显式给 Pos=x,y,z）");
	return false;
}

FIntPoint AsceRTSCheatOps::WorldToLogicalCell(const FVector& WorldLocation)
{
	return FIntPoint(
		FMath::FloorToInt(WorldLocation.X / LogicalCellSizeCm),
		FMath::FloorToInt(WorldLocation.Y / LogicalCellSizeCm));
}

int32 AsceRTSCheatOps::ResolveTeamId(const int32 TeamParam, const int32 RequesterTeamId)
{
	if (TeamParam < 0)
	{
		return RequesterTeamId;
	}
	return TeamParam;
}
