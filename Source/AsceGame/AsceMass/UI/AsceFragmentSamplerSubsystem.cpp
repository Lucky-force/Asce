// AsceFragmentSamplerSubsystem.cpp
#include "AsceFragmentSamplerSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "UObject/UObjectIterator.h"

UAsceFragmentSamplerSubsystem* UAsceFragmentSamplerSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	return World ? World->GetSubsystem<UAsceFragmentSamplerSubsystem>() : nullptr;
}

void UAsceFragmentSamplerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Samplers.Reset();
	CursorEntity = FMassEntityHandle();
	CursorType = nullptr;
	bCursorValid = false;
}

void UAsceFragmentSamplerSubsystem::Deinitialize()
{
	for (FAsceEntityFragmentSampler* Sampler : Samplers)
	{
		if (Sampler)
		{
			Sampler->Release();
			delete Sampler;
		}
	}
	Samplers.Reset();
	Super::Deinitialize();
}

FMassEntityManager* UAsceFragmentSamplerSubsystem::GetEntityManager()
{
	if (!MassSubsystem.IsValid())
	{
		UWorld* World = GetWorld();
		MassSubsystem = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	}
	return MassSubsystem.IsValid() ? &MassSubsystem->GetMutableEntityManager() : nullptr;
}

int32 UAsceFragmentSamplerSubsystem::LowerBoundIndex(FMassEntityHandle Entity, const UScriptStruct* FragmentType) const
{
	const uint64 KeyEntity = Entity.AsNumber();
	const UPTRINT KeyType = reinterpret_cast<UPTRINT>(FragmentType);

	int32 First = 0;
	int32 Num = Samplers.Num();
	while (Num > 0)
	{
		const int32 Half = Num / 2;
		const int32 Mid = First + Half;
		const FAsceEntityFragmentSampler* Sampler = Samplers[Mid];
		const uint64 MidEntity = Sampler->GetEntity().AsNumber();
		const UPTRINT MidType = reinterpret_cast<UPTRINT>(Sampler->GetFragmentType());

		const bool bMidLess = (MidEntity != KeyEntity) ? (MidEntity < KeyEntity) : (MidType < KeyType);
		if (bMidLess)
		{
			First = Mid + 1;
			Num -= Half + 1;
		}
		else
		{
			Num = Half;
		}
	}
	return First;
}

FAsceEntityFragmentSampler* UAsceFragmentSamplerSubsystem::FindOrCreateSampler(FMassEntityHandle Entity,
	const UScriptStruct* FragmentType)
{
	if (!Entity.IsSet() || !FragmentType)
	{
		return nullptr;
	}

	const int32 Index = LowerBoundIndex(Entity, FragmentType);
	if (Samplers.IsValidIndex(Index))
	{
		FAsceEntityFragmentSampler* Existing = Samplers[Index];
		if (Existing->GetEntity() == Entity && Existing->GetFragmentType() == FragmentType)
		{
			return Existing;
		}
	}

	FMassEntityManager* EntityManager = GetEntityManager();
	if (!EntityManager || !EntityManager->IsEntityValid(Entity))
	{
		return nullptr;
	}

	FAsceEntityFragmentSampler* NewSampler = new FAsceEntityFragmentSampler();
	if (!NewSampler->Init(*EntityManager, Entity, FragmentType))
	{
		delete NewSampler;
		return nullptr;
	}

	// 插入靠二分定位；键式游标不受插入影响（初始值已由"绑定即广播"给出）
	Samplers.Insert(NewSampler, Index);
	return NewSampler;
}

FDelegateHandle UAsceFragmentSamplerSubsystem::BindField(FMassEntityHandle Entity, const UScriptStruct* FragmentType,
	FName FieldName, const FAsceFragmentFieldChangedDelegate& Delegate)
{
	check(IsInGameThread());
	if (!Delegate.IsBound())
	{
		return FDelegateHandle();
	}

	FAsceFragmentFieldId FieldId;
	if (!FAsceEntityFragmentSampler::ResolveFieldId(FragmentType, FieldName, FieldId))
	{
		UE_LOG(LogTemp, Warning, TEXT("[AsceFragmentSampler] 找不到字段 %s（fragment=%s）"),
			*FieldName.ToString(), FragmentType ? *FragmentType->GetName() : TEXT("null"));
		return FDelegateHandle();
	}

	FAsceEntityFragmentSampler* Sampler = FindOrCreateSampler(Entity, FragmentType);
	if (!Sampler)
	{
		return FDelegateHandle();
	}
	return Sampler->AddDelegate(FieldId, Delegate);
}

void UAsceFragmentSamplerSubsystem::UnbindField(FDelegateHandle Handle)
{
	check(IsInGameThread());
	if (!Handle.IsValid())
	{
		return;
	}

	// sampler 数量级很小（几十），顺序扫即可
	for (FAsceEntityFragmentSampler* Sampler : Samplers)
	{
		if (Sampler)
		{
			Sampler->RemoveDelegate(Handle);
		}
	}
}

const UScriptStruct* UAsceFragmentSamplerSubsystem::FindFragmentTypeByName(FName TypeName)
{
	if (TypeName.IsNone())
	{
		return nullptr;
	}
	if (const TWeakObjectPtr<UScriptStruct>* Cached = FragmentTypeCache.Find(TypeName))
	{
		return Cached->Get();
	}

	// 首次：扫描一遍所有 UScriptStruct（按名字；不依赖模块名，Test/AsceGame 都适用），命中后缓存（含 nullptr）
	UScriptStruct* Found = nullptr;
	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		if (It->GetFName() == TypeName)
		{
			Found = *It;
			break;
		}
	}
	FragmentTypeCache.Add(TypeName, Found);
	return Found;
}

void UAsceFragmentSamplerSubsystem::BroadcastField(FMassEntityHandle Entity, const UScriptStruct* FragmentType,
	FName FieldName)
{
	check(IsInGameThread());

	FAsceFragmentFieldId FieldId;
	if (!FAsceEntityFragmentSampler::ResolveFieldId(FragmentType, FieldName, FieldId))
	{
		UE_LOG(LogTemp, Warning, TEXT("[AsceFragmentSampler] BroadcastField 找不到字段 %s（fragment=%s）"),
			*FieldName.ToString(), FragmentType ? *FragmentType->GetName() : TEXT("null"));
		return;
	}

	const int32 Index = LowerBoundIndex(Entity, FragmentType);
	if (!Samplers.IsValidIndex(Index) || Samplers[Index]->GetEntity() != Entity
		|| Samplers[Index]->GetFragmentType() != FragmentType)
	{
		return; // 没人订阅（sampler 还没建）⇒ 空操作
	}

	Samplers[Index]->BroadcastFieldNow(FieldId);
}

void UAsceFragmentSamplerSubsystem::BroadcastAllFields(FMassEntityHandle Entity, const UScriptStruct* FragmentType)
{
	check(IsInGameThread());

	const int32 Index = LowerBoundIndex(Entity, FragmentType);
	if (!Samplers.IsValidIndex(Index) || Samplers[Index]->GetEntity() != Entity
		|| Samplers[Index]->GetFragmentType() != FragmentType)
	{
		return;
	}

	Samplers[Index]->BroadcastAllBoundFieldsNow();
}

void UAsceFragmentSamplerSubsystem::RunSamplingStage()
{
	check(IsInGameThread());
	if (Samplers.Num() == 0)
	{
		return;
	}

	FMassEntityManager* EntityManager = GetEntityManager();
	if (!EntityManager)
	{
		return;
	}

	// 从游标（或从头）开始，处理最多 Budget 个 sampler
	int32 Index = bCursorValid ? LowerBoundIndex(CursorEntity, CursorType) : 0;
	int32 Processed = 0;
	while (Processed < Budget && Samplers.IsValidIndex(Index))
	{
		FAsceEntityFragmentSampler* Sampler = Samplers[Index];
		++Processed;
		++Index;

		// 广播回调里可能创建新 sampler（数组会插入/重分配）⇒ 每轮重新按下标取，不跨回调持有引用
		if (Sampler && Sampler->HasBoundFields())
		{
			Sampler->CaptureAll();
		}
	}

	if (Index >= Samplers.Num())
	{
		// 本轮走完 ⇒ 回绕：游标置无效 + 统一回收
		bCursorValid = false;
		Sweep(*EntityManager);
	}
	else
	{
		CursorEntity = Samplers[Index]->GetEntity();
		CursorType = Samplers[Index]->GetFragmentType();
		bCursorValid = true;
	}
}

void UAsceFragmentSamplerSubsystem::Sweep(FMassEntityManager& EntityManager)
{
	// 锁外、整遍扫：没人绑（CountSetBits == 0）或源（实体/fragment）已失效的回收
	for (int32 Index = Samplers.Num() - 1; Index >= 0; --Index)
	{
		FAsceEntityFragmentSampler* Sampler = Samplers[Index];
		if (!Sampler)
		{
			Samplers.RemoveAt(Index, 1, EAllowShrinking::No);
			continue;
		}

		const bool bRecyclable = !Sampler->HasBoundFields() || !Sampler->IsSourceValid();
		if (bRecyclable)
		{
			Sampler->Release();
			delete Sampler;
			Samplers.RemoveAt(Index, 1, EAllowShrinking::No);
		}
	}
}
