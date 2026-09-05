// Fill out your copyright notice in the Description page of Project Settings.


#include "Cosmetics/AsceControllerComponent_CharacterParts.h"
#include "Cosmetics/AsceCharacterPartTypes.h"
#include "Cosmetics/AscePawnComponent_CharacterParts.h"
#include "GameFramework/CheatManagerDefines.h"
#include "AsceCosmeticDeveloperSettings.h"
#include "GameFramework/Pawn.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceControllerComponent_CharacterParts)

//////////////////////////////////////////////////////////////////////

UAsceControllerComponent_CharacterParts::UAsceControllerComponent_CharacterParts(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UAsceControllerComponent_CharacterParts::BeginPlay()
{
	Super::BeginPlay();

	// Listen for pawn possession changed events
	// 注册Controller占有Pawn改变时的回调
	if (HasAuthority())
	{
		if (AController* OwningController = GetController<AController>())
		{
			OwningController->OnPossessedPawnChanged.AddDynamic(this, &ThisClass::OnPossessedPawnChanged);
			
			//如果这时候已经有Pawn了，就先调一下初始化
			if (APawn* ControlledPawn = GetPawn<APawn>())
			{
				OnPossessedPawnChanged(nullptr, ControlledPawn);
			}
		}

		ApplyDeveloperSettings();
	}
}

void UAsceControllerComponent_CharacterParts::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RemoveAllCharacterParts();
	Super::EndPlay(EndPlayReason);
}

UAscePawnComponent_CharacterParts* UAsceControllerComponent_CharacterParts::GetPawnCustomizer() const
{
	if (APawn* ControlledPawn = GetPawn<APawn>())
	{
		return ControlledPawn->FindComponentByClass<UAscePawnComponent_CharacterParts>();
	}
	return nullptr;
}

void UAsceControllerComponent_CharacterParts::AddCharacterPart(const FAsceCharacterPart& NewPart)
{
	AddCharacterPartInternal(NewPart, ECharacterPartSource::Natural);
}

void UAsceControllerComponent_CharacterParts::AddCharacterPartInternal(const FAsceCharacterPart& NewPart, ECharacterPartSource Source)
{
	FAsceControllerCharacterPartEntry& NewEntry = CharacterParts.AddDefaulted_GetRef();
	NewEntry.Part = NewPart;
	NewEntry.Source = Source;

	if (UAscePawnComponent_CharacterParts* PawnCustomizer = GetPawnCustomizer())
	{
		if (NewEntry.Source != ECharacterPartSource::NaturalSuppressedViaCheat)
		{
			NewEntry.Handle = PawnCustomizer->AddCharacterPart(NewPart);
		}
	}

}

void UAsceControllerComponent_CharacterParts::RemoveCharacterPart(const FAsceCharacterPart& PartToRemove)
{
	for (auto EntryIt = CharacterParts.CreateIterator(); EntryIt; ++EntryIt)
	{
		if (FAsceCharacterPart::AreEquivalentParts(EntryIt->Part, PartToRemove))
		{
			if (UAscePawnComponent_CharacterParts* PawnCustomizer = GetPawnCustomizer())
			{
				PawnCustomizer->RemoveCharacterPart(EntryIt->Handle);
			}

			EntryIt.RemoveCurrent();
			break;
		}
	}
}

void UAsceControllerComponent_CharacterParts::RemoveAllCharacterParts()
{
	if (UAscePawnComponent_CharacterParts* PawnCustomizer = GetPawnCustomizer())
	{
		for (FAsceControllerCharacterPartEntry& Entry : CharacterParts)
		{
			PawnCustomizer->RemoveCharacterPart(Entry.Handle);
		}
	}

	CharacterParts.Reset();
}

void UAsceControllerComponent_CharacterParts::OnPossessedPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	// Remove from the old pawn
	// 把原来的Pawn身上的部件删除了
	if (UAscePawnComponent_CharacterParts* OldCustomizer = OldPawn ? OldPawn->FindComponentByClass<UAscePawnComponent_CharacterParts>() : nullptr)
	{
		for (FAsceControllerCharacterPartEntry& Entry : CharacterParts)
		{
			OldCustomizer->RemoveCharacterPart(Entry.Handle);
			Entry.Handle.Reset();
		}
	}

	// Apply to the new pawn
	// 为新的Pawn添加部件
	if (UAscePawnComponent_CharacterParts* NewCustomizer = NewPawn ? NewPawn->FindComponentByClass<UAscePawnComponent_CharacterParts>() : nullptr)
	{
		for (FAsceControllerCharacterPartEntry& Entry : CharacterParts)
		{
			// Don't readd if it's already there, this can get called with a null oldpawn
			if (!Entry.Handle.IsValid() && Entry.Source != ECharacterPartSource::NaturalSuppressedViaCheat)
			{
				Entry.Handle = NewCustomizer->AddCharacterPart(Entry.Part);
			}
		}
	}
}

void UAsceControllerComponent_CharacterParts::ApplyDeveloperSettings()
{
	//应用开发者作弊设置，将
#if UE_WITH_CHEAT_MANAGER
	const UAsceCosmeticDeveloperSettings* Settings = GetDefault<UAsceCosmeticDeveloperSettings>();

	// Suppress or unsuppress natural parts if needed
	// 是否需要屏蔽已经添加了的自然部件
	const bool bSuppressNaturalParts = (Settings->CheatMode == ECosmeticCheatMode::ReplaceParts) && (Settings->CheatCosmeticCharacterParts.Num() > 0);
	SetSuppressionOnNaturalParts(bSuppressNaturalParts);

	// Remove anything added by developer settings and re-add it
	// 把上一次添加的作弊部件全部删除
	UAscePawnComponent_CharacterParts* PawnCustomizer = GetPawnCustomizer();
	for (auto It = CharacterParts.CreateIterator(); It; ++It)
	{
		if (It->Source == ECharacterPartSource::AppliedViaDeveloperSettingsCheat)
		{
			if (PawnCustomizer != nullptr)
			{
				PawnCustomizer->RemoveCharacterPart(It->Handle);
			}
			It.RemoveCurrent();
		}
	}

	// 把这一次的新的作弊部件添加进来
	for (const FAsceCharacterPart& PartDesc : Settings->CheatCosmeticCharacterParts)
	{
		AddCharacterPartInternal(PartDesc, ECharacterPartSource::AppliedViaDeveloperSettingsCheat);
	}
#endif
}


void UAsceControllerComponent_CharacterParts::AddCheatPart(const FAsceCharacterPart& NewPart, bool bSuppressNaturalParts)
{
#if UE_WITH_CHEAT_MANAGER
	SetSuppressionOnNaturalParts(bSuppressNaturalParts);
	AddCharacterPartInternal(NewPart, ECharacterPartSource::AppliedViaCheatManager);
#endif
}

void UAsceControllerComponent_CharacterParts::ClearCheatParts()
{
#if UE_WITH_CHEAT_MANAGER
	UAscePawnComponent_CharacterParts* PawnCustomizer = GetPawnCustomizer();

	// Remove anything added by cheat manager cheats
	for (auto It = CharacterParts.CreateIterator(); It; ++It)
	{
		if (It->Source == ECharacterPartSource::AppliedViaCheatManager)
		{
			if (PawnCustomizer != nullptr)
			{
				PawnCustomizer->RemoveCharacterPart(It->Handle);
			}
			It.RemoveCurrent();
		}
	}

	ApplyDeveloperSettings();
#endif
}

void UAsceControllerComponent_CharacterParts::SetSuppressionOnNaturalParts(bool bSuppressed)
{
	//设置是否屏蔽自然部件
#if UE_WITH_CHEAT_MANAGER
	UAscePawnComponent_CharacterParts* PawnCustomizer = GetPawnCustomizer();

	for (FAsceControllerCharacterPartEntry& Entry : CharacterParts)
	{
		if ((Entry.Source == ECharacterPartSource::Natural) && bSuppressed)
		{
			// 要屏蔽自然部件并且当前的是自然部件，从Pawn身上移除（但是并没有真的删除，只是被屏蔽了）
			if (PawnCustomizer != nullptr)
			{
				PawnCustomizer->RemoveCharacterPart(Entry.Handle);
				Entry.Handle.Reset();
			}
			//设置当前的部件entry为被作弊屏蔽的自然部件
			Entry.Source = ECharacterPartSource::NaturalSuppressedViaCheat;
		}
		else if ((Entry.Source == ECharacterPartSource::NaturalSuppressedViaCheat) && !bSuppressed)
		{
			// 如果取消屏蔽自然部件，并且当前是被作弊屏蔽的自然部件，说明是上一次调用这个函数屏蔽的自然部件
			// 把他们加回到Pawn身上，取消屏蔽
			if (PawnCustomizer != nullptr)
			{
				Entry.Handle = PawnCustomizer->AddCharacterPart(Entry.Part);
			}
			//设置会原本的自然部件
			Entry.Source = ECharacterPartSource::Natural;
		}
	}
#endif
}

