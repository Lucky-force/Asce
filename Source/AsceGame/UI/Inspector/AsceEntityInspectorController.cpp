// AsceEntityInspectorController.cpp
#include "AsceEntityInspectorController.h"

#include "AsceMass/Building/AsceBuildingSubsystem.h"
#include "AsceMass/Components/AsceRTSCommanderComponent.h"
#include "AsceMass/Data/AsceEntityDefLibrary.h"
#include "AsceMass/Fragments/AsceCoreFragments.h"
#include "AsceMass/Fragments/AsceFootprintFragment.h"
#include "AsceMass/Fragments/AsceMovementFragments.h"
#include "AsceMass/Vision/AsceVisionFragments.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "MassCommonFragments.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassEntityView.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceEntityInspectorController)

#define LOCTEXT_NAMESPACE "AsceInspector"

UAsceEntityInspectorController* UAsceEntityInspectorController::Create(UObject* WorldContextObject, APlayerController* PlayerController)
{
	if (!PlayerController)
	{
		return nullptr;
	}

	UAsceEntityInspectorController* Controller = NewObject<UAsceEntityInspectorController>(PlayerController);
	Controller->PC = PlayerController;

	// HUD/面板通常先于 Observer Pawn 构建：pawn 晚到或重生时靠这个通知重绑指挥官
	PlayerController->OnPossessedPawnChanged.AddDynamic(Controller, &UAsceEntityInspectorController::HandlePossessedPawnChanged);

	if (UWorld* World = PlayerController->GetWorld())
	{
		Controller->MassSubsystem = World->GetSubsystem<UMassEntitySubsystem>();
		Controller->BuildingSubsystem = World->GetSubsystem<UAsceBuildingSubsystem>();
	}

	Controller->RebindCommander();
	Controller->RebuildSelection();
	return Controller;
}

void UAsceEntityInspectorController::SetViewActive(bool bActive)
{
	if (bViewActive == bActive)
	{
		return;
	}
	bViewActive = bActive;

	if (bViewActive)
	{
		RebindCommander();
		RebuildSelection();
		RefreshSnapshot(true);
		if (PrimaryEntity.IsSet() && BuildingSubsystem.IsValid())
		{
			BuildingSubsystem->SetBuildingSelected(PrimaryEntity, true);
		}
	}
	else if (PrimaryEntity.IsSet() && BuildingSubsystem.IsValid())
	{
		BuildingSubsystem->SetBuildingSelected(PrimaryEntity, false);
	}
}

void UAsceEntityInspectorController::RefreshNow()
{
	RebindCommander();
	RebuildSelection();
	RefreshSnapshot(true);
}

void UAsceEntityInspectorController::GetSelectedEntities(TArray<int64>& OutResult) const
{
	OutResult.Reset(OutResult.Num() + Selection.Num());
	for (const FMassEntityHandle& Handle : Selection)
	{
		OutResult.Add(Handle.AsNumber());
	}
}

void UAsceEntityInspectorController::GetSelectionList(TArray<FAsceInspectorListEntry>& OutEntries) const
{
	OutEntries.Reset(OutEntries.Num() + Selection.Num());
	const FMassEntityManager* EntityManager = GetEntityManager();
	for (const FMassEntityHandle& Handle : Selection)
	{
		FAsceInspectorListEntry& Entry = OutEntries.AddDefaulted_GetRef();
		Entry.EntityNumber = Handle.AsNumber();
		Entry.bIsPrimary = (Handle == PrimaryEntity);

		int32 TypeId = 0;
		if (EntityManager)
		{
			if (const FAsceUnitDefFragment* Def = EntityManager->GetFragmentDataPtr<FAsceUnitDefFragment>(Handle))
			{
				TypeId = Def->UniqueTypeId;
				Entry.TypeId = TypeId;
			}
		}

		FAsceEntityDefRow Row;
		if (UAsceEntityDefLibrary::GetEntityDefRow(TypeId, Row))
		{
			Entry.DisplayName = Row.DisplayName;
			Entry.HeadIcon = Row.HeadIcon;
		}
	}
}

bool UAsceEntityInspectorController::GetPrimaryDefRow(FAsceEntityDefRow& OutRow, int32& OutTypeId) const
{
	const FMassEntityManager* EntityManager = GetEntityManager();
	if (!EntityManager || !PrimaryEntity.IsSet())
	{
		return false;
	}
	const FAsceUnitDefFragment* Def = EntityManager->GetFragmentDataPtr<FAsceUnitDefFragment>(PrimaryEntity);
	if (!Def)
	{
		return false;
	}
	OutTypeId = Def->UniqueTypeId;
	return UAsceEntityDefLibrary::GetEntityDefRow(Def->UniqueTypeId, OutRow);
}

void UAsceEntityInspectorController::GetStatRows(TArray<FAsceInspectorStatRow>& OutRows) const
{
	OutRows.Reset(OutRows.Num());
	BuildStatRowsInternal(OutRows);
}

void UAsceEntityInspectorController::GetActions(TArray<FAsceBuildingAction>& OutActions) const
{
	OutActions.Reset(OutActions.Num());
	BuildActionsInternal(OutActions);
}

bool UAsceEntityInspectorController::ExecuteAction(FName ActionId)
{
	if (!IsPrimaryBuilding() || !BuildingSubsystem.IsValid())
	{
		return false;
	}
	FString Error;
	return BuildingSubsystem->ExecuteBuildingAction(PrimaryEntity, ActionId, &Error);
}

bool UAsceEntityInspectorController::IsPrimaryBuilding() const
{
	const FMassEntityManager* EntityManager = GetEntityManager();
	if (!EntityManager || !PrimaryEntity.IsSet() || !EntityManager->IsEntityValid(PrimaryEntity))
	{
		return false;
	}
	const FMassEntityView View(*EntityManager, PrimaryEntity);
	return View.HasTag<FAsceBuildingTag>();
}

void UAsceEntityInspectorController::RequestHeadIcon(int64 EntityNumber)
{
	if (PendingIconRequests.Contains(EntityNumber))
	{
		return;
	}

	FMassEntityManager* EntityManager = GetEntityManager();
	if (!EntityManager)
	{
		OnEntityIconLoaded.Broadcast(EntityNumber, nullptr);
		return;
	}
	const FMassEntityHandle Entity = FMassEntityHandle::FromNumber(EntityNumber);
	const FAsceUnitDefFragment* Def = EntityManager->IsEntityValid(Entity)
		? EntityManager->GetFragmentDataPtr<FAsceUnitDefFragment>(Entity)
		: nullptr;

	FAsceEntityDefRow Row;
	if (!Def || !UAsceEntityDefLibrary::GetEntityDefRow(Def->UniqueTypeId, Row) || Row.HeadIcon.IsNull())
	{
		OnEntityIconLoaded.Broadcast(EntityNumber, nullptr);
		return;
	}

	// 已在内存：直接广播（widget 在 Construct 里绑好委托之后才会请求，不存在"广播早于绑定"）
	if (UTexture2D* Loaded = Row.HeadIcon.Get())
	{
		OnEntityIconLoaded.Broadcast(EntityNumber, Loaded);
		return;
	}

	// 异步加载（bManageActiveHandle=true ⇒ 管理器保活句柄，完成后自动清理）
	PendingIconRequests.Add(EntityNumber);
	const FSoftObjectPath Path = Row.HeadIcon.ToSoftObjectPath();
	UAssetManager::GetStreamableManager().RequestAsyncLoad(
		Path,
		FStreamableDelegate::CreateWeakLambda(this, [this, EntityNumber, Path]()
		{
			PendingIconRequests.Remove(EntityNumber);
			UTexture2D* Texture = Cast<UTexture2D>(Path.ResolveObject());
			OnEntityIconLoaded.Broadcast(EntityNumber, Texture);
		}),
		FStreamableManager::DefaultAsyncLoadPriority,
		/*bManageActiveHandle=*/true);
}

// ── FTickableGameObject ──

void UAsceEntityInspectorController::Tick(float DeltaTime)
{
	UpdateAccumulator += DeltaTime;
	if (UpdateAccumulator < UpdateInterval)
	{
		return;
	}
	UpdateAccumulator = 0.f;

	// 玩家 Pawn 重生后重绑；选择漂移（实体死亡等）在 Rebuild 里兜底
	RebindCommander();
	RebuildSelection();
	RefreshSnapshot(false);
}

TStatId UAsceEntityInspectorController::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UAsceEntityInspectorController, STATGROUP_Tickables);
}

UWorld* UAsceEntityInspectorController::GetTickableGameObjectWorld() const
{
	return GetWorld();
}

void UAsceEntityInspectorController::BeginDestroy()
{
	if (APlayerController* PlayerController = PC.Get())
	{
		PlayerController->OnPossessedPawnChanged.RemoveDynamic(this, &UAsceEntityInspectorController::HandlePossessedPawnChanged);
	}
	if (UAsceRTSCommanderComponent* C = Commander.Get())
	{
		C->OnSelectionChanged.Remove(CommanderSelectionHandle);
	}
	Super::BeginDestroy();
}

void UAsceEntityInspectorController::HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	RebindCommander();
	RebuildSelection();
}

// ── 内部 ──

FMassEntityManager* UAsceEntityInspectorController::GetEntityManager() const
{
	UMassEntitySubsystem* Mass = MassSubsystem.Get();
	return Mass ? &Mass->GetMutableEntityManager() : nullptr;
}

void UAsceEntityInspectorController::RebindCommander()
{
	APlayerController* PlayerController = PC.Get();
	APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	UAsceRTSCommanderComponent* NewCommander = Pawn ? Pawn->FindComponentByClass<UAsceRTSCommanderComponent>() : nullptr;
	if (NewCommander == Commander.Get())
	{
		return;
	}

	if (UAsceRTSCommanderComponent* Old = Commander.Get())
	{
		Old->OnSelectionChanged.Remove(CommanderSelectionHandle);
	}
	Commander = NewCommander;
	CommanderSelectionHandle.Reset();
	if (NewCommander)
	{
		CommanderSelectionHandle = NewCommander->OnSelectionChanged.AddUObject(this, &UAsceEntityInspectorController::RebuildSelection);
	}
}

void UAsceEntityInspectorController::RebuildSelection()
{
	const FMassEntityHandle OldPrimary = PrimaryEntity;

	TArray<FMassEntityHandle> NewSelection;
	if (UAsceRTSCommanderComponent* C = Commander.Get())
	{
		TArray<int64> Numbers;
		C->GetSelectedEntities(Numbers);

		FMassEntityManager* EntityManager = GetEntityManager();
		NewSelection.Reserve(Numbers.Num());
		for (const int64 Number : Numbers)
		{
			const FMassEntityHandle Handle = FMassEntityHandle::FromNumber(Number);
			if (EntityManager && EntityManager->IsEntityValid(Handle))
			{
				NewSelection.Add(Handle);
			}
		}
	}

	// 集合没变就不重复广播
	bool bSame = (NewSelection.Num() == Selection.Num());
	if (bSame)
	{
		for (int32 i = 0; i < NewSelection.Num(); ++i)
		{
			if (!(NewSelection[i] == Selection[i]))
			{
				bSame = false;
				break;
			}
		}
	}
	if (bSame)
	{
		return;
	}

	Selection = MoveTemp(NewSelection);
	PrimaryEntity = (Selection.Num() > 0) ? Selection[0] : FMassEntityHandle();

	SetPrimarySelectionHooks(OldPrimary, PrimaryEntity);
	OnSelectionChanged.Broadcast();
	RefreshSnapshot(true);
}

void UAsceEntityInspectorController::RefreshSnapshot(bool bForceBroadcast)
{
	TArray<FAsceInspectorStatRow> StatRows;
	BuildStatRowsInternal(StatRows);
	TArray<FString> StatSnapshot;
	StatSnapshot.Reserve(StatRows.Num());
	for (const FAsceInspectorStatRow& Row : StatRows)
	{
		StatSnapshot.Add(Row.Label.ToString() + TEXT("|") + Row.Value.ToString());
	}

	TArray<FAsceBuildingAction> Actions;
	BuildActionsInternal(Actions);
	TArray<FString> ActionSnapshot;
	ActionSnapshot.Reserve(Actions.Num());
	for (const FAsceBuildingAction& Action : Actions)
	{
		ActionSnapshot.Add(Action.ActionId.ToString() + (Action.bEnabled ? TEXT("|1") : TEXT("|0")));
	}

	if (bForceBroadcast || StatSnapshot != PrevStatSnapshot || ActionSnapshot != PrevActionSnapshot)
	{
		PrevStatSnapshot = MoveTemp(StatSnapshot);
		PrevActionSnapshot = MoveTemp(ActionSnapshot);
		OnPrimaryValuesChanged.Broadcast();
	}
}

void UAsceEntityInspectorController::SetPrimarySelectionHooks(const FMassEntityHandle& OldPrimary, const FMassEntityHandle& NewPrimary)
{
	if (!BuildingSubsystem.IsValid())
	{
		return;
	}
	if (OldPrimary.IsSet() && !(OldPrimary == NewPrimary))
	{
		BuildingSubsystem->SetBuildingSelected(OldPrimary, false);
	}
	if (NewPrimary.IsSet())
	{
		BuildingSubsystem->SetBuildingSelected(NewPrimary, true);
	}
}

void UAsceEntityInspectorController::BuildStatRowsInternal(TArray<FAsceInspectorStatRow>& OutRows) const
{
	const FMassEntityManager* EntityManager = GetEntityManager();
	if (!EntityManager || !PrimaryEntity.IsSet() || !EntityManager->IsEntityValid(PrimaryEntity))
	{
		return;
	}

	auto AddRow = [&OutRows](const FText& Label, const FString& Value)
	{
		FAsceInspectorStatRow& Row = OutRows.AddDefaulted_GetRef();
		Row.Label = Label;
		Row.Value = FText::FromString(Value);
	};

	if (const FAsceHealthFragment* Health = EntityManager->GetFragmentDataPtr<FAsceHealthFragment>(PrimaryEntity))
	{
		AddRow(LOCTEXT("Health", "生命"), FString::Printf(TEXT("%.0f / %.0f"), Health->CurrentHealth, Health->MaxHealth));
	}
	if (const FAsceTeamFragment* Team = EntityManager->GetFragmentDataPtr<FAsceTeamFragment>(PrimaryEntity))
	{
		AddRow(LOCTEXT("Team", "阵营"), FString::FromInt(AsceTeamId(Team->TeamKey)));
	}
	if (const FAgentRadiusFragment* Radius = EntityManager->GetFragmentDataPtr<FAgentRadiusFragment>(PrimaryEntity))
	{
		AddRow(LOCTEXT("Radius", "半径"), FString::Printf(TEXT("%.0f"), Radius->Radius));
	}
	if (const FAsceHeightFragment* Height = EntityManager->GetFragmentDataPtr<FAsceHeightFragment>(PrimaryEntity))
	{
		AddRow(LOCTEXT("Height", "高度"), FString::Printf(TEXT("%.0f"), Height->Height));
	}
	if (const FAsceMovementTuningFragment* Tuning = EntityManager->GetFragmentDataPtr<FAsceMovementTuningFragment>(PrimaryEntity))
	{
		AddRow(LOCTEXT("MaxSpeed", "最大速度"), FString::Printf(TEXT("%.0f"), Tuning->MaxSpeed));
		AddRow(LOCTEXT("DesiredSpeed", "期望速度"), FString::Printf(TEXT("%.0f"), Tuning->DefaultDesiredSpeed));
	}
	if (const FAsceVisionViewFragment* Vision = EntityManager->GetFragmentDataPtr<FAsceVisionViewFragment>(PrimaryEntity))
	{
		AddRow(LOCTEXT("SightRadius", "视野"), FString::FromInt(Vision->SightRadius));
	}
	if (const FAsceGridFootprintFragment* Foot = EntityManager->GetFragmentDataPtr<FAsceGridFootprintFragment>(PrimaryEntity))
	{
		AddRow(LOCTEXT("Footprint", "占地"), FString::Printf(TEXT("%d × %d"), Foot->SizeX, Foot->SizeY));
		AddRow(LOCTEXT("Occlusion", "遮挡高度"), FString::Printf(TEXT("%d"), Foot->OcclusionHeightCm));
	}
}

void UAsceEntityInspectorController::BuildActionsInternal(TArray<FAsceBuildingAction>& OutActions) const
{
	if (IsPrimaryBuilding() && BuildingSubsystem.IsValid())
	{
		BuildingSubsystem->CollectBuildingActions(PrimaryEntity, OutActions);
	}
}

#undef LOCTEXT_NAMESPACE
