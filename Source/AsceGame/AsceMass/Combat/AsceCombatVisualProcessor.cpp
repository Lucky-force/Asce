// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceCombatVisualProcessor.h"

#include "Engine/World.h"
#include "MassCommonFragments.h"
#include "MassEntityManager.h"
#include "MassEntityQuery.h"
#include "MassExecutionContext.h"
#include "MassLODFragments.h"             // FMassVisualizationChunkFragment
#include "MassRepresentationFragments.h"  // FMassRepresentationFragment/LOD/SubsystemSharedFragment
#include "MassRepresentationProcessor.h"  // UMassVisualizationProcessor（排序）
#include "MassRepresentationSubsystem.h"
#include "MassRepresentationTypes.h"      // FMassInstancedStaticMeshInfo / FMassLODSignificanceRange
#include "MassUpdateISMProcessor.h"       // UMassUpdateISMProcessor（排序/对齐）

#include "AsceMass/Combat/AsceActionDefLibrary.h"
#include "AsceMass/Combat/AsceAnimDef.h"
#include "AsceMass/Combat/AsceCombatDef.h"
#include "AsceMass/Combat/AsceCombatFragments.h"
#include "AsceMass/Data/AsceEntityDefLibrary.h"
#include "AsceMass/Data/AsceUnitDef.h"
#include "AsceMass/Fragments/AsceCoreFragments.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceCombatVisualProcessor)

namespace
{
	/** per-instance custom data = AnimToTexture 的 `FAnimToTextureFrameData` 布局（2 floats：Frame, PrevFrame）。
	 *  不直接引 AnimToTexture 模块类型，只保证 sizeof == 2*float（`AddBatchedCustomData` 的 static_assert 要求）。 */
	struct FAsceAnimFrameData
	{
		float Frame = 0.f;
		float PrevFrame = 0.f;
	};
	static_assert(sizeof(FAsceAnimFrameData) == 2 * sizeof(float), "custom data 必须正好 2 个 float");
}

UAsceCombatVisualProcessor::UAsceCombatVisualProcessor()
	: EntityQuery(*this)
{
	// 表现层处理器：走引擎标准处理链（可视化处理器都在那里）
	bAutoRegisterWithProcessingPhases = true;
	// 与 UMassUpdateISMProcessor 一致：Client | Standalone（听服 = Client|Server，两标志都会匹配）
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Standalone);
	bRequiresGameThreadExecution = true;

	// 顺序：Representation 组内、UMassVisualizationProcessor 之后（表现类型/LOD 已算完），
	// 且在 UMassUpdateISMProcessor 之前（它会在末尾消费/改写 PrevLODSignificance，我们镜像范围要先读旧值）。
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::Representation;
	ExecutionOrder.ExecuteAfter.Add(UMassVisualizationProcessor::StaticClass()->GetFName());
	// 必须显式声明"在 UMassUpdateISMProcessor 之前"：它会在处理末尾消费/改写 PrevLODSignificance，
	// 我们镜像"当前范围 + prev 范围"必须先读到旧值（仅靠 ExecuteAfter 不保证同组内的相对顺序）
	ExecutionOrder.ExecuteBefore.Add(UMassUpdateISMProcessor::StaticClass()->GetFName());
}

void UAsceCombatVisualProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.RegisterWithProcessor(*this);

	// ⚠️ 必须与 UMassUpdateISMProcessor 的匹配集/过滤条件一致（实体顺序对齐 custom data 缓冲）
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassRepresentationFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassRepresentationLODFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddChunkRequirement<FMassVisualizationChunkFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.SetChunkFilter(&FMassVisualizationChunkFragment::AreAnyEntitiesVisibleInChunk);
	EntityQuery.AddSharedRequirement<FMassRepresentationSubsystemSharedFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddTagRequirement<FMassStaticRepresentationTag>(EMassFragmentPresence::None);

	// 我们自己的数据：**全部 Optional**（不改变匹配集；缺失时写中立 custom data 保持缓冲对齐）
	EntityQuery.AddRequirement<FAsceActionFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	EntityQuery.AddRequirement<FAsceCombatVisualFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	EntityQuery.AddRequirement<FAsceUnitDefFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	EntityQuery.AddRequirement<FAsceCombatFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	EntityQuery.AddRequirement<FAsceAnimPlaybackFragment>(EMassFragmentAccess::ReadWrite, EMassFragmentPresence::Optional);
}

UAsceAnimSet* UAsceCombatVisualProcessor::FindOrLoadAnimSet(const uint16 UnitTypeId)
{
	const int32 Key = static_cast<int32>(UnitTypeId);
	if (const TObjectPtr<UAsceAnimSet>* Found = AnimSetCache.Find(Key))
	{
		return *Found;
	}

	UAsceAnimSet* AnimSet = nullptr;
	FAsceUnitAnimRefs Refs;

	// 按数字类型 id 查实体定义表行（见 `FAsceUnitDefFragment` / 实体定义表约定）
	FAsceEntityDefRow Row;
	if (UAsceEntityDefLibrary::GetEntityDefRow(Key, Row))
	{
		if (const FAsceRTSUnitDef* UnitDef = Row.Def.GetPtr<FAsceRTSUnitDef>())
		{
			AnimSet = UnitDef->AnimSet.LoadSynchronous();
			Refs.Idle = UnitDef->IdleAnim;
			Refs.Move = UnitDef->MoveAnim;
			Refs.Dying = UnitDef->DyingAnim;
			// 攻击动画跟攻击的 CastConfig 走（与技能一致，不再单列 AttackAnim）
			if (const FAsceNormalCastConfig* AttackConfig = UnitDef->AttackCastConfig.GetPtr<FAsceNormalCastConfig>())
			{
				Refs.Attack = AttackConfig->CastAnim;
			}
		}
		else if (!WarnedMissingAnimSets.Contains(Key))
		{
			WarnedMissingAnimSets.Add(Key);
			UE_LOG(LogTemp, Warning, TEXT("[Anim] 单位类型 %d 的实体定义 Def 不是 FAsceRTSUnitDef —— 不会有动画"), Key);
		}
	}
	else if (!WarnedMissingAnimSets.Contains(Key))
	{
		WarnedMissingAnimSets.Add(Key);
		UE_LOG(LogTemp, Warning, TEXT("[Anim] 单位类型 %d 在实体定义表里查不到行 —— 不会有动画"), Key);
	}

	if (!AnimSet && !WarnedMissingAnimSets.Contains(Key))
	{
		WarnedMissingAnimSets.Add(Key);
		UE_LOG(LogTemp, Warning, TEXT("[Anim] 单位类型 %d 没配 AnimSet —— 不会有动画"), Key);
	}

	AnimSetCache.Add(Key, AnimSet);
	AnimRefCache.Add(Key, Refs);
	return AnimSet;
}

TSoftObjectPtr<UAnimSequence> UAsceCombatVisualProcessor::ResolveClipRef(const uint8 ActionState, const int32 ActionDefId, const uint16 UnitTypeId) const
{
	const EAsceActionState State = static_cast<EAsceActionState>(ActionState);

	// 技能状态：读技能表的 CastConfig；没有就返回空 ⇒ 不播（不回退）
	if (State == EAsceActionState::Casting || State == EAsceActionState::Channel)
	{
		if (ActionDefId > 0)
		{
			FAsceActionDef ActionDef;
			if (FAsceActionDefLibrary::GetActionDef(ActionDefId, ActionDef))
			{
				// 施法/持续动画就在 CastConfig 上（唯一来源）
				if (const FAsceSkillCastConfig* CastConfig = ActionDef.CastConfig.GetPtr<FAsceSkillCastConfig>())
				{
					const TSoftObjectPtr<UAnimSequence> Clip = (State == EAsceActionState::Channel)
						? CastConfig->GetChannelAnim()
						: CastConfig->GetCastAnim();
					if (!Clip.IsNull())
					{
						return Clip;
					}
				}
			}
		}
		return TSoftObjectPtr<UAnimSequence>();
	}

	// 基础状态：读单位 def 的语义槽位（没配就不播）
	if (const FAsceUnitAnimRefs* Refs = AnimRefCache.Find(static_cast<int32>(UnitTypeId)))
	{
		switch (State)
		{
		case EAsceActionState::Move:   return Refs->Move;
		case EAsceActionState::Dying:  return Refs->Dying;
		case EAsceActionState::Attack: return Refs->Attack;
		case EAsceActionState::Idle:
		default:                       return Refs->Idle;
		}
	}
	return TSoftObjectPtr<UAnimSequence>();
}

const FAsceAnimClip* UAsceCombatVisualProcessor::ResolveClip(UAsceAnimSet* AnimSet, const TSoftObjectPtr<UAnimSequence>& ClipRef) const
{
	if (!AnimSet || ClipRef.IsNull())
	{
		return nullptr;
	}
	return AnimSet->Clips.Find(ClipRef);
}

bool UAsceCombatVisualProcessor::IsLoopingState(const uint8 ActionState)
{
	switch (static_cast<EAsceActionState>(ActionState))
	{
	case EAsceActionState::Idle:
	case EAsceActionState::Move:
	case EAsceActionState::Channel:
		return true;
	default:
		return false;
	}
}

void UAsceCombatVisualProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = Context.GetWorld();
	if (!World)
	{
		return;
	}

	const bool bAuthority = (World->GetNetMode() != NM_Client);
	const float Now = World->GetTimeSeconds();

	EntityQuery.ForEachEntityChunk(Context, [this, bAuthority, Now](FMassExecutionContext& Ctx)
	{
		UMassRepresentationSubsystem* RepresentationSubsystem = Ctx.GetSharedFragment<FMassRepresentationSubsystemSharedFragment>().RepresentationSubsystem;
		if (!RepresentationSubsystem)
		{
			return;
		}
		FMassInstancedStaticMeshInfoArrayView ISMInfos = RepresentationSubsystem->GetMutableInstancedStaticMeshInfos();

		const TConstArrayView<FMassRepresentationFragment> Reps = Ctx.GetFragmentView<FMassRepresentationFragment>();
		const TConstArrayView<FMassRepresentationLODFragment> RepLODs = Ctx.GetFragmentView<FMassRepresentationLODFragment>();
		const TConstArrayView<FAsceActionFragment> Actions = Ctx.GetFragmentView<FAsceActionFragment>();
		const TConstArrayView<FAsceCombatVisualFragment> Visuals = Ctx.GetFragmentView<FAsceCombatVisualFragment>();
		const TConstArrayView<FAsceUnitDefFragment> Defs = Ctx.GetFragmentView<FAsceUnitDefFragment>();
		const TConstArrayView<FAsceCombatFragment> Combats = Ctx.GetFragmentView<FAsceCombatFragment>();
		const TArrayView<FAsceAnimPlaybackFragment> Playbacks = Ctx.GetMutableFragmentView<FAsceAnimPlaybackFragment>();

		const int32 Num = Ctx.GetNumEntities();
		for (int32 i = 0; i < Num; ++i)
		{
			// 与 UMassUpdateISMProcessor 完全一致的走位条件：只有 StaticMeshInstance 才会进 ISM 更新列表
			if (Reps[i].CurrentRepresentation != EMassRepresentationType::StaticMeshInstance)
			{
				continue;
			}

			// ── 当前动作（权威端读 action fragment；客户端读复制下来的 visual fragment）──
			const FAsceActionFragment* Action = (Actions.Num() == Num) ? &Actions[i] : nullptr;
			const FAsceCombatVisualFragment* Visual = (Visuals.Num() == Num) ? &Visuals[i] : nullptr;
			const bool bHasAction = bAuthority ? (Action != nullptr) : (Visual != nullptr);

			// 单位类型：**两端都从 EntityTemplate 自带的身份 fragment 读**（不随动作消息复制）
			const uint16 UnitTypeId = (Defs.Num() == Num) ? Defs[i].UniqueTypeId : 0;
			uint8 ActionState = static_cast<uint8>(EAsceActionState::Idle);
			int32 ActionDefId = 0;
			uint32 Counter = 0;
			float ActionPlayRate = 1.f;
			if (bHasAction)
			{
				if (bAuthority)
				{
					ActionState = static_cast<uint8>(Action->ActionState);
					ActionDefId = Action->ActionDefId;
					Counter = Action->Counter;
					if (ActionState == static_cast<uint8>(EAsceActionState::Attack) && Combats.Num() == Num)
					{
						ActionPlayRate = FMath::Max(Combats[i].AttackSpeed / 200.f + 0.1f, 0.01f);
					}
				}
				else
				{
					ActionState = static_cast<uint8>(Visual->ActionState);
					ActionDefId = Visual->ActionDefId;
					Counter = Visual->Counter;
					ActionPlayRate = Visual->PlayRate;
				}
			}

			// ── 播放状态：动作边沿解析 clip；每帧推进帧号 ──
			// custom data 布局 = AnimToTexture 的 **FrameData**（2 floats：Frame, PrevFrame；帧为烘焙贴图内的**绝对帧**）
			float Floats[2] = { 0.f, 0.f };
			FAsceAnimPlaybackFragment* Playback = ((Playbacks.Num() == Num) && bHasAction) ? &Playbacks[i] : nullptr;
			if (Playback)
			{
				if (Playback->LastCounter != Counter || Playback->LastActionState != ActionState || !Playback->bValid)
				{
					Playback->LastCounter = Counter;
					Playback->LastActionState = ActionState;
					Playback->StartTime = Now;
					Playback->bValid = false;

					UAsceAnimSet* AnimSet = FindOrLoadAnimSet(UnitTypeId);
					const TSoftObjectPtr<UAnimSequence> ClipRef = ResolveClipRef(ActionState, ActionDefId, UnitTypeId);
					if (const FAsceAnimClip* Clip = ResolveClip(AnimSet, ClipRef))
					{
						Playback->AnimIndex = Clip->AnimIndex;
						Playback->StartFrame = Clip->StartFrame;
						Playback->EndFrame = Clip->EndFrame;
						Playback->FrameRate = AnimSet ? FMath::Max(AnimSet->FrameRate, 1.f) : 30.f;
						Playback->PlayRate = FMath::Max(ActionPlayRate, 0.01f);
						Playback->bLoop = IsLoopingState(ActionState);
						Playback->bValid = true;

						// 诊断（每种"单位类型+状态"只提示一次）：数据侧解析成功。
						// 有这条日志但画面还是不动 ⇒ 问题一定在渲染接线（visualization desc 里的网格/材质没换成烘焙产物）。
						const int32 HitKey = (static_cast<int32>(UnitTypeId) << 8) | static_cast<int32>(ActionState);
						if (!LoggedAnimHits.Contains(HitKey))
						{
							LoggedAnimHits.Add(HitKey);
							UE_LOG(LogTemp, Log, TEXT("[Anim] 单位类型 %u 状态 %d → %s（帧 [%d, %d]，速率 %.2f）"),
								UnitTypeId, static_cast<int32>(ActionState), *ClipRef.ToString(),
								Clip->StartFrame, Clip->EndFrame, Playback->PlayRate);
						}
					}
					else
					{
						// 诊断（每种"单位类型+状态"只提示一次）：没配 or 动画不在 AnimSet.Clips 里
						const int32 MissKey = (static_cast<int32>(UnitTypeId) << 8) | static_cast<int32>(ActionState);
						if (!WarnedAnimMisses.Contains(MissKey))
						{
							WarnedAnimMisses.Add(MissKey);
							if (ClipRef.IsNull())
							{
								UE_LOG(LogTemp, Warning, TEXT("[Anim] 单位类型 %u 的状态 %d 没配动画（AnimSet=%s）—— 不播"),
									UnitTypeId, static_cast<int32>(ActionState), AnimSet ? *AnimSet->GetName() : TEXT("无"));
							}
							else
							{
								UE_LOG(LogTemp, Warning, TEXT("[Anim] 单位类型 %u 的状态 %d 的动画 %s 不在 AnimSet(%s).Clips 里 —— 没重烘 / 动画资产换过？"),
									UnitTypeId, static_cast<int32>(ActionState), *ClipRef.ToString(), AnimSet ? *AnimSet->GetName() : TEXT("无"));
							}
						}
					}
				}

				if (Playback->bValid)
				{
					const float StartFrame = static_cast<float>(Playback->StartFrame);
					const float EndFrame = static_cast<float>(Playback->EndFrame);
					const float Length = FMath::Max(EndFrame - StartFrame, 1.f);
					float Frame = StartFrame + (Now - Playback->StartTime) * Playback->FrameRate * Playback->PlayRate;
					Frame = Playback->bLoop
						? StartFrame + FMath::Fmod(Frame - StartFrame, Length)
						: FMath::Min(Frame, EndFrame);

					Floats[0] = Frame;                                                            // 当前帧（可小数，材质做帧间插值）
					Floats[1] = FMath::Clamp(Frame - 1.f, StartFrame, EndFrame);                  // 上一帧（运动模糊/混合）

					// 诊断（每种"单位类型+状态"约每 0.5s 一次）：确认实际写入的帧号在推进。
					// 这条在变但画面不动 ⇒ 材质/组件没消费 custom data（先查 MI 的 AutoPlay 开关必须为 false）。
					{
						const int32 LogKey = (static_cast<int32>(UnitTypeId) << 8) | static_cast<int32>(ActionState);
						float& LastLogged = LastLoggedFrames.FindOrAdd(LogKey, -1000.f);
						if (FMath::Abs(Frame - LastLogged) >= FMath::Max(Playback->FrameRate, 1.f) * 0.5f)
						{
							LastLogged = Frame;
							UE_LOG(LogTemp, Log, TEXT("[Anim] 写入帧：单位类型 %u 状态 %d frame=%.1f（[%d, %d]）"),
								UnitTypeId, static_cast<int32>(ActionState), Frame, Playback->StartFrame, Playback->EndFrame);
						}
					}
				}
			}

			// ── 追加 per-instance custom data（**append，不是覆盖**）──
			// `WriteCustomDataFloatsAtStartIndex` 只覆盖已分配的槽位、不扩容（前提是别人先 append 过），
			// 而动态 ISM 路径里没有任何地方 append custom floats ⇒ 缓冲每帧都是空的，会 ensure + 越界。
			// 这里走 `AddBatchedCustomData`：和 `UMassUpdateISMProcessor` 给同一实体 append Transform **完全镜像**
			// （当前 + prev 两个 LODSignificance range）——每实例恰好 2 个 float、顺序一致，
			// PostPhysics 的 `EndVisualChanges` 才能把 `NumCustomDataFloats` 正确对应到每个实例。
			//
			// ⚠️⚠️ **改本 processor 的 query / 执行顺序前必读：custom data 与 instance 的对应靠"append 顺序"
			// 隐式对齐**——本 processor 追加的帧号会贴到 `UMassUpdateISMProcessor` 同一次循环里追加的实例上。
			// 要成立的三个条件（缺一即静默错位、帧号贴到别的实例）：
			//   1. **匹配的 archetype 集合相同**（本 query 独有的 fragment 全用 Optional ⇒ 不额外筛掉实体）；
			//   2. **同帧内、且在 `UMassUpdateISMProcessor` 之前**跑（`ExecuteAfter/Below` 已锁定）；
			//   3. 两组 query 走同一条 archetype/chunk/entity 迭代顺序（Mass 按 entity manager 的 archetype
			//      顺序缓存，同帧内一致 ⇒ 只要集合相同就成立）。
			// 验证手段：PIE 里同时放**两种**单位类型一起动，看帧号没有串到另一类身上。
			//
			// ⚠️ 假设：**每个 LODSignificance range 只有 1 个 mesh**（v1 限制，见 `技能系统设计.md` §7）。
			// `UMassUpdateISMProcessor` 对 range 里每个 mesh 各 append 一次实例；这里每实体只 append 一份，
			// 多 mesh 的 range 会实例数不齐 ⇒ 将来要支持多 mesh 必须同步改成"按 mesh 次数 append"。
			const int32 ISMInfoIndex = Reps[i].StaticMeshDescHandle.ToIndex();
			if (!ISMInfos.IsValidIndex(ISMInfoIndex))
			{
				continue;
			}
			FMassInstancedStaticMeshInfo& ISMInfo = ISMInfos[ISMInfoIndex];

			// `PrevLODSignificance`：与 Transform 路径相同（当前范围 + prev 范围镜像）；
			// 本处理器在 UMassUpdateISMProcessor 之前跑，这里读到的还是旧值。
			ISMInfo.AddBatchedCustomData<FAsceAnimFrameData>(FAsceAnimFrameData{ Floats[0], Floats[1] }
				, RepLODs[i].LODSignificance, Reps[i].PrevLODSignificance);
		}
	});
}
