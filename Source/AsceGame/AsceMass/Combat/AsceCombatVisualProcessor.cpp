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
				}
			}

			// ── 写 per-instance custom data（**必须覆盖每一个进 ISM 更新的实例**，否则缓冲对不齐）──
			const int32 ISMInfoIndex = Reps[i].StaticMeshDescHandle.ToIndex();
			if (!ISMInfos.IsValidIndex(ISMInfoIndex))
			{
				continue;
			}
			FMassInstancedStaticMeshInfo& ISMInfo = ISMInfos[ISMInfoIndex];
			if (FMassLODSignificanceRange* Range = ISMInfo.GetLODSignificanceRange(RepLODs[i].LODSignificance))
			{
				if (Range->StaticMeshRefs.Num() != 1)
				{
					if (!bWarnedMultiMesh)
					{
						bWarnedMultiMesh = true;
						UE_LOG(LogTemp, Warning,
							TEXT("[Anim] 每个 LODSignificance range 只支持 1 个 mesh（当前 %d 个）：动画 custom data 跳过"),
							Range->StaticMeshRefs.Num());
					}
					continue;
				}

				// `PrevLODSignificance`：与 Transform 路径相同（当前范围 + prev 范围镜像）；
				// 本处理器在 UMassUpdateISMProcessor 之前跑，这里读到的还是旧值。
				ISMInfo.WriteCustomDataFloatsAtStartIndex(0, MakeArrayView(Floats), RepLODs[i].LODSignificance, 2, 0, Reps[i].PrevLODSignificance);
			}
		}
	});
}
