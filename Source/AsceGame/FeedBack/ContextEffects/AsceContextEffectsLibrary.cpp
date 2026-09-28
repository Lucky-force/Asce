// Fill out your copyright notice in the Description page of Project Settings.


#include "AsceContextEffectsLibrary.h"

#include "NiagaraSystem.h"
#include "Engine/AssetManager.h"
#include "Sound/SoundBase.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceContextEffectsLibrary)


void UAsceContextEffectsLibrary::GetEffects(const FGameplayTag Effect, const FGameplayTagContainer Context, 
	TArray<USoundBase*>& Sounds, TArray<UNiagaraSystem*>& NiagaraSystems)
{
	//这个函数会把所有满足条件的Effect的音效和粒子效果返回
	
	
	// Make sure Effect is valid and Library is loaded
	// 保证这个Library处于完成加载的状态
	if (Effect.IsValid() && Context.IsValid() && EffectsLoadState == EContextEffectsLibraryLoadState::Loaded)
	{
		// 遍历所有的ActiveEffect对象
		for (const auto& ActiveContextEffect : ActiveContextEffects)
		{
			// Make sure the Effect is an exact Tag Match and ensure the Context has all tags in the Effect (and neither or both are empty)
			// 查询满足的Effect，并且保证当前需要的Context包含这个Effect的Context
			if (Effect.MatchesTagExact(ActiveContextEffect->EffectTag)
				&& Context.HasAllExact(ActiveContextEffect->Context)
				&& (ActiveContextEffect->Context.IsEmpty() == Context.IsEmpty()))
			{
				// Get all Matching Sounds and Niagara Systems
				Sounds.Append(ActiveContextEffect->Sounds);
				NiagaraSystems.Append(ActiveContextEffect->NiagaraSystems);
			}
		}
	}
}

void UAsceContextEffectsLibrary::LoadEffects()
{
	if (EffectsLoadState != EContextEffectsLibraryLoadState::Loading)
	{
		// Set load state to loading
		EffectsLoadState = EContextEffectsLibraryLoadState::Loading;

		// Clear out any old Active Effects
		ActiveContextEffects.Empty();

		// Call internal loading function
		LoadEffectsInternal();
	}
}

EContextEffectsLibraryLoadState UAsceContextEffectsLibrary::GetContextEffectsLibraryLoadState()
{
	// Return current Load State
	return EffectsLoadState;
}

void UAsceContextEffectsLibrary::LoadEffectsInternal()
{

	// 先复制一下数据来异步加载
	TArray<FAsceContextEffects> LocalContextEffects = ContextEffects;
	
	TArray<FSoftObjectPath> ObjectsToLoad;
	
	for (int i = 0; i < ContextEffects.Num(); ++i)
	{
		if (ContextEffects[i].EffectTag.IsValid() && ContextEffects[i].Context.IsValid())
		{
			for (int j = 0; j < ContextEffects[i].Effects.Num(); ++j)
			{
				ObjectsToLoad.Add(ContextEffects[i].Effects[j]);
			}
		}
	}
	
	//异步加载
	AsyncLoad(ObjectsToLoad, [this]()
	{
		TArray<UAsceActiveContextEffects*> AsceActiveContextEffectsList;
		
		for (int i = 0; i < ContextEffects.Num(); ++i)
		{
			if (ContextEffects[i].EffectTag.IsValid() && ContextEffects[i].Context.IsValid())
			{
				UAsceActiveContextEffects* NewActiveContextEffects = NewObject<UAsceActiveContextEffects>(this);
				for (int j = 0; j < ContextEffects[i].Effects.Num(); ++j)
				{
					UObject* LoadedObject = ContextEffects[i].Effects[j].ResolveObject();
					if (LoadedObject->IsA(USoundBase::StaticClass()))
					{
						if (USoundBase* SoundBase = Cast<USoundBase>(LoadedObject))
						{
							NewActiveContextEffects->Sounds.Add(SoundBase);
						}
					}
					else if (LoadedObject->IsA(UNiagaraSystem::StaticClass()))
					{
						if (UNiagaraSystem* NiagaraSystem = Cast<UNiagaraSystem>(LoadedObject))
						{
							NewActiveContextEffects->NiagaraSystems.Add(NiagaraSystem);
						}
					}
				}
				AsceActiveContextEffectsList.Add(NewActiveContextEffects);
			}
		}
		this->AsceContextEffectLibraryLoadingComplete(AsceActiveContextEffectsList);
	});
}

void UAsceContextEffectsLibrary::AsceContextEffectLibraryLoadingComplete(
	TArray<UAsceActiveContextEffects*> AsceActiveContextEffects)
{
	// Flag data as loaded
	EffectsLoadState = EContextEffectsLibraryLoadState::Loaded;

	// Append incoming Context Effects Array to current list of Active Context Effects
	ActiveContextEffects.Append(AsceActiveContextEffects);
}
