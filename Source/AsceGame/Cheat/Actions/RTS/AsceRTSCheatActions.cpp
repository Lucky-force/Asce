// Fill out your copyright notice in the Description page of Project Settings.

#include "Cheat/Actions/RTS/AsceRTSCheatActions.h"

#include "MassEntityHandle.h"

#include "Cheat/Actions/RTS/AsceRTSCheatArgTypes.h"
#include "Cheat/Actions/RTS/AsceRTSCheatOps.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceRTSCheatActions)

namespace
{
	const FAsceCheatActionArg* FindArg(const TMap<FName, FAsceCheatActionArg>& Args, const TCHAR* Name)
	{
		return Args.Find(FName(Name));
	}

	/** 参数是否被"显式填了内容"（空 = 用默认/自动） */
	bool HasContent(const FAsceCheatActionArg* Arg)
	{
		return Arg && !Arg->ArgContent.IsEmpty();
	}
}

// =============================================================================================
// SpawnUnits
// =============================================================================================

FText UAsceRTSCheatActionSpawnUnits::GetDisplayName() const
{
	return INVTEXT("生成单位");
}

FText UAsceRTSCheatActionSpawnUnits::GetCategory() const
{
	return INVTEXT("生成");
}

TMap<FName, FAsceCheatActionArg> UAsceRTSCheatActionSpawnUnits::BuildArgs() const
{
	TMap<FName, FAsceCheatActionArg> Args;

	FAsceCheatArgDefRow DefRowType;
	DefRowType.bUnitsOnly = true;
	FAsceCheatActionArg DefRow;
	DefRow.ArgName = TEXT("DefRow");
	DefRow.DisplayName = INVTEXT("单位定义行");
	DefRow.Tooltip = INVTEXT("实体定义表里的行名（ListDefs 可查）");
	DefRow.bRequired = true;
	DefRow.ArgType.InitializeAs<FAsceCheatArgDefRow>(DefRowType);
	Args.Add(DefRow.ArgName, DefRow);

	FAsceCheatArgInt CountType;
	CountType.Min = 1;
	CountType.Max = 200;
	FAsceCheatActionArg Count;
	Count.ArgName = TEXT("Count");
	Count.DisplayName = INVTEXT("数量");
	Count.Tooltip = INVTEXT("一次最多 200（生成队列单帧上限）");
	Count.ArgContent = TEXT("1");
	Count.ArgType.InitializeAs<FAsceCheatArgInt>(CountType);
	Args.Add(Count.ArgName, Count);

	FAsceCheatActionArg Team = AsceCheat::MakeArg<FAsceCheatArgTeam>(TEXT("Team"), INVTEXT("阵营"), TEXT("-1"));
	Team.Tooltip = INVTEXT("-1 = 请求者阵营，0 = 中立，>0 = 指定 TeamId");
	Args.Add(Team.ArgName, Team);

	FAsceCheatArgFloat SpreadType;
	SpreadType.Min = 0.f;
	SpreadType.Max = 5000.f;
	FAsceCheatActionArg Spread;
	Spread.ArgName = TEXT("Spread");
	Spread.DisplayName = INVTEXT("散开半径");
	Spread.Tooltip = INVTEXT("多个单位环形散开的半径（cm），0 = 全堆在一点");
	Spread.ArgContent = TEXT("200");
	Spread.ArgType.InitializeAs<FAsceCheatArgFloat>(SpreadType);
	Args.Add(Spread.ArgName, Spread);

	FAsceCheatActionArg Pos = AsceCheat::MakeArg<FAsceCheatArgVector>(TEXT("Pos"), INVTEXT("位置"));
	Pos.Tooltip = INVTEXT("留空 = 鼠标下的地面点；格式 x,y,z");
	Args.Add(Pos.ArgName, Pos);

	return Args;
}

bool UAsceRTSCheatActionSpawnUnits::Execute(const FAsceCheatContext& Context, const TMap<FName, FAsceCheatActionArg>& Args
	, FString& OutSummary, FString& OutError) const
{
	const FAsceCheatActionArg* PosArg = FindArg(Args, TEXT("Pos"));

	FVector Center = FVector::ZeroVector;
	if (HasContent(PosArg))
	{
		Center = PosArg->GetVector();
	}
	else if (Context.bHasCursor)
	{
		// 客户端/主机各自在**本机**采集，随请求传上来；不要用服务器 PC 的鼠标
		Center = Context.CursorWorldLocation;
	}
	else if (!AsceRTSCheatOps::GetCursorWorldLocation(Context.Requester, Center, OutError))
	{
		return false;
	}

	const FAsceCheatActionArg* CountArg = FindArg(Args, TEXT("Count"));
	const FAsceCheatActionArg* SpreadArg = FindArg(Args, TEXT("Spread"));
	const FAsceCheatActionArg* TeamArg = FindArg(Args, TEXT("Team"));

	const int32 TeamId = AsceRTSCheatOps::ResolveTeamId(TeamArg ? TeamArg->GetInt() : -1, Context.RequesterTeamId);

	const FAsceCheatActionArg* DefRowArg = FindArg(Args, TEXT("DefRow"));
	return AsceRTSCheatOps::SpawnUnits(Context.World, DefRowArg ? DefRowArg->GetName() : NAME_None, Center
		, CountArg ? CountArg->GetInt() : 1, SpreadArg ? SpreadArg->GetFloat() : 200.f, TeamId, OutSummary, OutError);
}

// =============================================================================================
// PlaceBuilding
// =============================================================================================

FText UAsceRTSCheatActionPlaceBuilding::GetDisplayName() const
{
	return INVTEXT("放置建筑");
}

FText UAsceRTSCheatActionPlaceBuilding::GetCategory() const
{
	return INVTEXT("生成");
}

TMap<FName, FAsceCheatActionArg> UAsceRTSCheatActionPlaceBuilding::BuildArgs() const
{
	TMap<FName, FAsceCheatActionArg> Args;

	FAsceCheatArgDefRow DefRowType;
	DefRowType.bBuildingsOnly = true;
	FAsceCheatActionArg DefRow;
	DefRow.ArgName = TEXT("DefRow");
	DefRow.DisplayName = INVTEXT("建筑定义行");
	DefRow.Tooltip = INVTEXT("实体定义表里的建筑行（ListDefs BuildingsOnly=1 可查）");
	DefRow.bRequired = true;
	DefRow.ArgType.InitializeAs<FAsceCheatArgDefRow>(DefRowType);
	Args.Add(DefRow.ArgName, DefRow);

	FAsceCheatArgInt CellType;
	FAsceCheatActionArg CellX;
	CellX.ArgName = TEXT("CellX");
	CellX.DisplayName = INVTEXT("格子 X");
	CellX.Tooltip = INVTEXT("留空 = 用鼠标位置换算（1 格 = 100cm）");
	CellX.ArgType.InitializeAs<FAsceCheatArgInt>(CellType);
	Args.Add(CellX.ArgName, CellX);

	FAsceCheatActionArg CellY;
	CellY.ArgName = TEXT("CellY");
	CellY.DisplayName = INVTEXT("格子 Y");
	CellY.Tooltip = INVTEXT("和 CellX 一起留空才自动换算");
	CellY.ArgType.InitializeAs<FAsceCheatArgInt>(CellType);
	Args.Add(CellY.ArgName, CellY);

	FAsceCheatArgInt RotationType;
	RotationType.Min = 0;
	RotationType.Max = 3;
	FAsceCheatActionArg Rotation;
	Rotation.ArgName = TEXT("Rotation");
	Rotation.DisplayName = INVTEXT("旋转");
	Rotation.Tooltip = INVTEXT("0~3（顺时针 90°）");
	Rotation.ArgContent = TEXT("0");
	Rotation.ArgType.InitializeAs<FAsceCheatArgInt>(RotationType);
	Args.Add(Rotation.ArgName, Rotation);

	FAsceCheatActionArg Team = AsceCheat::MakeArg<FAsceCheatArgTeam>(TEXT("Team"), INVTEXT("阵营"), TEXT("-1"));
	Team.Tooltip = INVTEXT("-1 = 请求者阵营，0 = 中立，>0 = 指定 TeamId");
	Args.Add(Team.ArgName, Team);

	return Args;
}

bool UAsceRTSCheatActionPlaceBuilding::Execute(const FAsceCheatContext& Context, const TMap<FName, FAsceCheatActionArg>& Args
	, FString& OutSummary, FString& OutError) const
{
	const FAsceCheatActionArg* CellXArg = FindArg(Args, TEXT("CellX"));
	const FAsceCheatActionArg* CellYArg = FindArg(Args, TEXT("CellY"));

	FIntPoint CenterCell = FIntPoint::ZeroValue;
	const bool bHasCellX = HasContent(CellXArg);
	const bool bHasCellY = HasContent(CellYArg);
	if (bHasCellX != bHasCellY)
	{
		OutError = TEXT("CellX 和 CellY 必须同时给（或都不给 = 用光标）");
		return false;
	}
	if (bHasCellX)
	{
		CenterCell = FIntPoint(CellXArg->GetInt(), CellYArg->GetInt());
	}
	else if (Context.bHasCursor)
	{
		CenterCell = AsceRTSCheatOps::WorldToLogicalCell(Context.CursorWorldLocation);
	}
	else
	{
		FVector Cursor;
		if (!AsceRTSCheatOps::GetCursorWorldLocation(Context.Requester, Cursor, OutError))
		{
			return false;
		}
		CenterCell = AsceRTSCheatOps::WorldToLogicalCell(Cursor);
	}

	const FAsceCheatActionArg* RotationArg = FindArg(Args, TEXT("Rotation"));
	const FAsceCheatActionArg* TeamArg = FindArg(Args, TEXT("Team"));
	const int32 TeamId = AsceRTSCheatOps::ResolveTeamId(TeamArg ? TeamArg->GetInt() : -1, Context.RequesterTeamId);

	const FAsceCheatActionArg* DefRowArg = FindArg(Args, TEXT("DefRow"));
	return AsceRTSCheatOps::PlaceBuilding(Context.World, DefRowArg ? DefRowArg->GetName() : NAME_None, CenterCell
		, RotationArg ? RotationArg->GetInt() : 0, TeamId, OutSummary, OutError);
}

// =============================================================================================
// DestroyEntity
// =============================================================================================

FText UAsceRTSCheatActionDestroyEntity::GetDisplayName() const
{
	return INVTEXT("删除实体");
}

FText UAsceRTSCheatActionDestroyEntity::GetCategory() const
{
	return INVTEXT("删除");
}

TMap<FName, FAsceCheatActionArg> UAsceRTSCheatActionDestroyEntity::BuildArgs() const
{
	TMap<FName, FAsceCheatActionArg> Args;

	FAsceCheatActionArg Entity = AsceCheat::MakeArg<FAsceCheatArgEntity>(TEXT("Entity"), INVTEXT("实体句柄"));
	Entity.Tooltip = INVTEXT("int64（UI 可用\"取当前选中\"；控制台可先用 Inspect 查）");
	Entity.bRequired = true;
	Args.Add(Entity.ArgName, Entity);

	return Args;
}

bool UAsceRTSCheatActionDestroyEntity::Execute(const FAsceCheatContext& Context, const TMap<FName, FAsceCheatActionArg>& Args
	, FString& OutSummary, FString& OutError) const
{
	const FAsceCheatActionArg* EntityArg = FindArg(Args, TEXT("Entity"));
	const int64 EntityNumber = EntityArg ? EntityArg->GetEntity() : 0;
	return AsceRTSCheatOps::DestroyEntity(Context.World, FMassEntityHandle::FromNumber(static_cast<uint64>(EntityNumber))
		, OutSummary, OutError);
}

// =============================================================================================
// ListDefs
// =============================================================================================

FText UAsceRTSCheatActionListDefs::GetDisplayName() const
{
	return INVTEXT("列出实体定义");
}

FText UAsceRTSCheatActionListDefs::GetCategory() const
{
	return INVTEXT("查询");
}

TMap<FName, FAsceCheatActionArg> UAsceRTSCheatActionListDefs::BuildArgs() const
{
	TMap<FName, FAsceCheatActionArg> Args;

	FAsceCheatActionArg UnitsOnly = AsceCheat::MakeArg<FAsceCheatArgBool>(TEXT("UnitsOnly"), INVTEXT("只看单位"), TEXT("0"));
	Args.Add(UnitsOnly.ArgName, UnitsOnly);

	FAsceCheatActionArg BuildingsOnly = AsceCheat::MakeArg<FAsceCheatArgBool>(TEXT("BuildingsOnly"), INVTEXT("只看建筑"), TEXT("0"));
	Args.Add(BuildingsOnly.ArgName, BuildingsOnly);

	return Args;
}

bool UAsceRTSCheatActionListDefs::Execute(const FAsceCheatContext& Context, const TMap<FName, FAsceCheatActionArg>& Args
	, FString& OutSummary, FString& OutError) const
{
	const FAsceCheatActionArg* UnitsOnlyArg = FindArg(Args, TEXT("UnitsOnly"));
	const FAsceCheatActionArg* BuildingsOnlyArg = FindArg(Args, TEXT("BuildingsOnly"));

	return AsceRTSCheatOps::ListDefs(Context.World
		, UnitsOnlyArg && UnitsOnlyArg->GetBool()
		, BuildingsOnlyArg && BuildingsOnlyArg->GetBool()
		, OutSummary, OutError);
}
