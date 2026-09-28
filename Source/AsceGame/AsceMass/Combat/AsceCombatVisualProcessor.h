// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassProcessor.h"

#include "AsceCombatVisualProcessor.generated.h"

struct FMassEntityQuery;
struct FAsceAnimClip;
class UAsceAnimSet;

/**
 * 动作 → 动画表现 processor（Bone 模式；**无预测版：权威动作态驱动**）。
 *
 * 职责：
 *  - 权威端（standalone/听服 host）读 `FAsceActionFragment`；客户端读复制下来的 `FAsceCombatVisualFragment`；
 *  - 动作边沿（`Counter`/`ActionState` 变化）解析 clip：`(单位类型 AnimSet, ActionDefId, State)` → `FAsceAnimClip`
 *    （含回退链，见 `AsceAnimDef.h` / `技能系统设计.md` §7）；
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
	/** 单位类型 → 动画资产（AnimSet + 攻击 clip 名）；首次用到时同步加载并缓存 */
	UAsceAnimSet* FindOrLoadAnimSet(uint16 UnitTypeId);

	/** 解析当前动作应播的语义 clip 名（技能优先读动作表 AnimClips / CastConfig，其次保留名） */
	FName ResolveClipName(uint8 ActionState, int32 ActionDefId, uint16 UnitTypeId) const;

	/** 从 AnimSet 里按 clip 名 + 回退链取段；找不到返回 null */
	const FAsceAnimClip* ResolveClip(UAsceAnimSet* AnimSet, FName ClipName, uint8 ActionState, bool& bOutLoop) const;

	/** 攻击 clip 名缓存（单位 def 的 AttackCastConfig.CastAnim；空 = 保留名 Attack） */
	FName FindAttackClipName(uint16 UnitTypeId) const;

	FMassEntityQuery EntityQuery;

	/** AnimSet 硬引用缓存（防 GC；处理器实例由 Mass 阶段管理器持有） */
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UAsceAnimSet>> AnimSetCache;

	/** 单位类型 → 攻击 clip 名（空 = Attack） */
	mutable TMap<int32, FName> AttackClipCache;

	/** 多 mesh desc 告警只打一次 */
	bool bWarnedMultiMesh = false;
};
