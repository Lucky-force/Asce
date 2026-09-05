
#include "GameFeatureAction_WorldActionBase.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GameFeatureAction_WorldActionBase)

void UGameFeatureAction_WorldActionBase::OnGameFeatureActivating(FGameFeatureActivatingContext& Context)
{
	//传入的Context代表这个GameFeature激活时所在的世界上下文
	//在GameFeature被激活的时候，会对其所用的Action调用此函数, 通常这个Context为默认值，意味着对所有的World都激活(Context中存了一个World的名字)
	//同时吗，将Context作为键，用于存放绑定到OnStartGameInstance上的委托句柄，每个World（即每个GameInstance）对应一个句柄，因此使用Context作为键
	
	GameInstanceStartHandles.FindOrAdd(Context) = FWorldDelegates::OnStartGameInstance.AddUObject(this, 
		&UGameFeatureAction_WorldActionBase::HandleGameInstanceStart, FGameFeatureStateChangeContext(Context));

	//绑定了在GameInstanceStart时调用的委托，随后对所有当前已经开始的世界（或者GameInstance）直接调用AddToWorld来应用Action
	//AddToWorld是子类必须实现的纯虚函数，处理Action的真正应用
	
	// Add to any worlds with associated game instances that have already been initialized
	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		if (Context.ShouldApplyToWorldContext(WorldContext))
		{
			AddToWorld(WorldContext, Context);
		}
	}
}

void UGameFeatureAction_WorldActionBase::OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context)
{
	
	//用世界对应的Context来移除之前保存的委托句柄
	FDelegateHandle* FoundHandle = GameInstanceStartHandles.Find(Context);
	if (ensure(FoundHandle))
	{
		FWorldDelegates::OnStartGameInstance.Remove(*FoundHandle);
	}
	
}

void UGameFeatureAction_WorldActionBase::HandleGameInstanceStart(UGameInstance* GameInstance, FGameFeatureStateChangeContext ChangeContext)
{
	//World（GameInstance）启动时回调
	if (FWorldContext* WorldContext = GameInstance->GetWorldContext())
	{
		if (ChangeContext.ShouldApplyToWorldContext(*WorldContext))
		{
			AddToWorld(*WorldContext, ChangeContext);
		}
	}
}

