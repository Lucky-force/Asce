// AsceEntityFragmentSampler.cpp
#include "AsceEntityFragmentSampler.h"

#include "MassEntityManager.h"
#include "MassEntityView.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

bool FAsceEntityFragmentSampler::Init(FMassEntityManager& InEntityManager, FMassEntityHandle InEntity,
	const UScriptStruct* InFragmentType)
{
	// P0-1：只支持 trivially copyable 的 fragment（影子靠位比较判变化，非平凡类型会破坏引用计数/漏广播）
	if (!InFragmentType || (InFragmentType->StructFlags & STRUCT_IsPlainOldData) == 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[AsceFragmentSampler] 拒绝创建：fragment %s 不是 trivially copyable（缺 STRUCT_IsPlainOldData）"),
			InFragmentType ? *InFragmentType->GetName() : TEXT("null"));
		return false;
	}

	EntityManager = &InEntityManager;
	Entity = InEntity;
	FragmentType = InFragmentType;

	// 字段表：index（= BitNumber）= 属性枚举序号
	int32 NumProperties = 0;
	for (TFieldIterator<FProperty> It(FragmentType); It; ++It)
	{
		FAsceFragmentFieldCache CacheField;
		CacheField.Property = *It;
		// 只 Malloc 不 Memset：影子第一次由 CaptureInit 从实时值整段拷贝建立（连 padding 一起，避免假变化）
		CacheField.Buffer = FMemory::Malloc(It->GetSize(), It->GetMinAlignment());
		FieldArray.Add(CacheField);
		++NumProperties;
	}
	BitNumberArray.Init(false, NumProperties);

	AddedEmplaceAt = MAX_int32;
	BindLock = 0;
	CompactionCount = 0;
	return true;
}

void FAsceEntityFragmentSampler::Release()
{
	for (int32 Index = 0; Index < FieldArray.Num(); ++Index)
	{
		FMemory::Free(FieldArray[Index].Buffer);
		FieldArray[Index].Property = nullptr;
		FieldArray[Index].Buffer = nullptr;
	}
	FieldArray.Empty();
	BitNumberArray.Empty();
	BindElements.Empty();
	AddedEmplaceAt = MAX_int32;
	BindLock = 0;
	CompactionCount = 0;
	EntityManager = nullptr;
	FragmentType = nullptr;
}

// ── 采样 ──

void FAsceEntityFragmentSampler::CaptureInit(const FAsceFragmentFieldId& FieldId)
{
	if (!EntityManager || !FieldArray.IsValidIndex(FieldId.BitNumber))
	{
		return;
	}

	// 只读一次 fragment；建完影子就出去（无比较、无广播 ⇒ 不会重入）
	const FMassEntityView View = FMassEntityView::TryMakeView(*EntityManager, Entity);
	if (!View.IsSet())
	{
		return;
	}
	const FStructView FragmentView = View.GetFragmentDataStruct(FragmentType);
	if (!FragmentView.IsValid())
	{
		return;
	}

	FAsceFragmentFieldCache& CacheField = FieldArray[FieldId.BitNumber];
	FMemory::Memcpy(
		CacheField.Buffer,
		CacheField.Property->ContainerPtrToValuePtr<void>(FragmentView.GetMemory()),
		CacheField.Property->GetSize());
}

void FAsceEntityFragmentSampler::Capture(const FAsceFragmentFieldId& FieldId)
{
	if (!EntityManager || !FieldId.IsValid() || !FieldArray.IsValidIndex(FieldId.BitNumber))
	{
		return;
	}
	if (!BitNumberArray[FieldId.BitNumber])
	{
		return; // 该字段没人绑
	}

	const FMassEntityView View = FMassEntityView::TryMakeView(*EntityManager, Entity);
	if (!View.IsSet())
	{
		return; // 实体/fragment 已失效（sweep 时候回收）
	}
	const FStructView FragmentView = View.GetFragmentDataStruct(FragmentType);
	if (!FragmentView.IsValid())
	{
		return;
	}

	FAsceFragmentFieldCache& CacheField = FieldArray[FieldId.BitNumber];
	void* FieldPtr = CacheField.Property->ContainerPtrToValuePtr<void>(FragmentView.GetMemory());
	const int32 Size = CacheField.Property->GetSize();

	if (FMemory::Memcmp(FieldPtr, CacheField.Buffer, Size) == 0)
	{
		return; // 没变，啥也不干
	}

	FMemory::Memcpy(CacheField.Buffer, FieldPtr, Size);
	// 广播之后**不再使用 FieldPtr / FragmentView**（回调里可能 demolish 实体、换 fragment）
	Broadcast(FieldId);
}

void FAsceEntityFragmentSampler::CaptureAll()
{
	if (!HasBoundFields())
	{
		return;
	}

	for (int32 Bit = 0; Bit < BitNumberArray.Num(); ++Bit)
	{
		if (!BitNumberArray[Bit])
		{
			continue;
		}

		FAsceFragmentFieldId FieldId;
		FieldId.FieldName = FieldArray[Bit].Property->GetFName();
		FieldId.BitNumber = Bit;

		// 每个字段各自重新取一次 View（不缓存 fragment 指针）
		Capture(FieldId);

		if (!HasBoundFields())
		{
			return; // 广播里可能把所有绑定都解掉了
		}
	}
}

void FAsceEntityFragmentSampler::BroadcastFieldNow(const FAsceFragmentFieldId& FieldId)
{
	if (!FieldId.IsValid() || !FieldArray.IsValidIndex(FieldId.BitNumber))
	{
		return;
	}
	if (!BitNumberArray[FieldId.BitNumber])
	{
		return; // 没人绑这个字段 ⇒ 下推没有意义（也别去白建影子）
	}

	// 影子对齐：外部系统改了"关联数据"时字段值可能没变，这次先对齐，避免下一轮采样又广播一次
	CaptureInit(FieldId);

	// 无条件广播（不看 diff）
	Broadcast(FieldId);
}

void FAsceEntityFragmentSampler::BroadcastAllBoundFieldsNow()
{
	for (int32 Bit = 0; Bit < BitNumberArray.Num(); ++Bit)
	{
		if (!BitNumberArray[Bit])
		{
			continue;
		}

		FAsceFragmentFieldId FieldId;
		FieldId.FieldName = FieldArray[Bit].Property->GetFName();
		FieldId.BitNumber = Bit;
		BroadcastFieldNow(FieldId);

		if (!HasBoundFields())
		{
			return; // 广播里可能全解绑了
		}
	}
}

// ── 绑定 ──

FDelegateHandle FAsceEntityFragmentSampler::AddDelegate(const FAsceFragmentFieldId& FieldId,
	const FAsceFragmentFieldChangedDelegate& Delegate)
{
	if (!FieldId.IsValid() || !FieldArray.IsValidIndex(FieldId.BitNumber) || !Delegate.IsBound())
	{
		return FDelegateHandle();
	}

	FAsceFragmentFieldBind NewBind;
	NewBind.FieldId = FieldId;
	NewBind.Delegate = Delegate;
	const FDelegateHandle Handle = Delegate.GetHandle();

	if (BindLock > 0)
	{
		// 广播中：先追加到暂存区（等解锁后归位）
		const int32 Index = BindElements.Emplace(MoveTemp(NewBind));
		AddedEmplaceAt = FMath::Min(AddedEmplaceAt, Index);
	}
	else
	{
		const int32 FoundIndex = UpperBound(FieldId);
		BindElements.EmplaceAt(FoundIndex, MoveTemp(NewBind));
	}

	// 字段第一次有绑定者 ⇒ 建影子
	if (!BitNumberArray[FieldId.BitNumber])
	{
		BitNumberArray[FieldId.BitNumber] = true;
		CaptureInit(FieldId);
	}

	// 绑定即广播：委托已在表里 ⇒ 新订阅者当场拿到该字段当前值
	Broadcast(FieldId);
	return Handle;
}

void FAsceEntityFragmentSampler::RemoveDelegate(FDelegateHandle Handle)
{
	if (!Handle.IsValid())
	{
		return;
	}

	int32 Index = INDEX_NONE;
	for (int32 i = 0; i < BindElements.Num(); ++i)
	{
		if (BindElements[i].Delegate.GetHandle() == Handle)
		{
			Index = i;
			break;
		}
	}
	if (Index == INDEX_NONE)
	{
		return;
	}

	const FAsceFragmentFieldId FieldId = BindElements[Index].FieldId;
	if (BindLock > 0)
	{
		// 广播中：只解绑，元素留着（等解锁后一起压实）
		BindElements[Index].Delegate.Unbind();
		++CompactionCount;
	}
	else
	{
		BindElements.RemoveAt(Index, 1, EAllowShrinking::No);
		if (AddedEmplaceAt != MAX_int32 && Index < AddedEmplaceAt)
		{
			--AddedEmplaceAt;
		}
	}

	// 有序区 + 暂存区都没有该字段的"已绑定"委托了 ⇒ 清位
	if (!HasOtherBoundDelegate(FieldId))
	{
		BitNumberArray[FieldId.BitNumber] = false;
	}
}

// ── 查询 ──

bool FAsceEntityFragmentSampler::IsSourceValid() const
{
	if (!EntityManager || !FragmentType || !EntityManager->IsEntityValid(Entity))
	{
		return false;
	}
	const FMassEntityView View = FMassEntityView::TryMakeView(*EntityManager, Entity);
	return View.IsSet() && View.GetFragmentDataStruct(FragmentType).IsValid();
}

bool FAsceEntityFragmentSampler::ResolveFieldId(const UScriptStruct* FragmentType, FName FieldName,
	FAsceFragmentFieldId& OutFieldId)
{
	if (!FragmentType || FieldName.IsNone())
	{
		return false;
	}

	// 不做目录缓存：绑定时遍历一次（顺序必须与 Init 建 FieldArray 时完全一致）
	int32 BitNumber = 0;
	for (TFieldIterator<FProperty> It(FragmentType); It; ++It, ++BitNumber)
	{
		if (It->GetFName() == FieldName)
		{
			OutFieldId.FieldName = FieldName;
			OutFieldId.BitNumber = BitNumber;
			return true;
		}
	}
	return false;
}

// ── 内部：二分 / 广播 / 压实 ──

int32 FAsceEntityFragmentSampler::UpperBound(const FAsceFragmentFieldId& FieldId) const
{
	// 二分范围 = 有序区 [0, AddedEmplaceAt)
	int32 Num = FMath::Min(AddedEmplaceAt, BindElements.Num());
	int32 First = 0;
	while (Num > 0)
	{
		const int32 Half = Num / 2;
		const int32 Mid = First + Half;
		if (FieldId.FieldName.FastLess(BindElements[Mid].FieldId.FieldName))
		{
			Num = Half; // 该元素 > FieldId ⇒ 往前找
		}
		else
		{
			First = Mid + 1;
			Num -= Half + 1;
		}
	}
	return First;
}

bool FAsceEntityFragmentSampler::HasOtherBoundDelegate(const FAsceFragmentFieldId& FieldId) const
{
	// 有序区 + 暂存区一起扫（暂存区的字段名不保证有序，不能只做二分）
	for (const FAsceFragmentFieldBind& Bind : BindElements)
	{
		if (Bind.FieldId == FieldId && Bind.Delegate.IsBound())
		{
			return true;
		}
	}
	return false;
}

void FAsceEntityFragmentSampler::Broadcast(const FAsceFragmentFieldId& FieldId)
{
	++BindLock;

	// ① 有序区：从 UpperBound-1 往前扫，遇到不同 FieldId 就停
	const int32 FoundIndex = UpperBound(FieldId) - 1;
	if (BindElements.IsValidIndex(FoundIndex))
	{
		for (int32 Index = FoundIndex; Index >= 0; --Index)
		{
			FAsceFragmentFieldBind& Bind = BindElements[Index];
			if (Bind.FieldId != FieldId)
			{
				break;
			}
			if (Bind.Delegate.IsBound())
			{
				Bind.Delegate.Execute(static_cast<int64>(Entity.AsNumber()), Bind.FieldId.FieldName);
			}
		}
	}

	// ② 暂存区（广播期间新增的也收到本次）：从末尾倒序扫到 AddedEmplaceAt
	for (int32 Index = BindElements.Num() - 1; Index >= AddedEmplaceAt; --Index)
	{
		FAsceFragmentFieldBind& Bind = BindElements[Index];
		if (Bind.FieldId == FieldId && Bind.Delegate.IsBound())
		{
			Bind.Delegate.Execute(static_cast<int64>(Entity.AsNumber()), Bind.FieldId.FieldName);
		}
	}

	--BindLock;
	ExecuteLockOperations();
}

void FAsceEntityFragmentSampler::ExecuteLockOperations()
{
	if (BindLock > 0)
	{
		return;
	}

	// ① 压实：广播期间解绑的元素摘掉（阈值 > 2 才做，减少 memmove）
	if (CompactionCount > 2)
	{
		for (int32 Index = BindElements.Num() - 1; Index >= 0; --Index)
		{
			if (!BindElements[Index].Delegate.IsBound())
			{
				BindElements.RemoveAt(Index, 1, EAllowShrinking::No);
				if (AddedEmplaceAt != MAX_int32 && Index < AddedEmplaceAt)
				{
					--AddedEmplaceAt;
				}
			}
		}
		CompactionCount = 0;
	}

	// ② 暂存区归位：已绑的二分插回有序区，未绑的直接丢
	for (; AddedEmplaceAt < BindElements.Num(); )
	{
		if (BindElements[AddedEmplaceAt].Delegate.IsBound())
		{
			const int32 FoundIndex = UpperBound(BindElements[AddedEmplaceAt].FieldId);
			if (FoundIndex != AddedEmplaceAt)
			{
				FAsceFragmentFieldBind Bind = MoveTemp(BindElements[AddedEmplaceAt]);
				BindElements.RemoveAtSwap(AddedEmplaceAt, 1, EAllowShrinking::No);
				BindElements.EmplaceAt(FoundIndex, MoveTemp(Bind));
			}
			++AddedEmplaceAt;
		}
		else
		{
			BindElements.RemoveAtSwap(AddedEmplaceAt, 1, EAllowShrinking::No);
		}
	}
	AddedEmplaceAt = MAX_int32;
}
