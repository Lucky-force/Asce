#include "AsceCombatSet.h"

#include "AbilitySystem/Attributes/AsceAttributeSet.h"
#include "Net/UnrealNetwork.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceCombatSet)

class FLifetimeProperty;


UAsceCombatSet::UAsceCombatSet()
	: BaseDamage(0.0f)
	, BaseHeal(0.0f)
{
}

void UAsceCombatSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(UAsceCombatSet, BaseDamage, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAsceCombatSet, BaseHeal, COND_OwnerOnly, REPNOTIFY_Always);
}

void UAsceCombatSet::OnRep_BaseDamage(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAsceCombatSet, BaseDamage, OldValue);
	//服务器会直接复制并修改值
	//这里通过这种方式调用让ASC重新更新一遍值，此时就会除非PreBaseValueChange，PostBaseValueChange这些函数了
}

void UAsceCombatSet::OnRep_BaseHeal(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAsceCombatSet, BaseHeal, OldValue);
}

