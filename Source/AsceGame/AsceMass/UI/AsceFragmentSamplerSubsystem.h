// AsceFragmentSamplerSubsystem.h
#pragma once

#include "CoreMinimal.h"
#include "AsceMass/UI/AsceEntityFragmentSampler.h"
#include "Subsystems/WorldSubsystem.h"
#include "AsceFragmentSamplerSubsystem.generated.h"

class UMassEntitySubsystem;
class UScriptStruct;
struct FMassEntityManager;

/**
 * 字段采样调度器 / 注册表（对应 `UI.md` §3）。
 *
 * - **唯一真相 = 一个有序数组**：按 `(EntityHandle, FragmentType)` 排序，二级键用指针地址（只为把同一个 key
 *   的 sampler 放一起，不要求跨运行稳定）；查找用二分，实体销毁时按 Handle 段批量处理，没有字典。
 * - **键式游标 + 常量预算**：每帧只处理 `Budget` 个 sampler；扫到数组末尾 ⇒ 游标置无效 + 做 sweep。
 * - **sweep**（回绕时、锁外、整遍扫）：回收 `BitNumberArray.CountSetBits() == 0` 或"实体/fragment 已失效"的 sampler。
 * - **只在主线程**：所有入口 `check(IsInGameThread())`；由 `UAsceSimulationSubsystem` 的最后一个阶段驱动 `RunSamplingStage()`。
 * - 遍历期间不跨回调持有元素引用（回调可能创建新 sampler ⇒ 数组重分配）；插入导致的索引漂移只会"重复采一次"（幂等）。
 *
 * [TODO 待定] 订阅 API 给 AngelScript/Blueprint 用什么形式：目前是 C++ 的 `FAsceFragmentFieldChangedDelegate`
 * （非 dynamic delegate，脚本层不能直接绑）。需要一个 dynamic delegate 包装/转发层，形式待定。
 */
UCLASS()
class UAsceFragmentSamplerSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	static UAsceFragmentSamplerSubsystem* Get(const UObject* WorldContextObject);

	/**
	 * 绑定某个实体某个 fragment 的某个字段：
	 * 首次绑定该字段时会建影子并立刻广播一次当前值（绑定即广播）；成功返回解绑句柄。
	 */
	FDelegateHandle BindField(FMassEntityHandle Entity, const UScriptStruct* FragmentType, FName FieldName,
		const FAsceFragmentFieldChangedDelegate& Delegate);

	/** 解绑（O(sampler 数)，量级很小）。 */
	void UnbindField(FDelegateHandle Handle);

	/** 按名字找 fragment 的 UScriptStruct（首次扫描一次并缓存；给"标识自动绑定"解析 `类型名`用）。 */
	const UScriptStruct* FindFragmentTypeByName(FName TypeName);

	/**
	 * **显式广播**：不管字段值变没变，直接通知该字段的所有绑定者。
	 * 给"数据在外部系统、fragment 上只存 id"的场景用（如 Ether 建筑：NodeId 没变但节点数据变了 ⇒
	 * 主动推一次，让订阅者重新去外部系统取）。**没有 sampler（没人订阅）时是空操作**。
	 */
	void BroadcastField(FMassEntityHandle Entity, const UScriptStruct* FragmentType, FName FieldName);

	/** 同上，但下推该 (实体, fragment) 上**所有有绑定者的字段**。 */
	void BroadcastAllFields(FMassEntityHandle Entity, const UScriptStruct* FragmentType);

	/** 每帧采样入口（由 `UAsceSimulationSubsystem` 最后阶段调用）。 */
	void RunSamplingStage();

	int32 GetSamplerCount() const { return Samplers.Num(); }

	/** 每帧最多处理多少个 sampler（同频轮转；一圈 ≈ Samplers / Budget 帧）。 */
	UPROPERTY(EditAnywhere, Category = "Asce|FragmentSampler")
	int32 Budget = 64;

private:
	/** 第一个"不小于 (Entity, FragmentType)" 的位置（二分） */
	int32 LowerBoundIndex(FMassEntityHandle Entity, const UScriptStruct* FragmentType) const;
	/** 命中即复用；否则校验 fragment 类型后创建并插入（位置由二分决定，插入对键式游标无影响） */
	FAsceEntityFragmentSampler* FindOrCreateSampler(FMassEntityHandle Entity, const UScriptStruct* FragmentType);
	/** 回绕时整遍扫：回收没人绑 / 源已失效的 sampler */
	void Sweep(FMassEntityManager& EntityManager);
	FMassEntityManager* GetEntityManager();

private:
	/** 按 (Entity.AsNumber(), FragmentType 指针) 排序 */
	TArray<FAsceEntityFragmentSampler*> Samplers;

	/** 键式游标：下个待处理的 sampler key；bCursorValid=false 表示下帧从头开始 */
	FMassEntityHandle CursorEntity;
	const UScriptStruct* CursorType = nullptr;
	bool bCursorValid = false;

	TWeakObjectPtr<UMassEntitySubsystem> MassSubsystem;
	/** 类型名 → UScriptStruct（"标识自动绑定"解析用；首次扫描后缓存） */
	TMap<FName, TWeakObjectPtr<UScriptStruct>> FragmentTypeCache;
};
