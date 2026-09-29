// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassProcessor.h"
#include "UObject/SoftObjectPtr.h"

#include "AsceCombatVisualProcessor.generated.h"

struct FMassEntityQuery;
struct FAsceAnimClip;
class UAsceAnimSet;
class UAnimSequence;

/**
 * 动作 → 动画表现 processor（Bone 模式；**无预测版：权威动作态驱动**）。
 *
 * 职责：
 *  - 权威端（standalone/听服 host）读 `FAsceActionFragment`；客户端读复制下来的 `FAsceCombatVisualFragment`；
 *  - 动作边沿（`Counter`/`ActionState` 变化）解析动画资产：`State` → 动画资产软引用
 *    （Idle/Move/Dying 读单位 def；Attack 读单位 def 的 `AttackCastConfig.CastAnim`；Casting/Channel 读技能表 `CastConfig`）
 *    → 再按资产引用在单位 AnimSet 的 `Clips` 里取 `FAsceAnimClip`；**没配就不播**（无回退链，见 `AsceAnimDef.h` / §7.2）；
 *  - 每帧按 `StartTime/FrameRate/PlayRate` 推进帧，写进 MassRepresentation 的 per-instance custom data
 *    （**2 floats，对齐 AnimToTexture 的 FrameData**：`Frame / PrevFrame`；帧为烘焙贴图内的**绝对帧**，
 *    材质用插件自带的 `MF_BoneAnimation` / `GetFrame` 采样）。
 *
 * **顺序与数据对齐（重要，改之前先读）**：
 *  - 与 `UMassUpdateISMProcessor` **同一匹配集、同一实体顺序**（本处理器自己的 fragment 全部 Optional，不改变
 *    Query 匹配；表现/可见性过滤条件与它一致），把 custom float 写进与它 Transform 缓冲相同的序列；
 *  - 跑在 Representation 组内、`UMassVisualizationProcessor` 之后、`UMassUpdateISMProcessor` 之前
 *    （后者在处理末尾会消费/改写 `PrevLODSignificance`；我们镜像"当前范围 + prev 范围"必须在它之前读到旧值）；
 *  - custom data 缓冲每帧由 `EndVisualChanges`（PostPhysics）统一消费并重置。
 *
 * v1 限制：每个 LODSignificance range 只支持 1 个 mesh（多 mesh 的 desc 跳过并告警一次）。
 */
UCLASS()
class UAsceCombatVisualProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UAsceCombatVisualProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	/** 单位类型的动画语义（来自单位 def；软引用不占 GC 生命周期） */
	struct FAsceUnitAnimRefs
	{
		TSoftObjectPtr<UAnimSequence> Idle;
		TSoftObjectPtr<UAnimSequence> Move;
		TSoftObjectPtr<UAnimSequence> Attack;
		TSoftObjectPtr<UAnimSequence> Dying;
	};

	/** 单位类型 → 动画资产（AnimSet + 4 个语义动画）；首次用到时同步加载并缓存 */
	UAsceAnimSet* FindOrLoadAnimSet(uint16 UnitTypeId);

	/** 解析当前动作应播的**动画资产**（Idle/Move/Dying 读单位 def；Attack 读 `AttackCastConfig.CastAnim`；Casting/Channel 读技能表 `CastConfig`；空 = 不播） */
	TSoftObjectPtr<UAnimSequence> ResolveClipRef(uint8 ActionState, int32 ActionDefId, uint16 UnitTypeId) const;

	/** 按资产引用在 AnimSet 的 Clips 里取段；找不到返回 null */
	const FAsceAnimClip* ResolveClip(UAsceAnimSet* AnimSet, const TSoftObjectPtr<UAnimSequence>& ClipRef) const;

	/** 该动作状态是否循环播放（Idle/Move/Channel 循环、Attack/Casting/Dying 一次性） */
	static bool IsLoopingState(uint8 ActionState);

	FMassEntityQuery EntityQuery;

	/** AnimSet 硬引用缓存（防 GC；处理器实例由 Mass 阶段管理器持有） */
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UAsceAnimSet>> AnimSetCache;

	/** 单位类型 → 语义动画软引用（`FindOrLoadAnimSet` 填充；软引用不参与 GC） */
	mutable TMap<int32, FAsceUnitAnimRefs> AnimRefCache;

	/** 诊断防刷屏：每种"单位类型+状态"的动画解析失败只提示一次（没配 / 不在 AnimSet.Clips 里） */
	TSet<int32> WarnedAnimMisses;

	/** 诊断防刷屏：单位类型缺 AnimSet / 表行查不到 / Def 类型不对 只提示一次 */
	TSet<int32> WarnedMissingAnimSets;

	/** 诊断防刷屏：每种"单位类型+状态"解析成功只提示一次（用来判断"数据侧 OK、画面不动 ⇒ 渲染接线问题"） */
	TSet<int32> LoggedAnimHits;

	/** 诊断防刷屏：每种"单位类型+状态"约每 0.5s 打一次实际写入的帧号（确认 custom data 在推进） */
	TMap<int32, float> LastLoggedFrames;
};
