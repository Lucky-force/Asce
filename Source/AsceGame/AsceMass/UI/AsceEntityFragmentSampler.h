// AsceEntityFragmentSampler.h
#pragma once

#include "CoreMinimal.h"
#include "Containers/BitArray.h"
#include "Delegates/Delegate.h"
#include "MassEntityHandle.h"

class FProperty;
class UScriptStruct;
struct FMassEntityManager;

/**
 * 字段标识（对应 `UI.md` 的 `FFragmentFieldId`）。
 * - 比较/排序**只看 FieldName**；
 * - `BitNumber` = 该字段在 fragment 内的**属性枚举序号**，也是 `FieldArray` / `BitNumberArray` 的下标；
 *   仅"进程内 + 同一 UScriptStruct 生命周期内"稳定 ⇒ 配置/UI 引用字段一律用**名字**，绑定时解析。
 */
struct FAsceFragmentFieldId
{
	FName FieldName;
	int32 BitNumber = INDEX_NONE;

	bool operator==(const FAsceFragmentFieldId& Other) const { return Other.FieldName == FieldName; }
	bool operator!=(const FAsceFragmentFieldId& Other) const { return !(*this == Other); }
	bool IsValid() const { return !FieldName.IsNone() && BitNumber != INDEX_NONE; }
};

/**
 * 字段变化通知（对应 `UI.md` 的 `FFragmentFieldValueChangedDelegate`）。
 * - 第 1 个参数 `int64 EntityNumber` = `FMassEntityHandle::AsNumber()`：绑定者用**自己**去 entity 上取想要的值
 *   （比如"生命变了要算百分比"就连带监听最大生命，任一变化都直接重新读两个字段再算）——**sampler 不返回真实值**。
 *   用 int64 而不是 `FMassEntityHandle`：和本项目 AS/BP 的既有约定一致（`FMassEntityHandle` 不是 BlueprintType），
 *   C++ 侧用 `FMassEntityHandle::FromNumber(EntityNumber)` 还原；
 * - 第 2 个参数 `FName` = 变化的字段名（BitNumber 是内部概念，不往外传）；
 * - 签名固定 `(int64, FName)`：这样普通 `TDelegate` 也能用 `CreateUFunction` 直接调 AS/BP 的 UFUNCTION
 *   （见 `UAsceFieldBindingWidget` 的 `AsceBind` 标识自动绑定）；
 * - 不带"绑定者"参数：绑定者身份由 delegate 自身（`GetUObject()` / `IsBoundToObject`）
 *   或 `BindField` 返回的 `FDelegateHandle` 标识。
 */
using FAsceFragmentFieldChangedDelegate =
	TDelegate<void(int64, FName), FNotThreadSafeNotCheckedDelegateUserPolicy>;

/** 每字段一份影子副本（对应 `UI.md` 的 `FCacheField`）：POD 位拷贝，不是引用、不参与 GC。 */
struct FAsceFragmentFieldCache
{
	FProperty* Property = nullptr;
	void* Buffer = nullptr;
};

/** 绑定项（对应 `UI.md` 的 `BindElement`）。 */
struct FAsceFragmentFieldBind
{
	FAsceFragmentFieldId FieldId;
	FAsceFragmentFieldChangedDelegate Delegate;
};

/**
 * 一个 `(EntityHandle, FragmentType)` 的字段采样器（key 唯一性由持有者 = `UAsceFragmentSamplerSubsystem` 保证）。
 *
 * 不变量（照 `UE::FieldNotification::FFieldMulticastDelegate`）：
 *   BindElements[0, AddedEmplaceAt)      = 有序区（二分只搜这一段）
 *   BindElements[AddedEmplaceAt, Num)    = 广播期间新增的**暂存区**（无序，可能已解绑）
 *   BindLock > 0：新增追加到尾部（并把 AddedEmplaceAt 取下界）、解绑只 Unbind + ++CompactionCount；
 *   BindLock 归零后 ExecuteLockOperations：先压实（CompactionCount > 2 才做）、再把尾部逐个归位。
 *
 * 约定：
 * - **只支持 trivially copyable 的 fragment**（`Init` 校验 `STRUCT_IsPlainOldData`，不合规拒绝创建）；
 * - 影子是**纯值快照**（位比较判变化；不进 GC、不解引用）；
 * - 采样/广播**只能在主线程**，且**不得跨广播持有 fragment 指针**（回调里可能 demolish 实体）。
 */
struct FAsceEntityFragmentSampler
{
public:
	// ── 生命周期 ──

	/** 校验 fragment 类型 + 建字段表/影子 buffer；失败（非 trivially copyable）返回 false，不创建。 */
	bool Init(FMassEntityManager& InEntityManager, FMassEntityHandle InEntity, const UScriptStruct* InFragmentType);

	/** 释放所有影子 buffer 与绑定表。 */
	void Release();

	// ── 采样（主线程） ──

	/** 字段第一次被绑定：**只建影子**（不比较、不广播）。 */
	void CaptureInit(const FAsceFragmentFieldId& FieldId);

	/** 采一个字段：值与影子不同才更新影子并广播；广播后不再使用 fragment 指针。 */
	void Capture(const FAsceFragmentFieldId& FieldId);

	/** 周期采样入口：遍历所有"有绑定者"的字段逐个 Capture。 */
	void CaptureAll();

	/**
	 * **无条件下推**某字段（不管值变没变）：给"数据在外部系统、fragment 上只存 id"的场景用
	 * （比如 Ether 建筑的节点数据由 Ether 网络持有，`FEtherBuildingFragment` 只存 NodeId ⇒
	 *  外部数据变了就主动推一次，让订阅者重新去外部系统取）。
	 * 会先把影子与实时对齐（能读到 fragment 时），避免下一次采样因为"外部已改"再重复广播一次。
	 * 该字段没有绑定者时直接返回（无意义，也避免白建影子）。
	 */
	void BroadcastFieldNow(const FAsceFragmentFieldId& FieldId);

	/** **无条件下推**该 sampler 上所有"有绑定者"的字段（同上）。 */
	void BroadcastAllBoundFieldsNow();

	// ── 绑定 ──

	/** 加一个绑定（含"绑定即广播"：新增委托当场收到该字段当前值）；返回解绑句柄。 */
	FDelegateHandle AddDelegate(const FAsceFragmentFieldId& FieldId,
		const FAsceFragmentFieldChangedDelegate& Delegate);

	/** 解绑；最后一个绑定离开时清 `BitNumberArray` 位（判据同时看有序区与暂存区）。 */
	void RemoveDelegate(FDelegateHandle Handle);

	// ── 取值 / 查询 ──

	FMassEntityHandle GetEntity() const { return Entity; }
	const UScriptStruct* GetFragmentType() const { return FragmentType; }
	/** 是否还有字段被绑定（= 是否还需要采样 / 是否可回收）。 */
	bool HasBoundFields() const { return BitNumberArray.CountSetBits() > 0; }
	/** 实体/fragment 是否还在（sweep 时用；不在就回收）。 */
	bool IsSourceValid() const;

	/** 字段名 → FieldId（绑定时遍历一次解析；不做目录缓存）。 */
	static bool ResolveFieldId(const UScriptStruct* FragmentType, FName FieldName, FAsceFragmentFieldId& OutFieldId);

private:
	/** 有序区里找"第一个 > FieldId"的位置（二分范围 = [0, AddedEmplaceAt)）。 */
	int32 UpperBound(const FAsceFragmentFieldId& FieldId) const;
	/** 广播（有序区 + 暂存区两段）。 */
	void Broadcast(const FAsceFragmentFieldId& FieldId);
	/** BindLock 归零后：压实 + 暂存区归位。 */
	void ExecuteLockOperations();
	/** 该字段在有序区或暂存区是否还有"已绑定"的委托。 */
	bool HasOtherBoundDelegate(const FAsceFragmentFieldId& FieldId) const;

private:
	FMassEntityManager* EntityManager = nullptr;
	FMassEntityHandle Entity;
	const UScriptStruct* FragmentType = nullptr;

	/** index == BitNumber */
	TArray<FAsceFragmentFieldCache> FieldArray;
	/** 位 = 该字段当前是否有绑定者（立即置位/清位） */
	TBitArray<> BitNumberArray;
	/** 有序区 + 暂存区 */
	TArray<FAsceFragmentFieldBind> BindElements;

	int32 AddedEmplaceAt = MAX_int32;
	/** 广播锁（计数，支持嵌套广播） */
	int32 BindLock = 0;
	/** 广播期间解绑的待压实计数 */
	int32 CompactionCount = 0;
};
