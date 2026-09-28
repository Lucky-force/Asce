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
	FName AttackClip;

	// 按数字类型 id 查实体定义表行（见 `FAsceUnitDefFragment` / 实体定义表约定）
	FAsceEntityDefRow Row;
	if (UAsceEntityDefLibrary::GetEntityDefRow(Key, Row))
	{
		if (const FAsceRTSUnitDef* UnitDef = Row.Def.GetPtr<FAsceRTSUnitDef>())
		{
			AnimSet = UnitDef->AnimSet.LoadSynchronous();
			if (const FAsceNormalCastConfig* AttackConfig = UnitDef->AttackCastConfig.GetPtr<FAsceNormalCastConfig>())
			{
				AttackClip = AttackConfig->CastAnim;
			}
		}
	}

	AnimSetCache.Add(Key, AnimSet);
	AttackClipCache.Add(Key, AttackClip);
	return AnimSet;
}

FName UAsceCombatVisualProcessor::FindAttackClipName(const uint16 UnitTypeId) const
{
	const FName* Found = AttackClipCache.Find(static_cast<int32>(UnitTypeId));
	return (Found && !Found->IsNone()) ? *Found : AsceAnimClipNames::Attack;
}

FName UAsceCombatVisualProcessor::ResolveClipName(const uint8 ActionState, const int32 ActionDefId, const uint16 UnitTypeId) const
{
	switch (static_cast<EAsceActionState>(ActionState))
	{
	case EAsceActionState::Attack:
		return FindAttackClipName(UnitTypeId);

	case EAsceActionState::Casting:
	case EAsceActionState::Channel:
	{
		if (ActionDefId > 0)
		{
			FAsceActionDef ActionDef;
			if (FAsceActionDefLibrary::GetActionDef(ActionDefId, ActionDef))
			{
				// 1) 动作表里声明的语义 clip（优先）
				for (const FAsceActionClipRef& Ref : ActionDef.AnimClips)
				{
					if (Ref.State == static_cast<EAsceActionState>(ActionState) && !Ref.ClipName.IsNone())
					{
						return Ref.ClipName;
					}
				}
				// 2) CastConfig 上的动画名
				if (const FAsceSkillCastConfig* CastConfig = ActionDef.CastConfig.GetPtr<FAsceSkillCastConfig>())
				{
					const FName Clip = (ActionState == static_cast<uint8>(EAsceActionState::Channel))
						? CastConfig->GetChannelAnim()
						: CastConfig->GetCastAnim();
					if (!Clip.IsNone())
					{
						return Clip;
					}
				}
			}
		}
		// 3) 保留名
		return (ActionState == static_cast<uint8>(EAsceActionState::Channel))
			? AsceAnimClipNames::ChannelGeneric
			: AsceAnimClipNames::CastGeneric;
	}

	case EAsceActionState::Move:
		return AsceAnimClipNames::Move;

	case EAsceActionState::Dying:
		return AsceAnimClipNames::Dying;

	default:
		return AsceAnimClipNames::Idle;
	}
}

const FAsceAnimClip* UAsceCombatVisualProcessor::ResolveClip(UAsceAnimSet* AnimSet, const FName ClipName, const uint8 ActionState, bool& bOutLoop) const
{
	bOutLoop = false;
	if (!AnimSet)
	{
		return nullptr;
	}

	auto Try = [AnimSet](const FName Name) -> const FAsceAnimClip*
	{
		return Name.IsNone() ? nullptr : AnimSet->Clips.Find(Name);
	};

	// 回退链：专属 → Cast_Generic/Channel_Generic/Attack → Idle → 无
	const EAsceActionState State = static_cast<EAsceActionState>(ActionState);
	if (const FAsceAnimClip* Clip = Try(ClipName))
	{
		bOutLoop = Clip->bLoop;
		return Clip;
	}
	if (State == EAsceActionState::Channel)
	{
		if (const FAsceAnimClip* Clip = Try(AsceAnimClipNames::ChannelGeneric)) { bOutLoop = Clip->bLoop; return Clip; }
	}
	if (State == EAsceActionState::Channel || State == EAsceActionState::Casting)
	{
		if (const FAsceAnimClip* Clip = Try(AsceAnimClipNames::CastGeneric)) { bOutLoop = Clip->bLoop; return Clip; }
		if (const FAsceAnimClip* Clip = Try(AsceAnimClipNames::Attack)) { bOutLoop = Clip->bLoop; return Clip; }
	}
	if (State == EAsceActionState::Attack || State == EAsceActionState::Dying)
	{
		if (const FAsceAnimClip* Clip = Try(AsceAnimClipNames::Attack)) { bOutLoop = Clip->bLoop; return Clip; }
	}
	if (const FAsceAnimClip* Clip = Try(AsceAnimClipNames::Idle))
	{
		bOutLoop = Clip->bLoop;
		return Clip;
	}
	return nullptr;
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

			uint16 UnitTypeId = 0;
			uint8 ActionState = static_cast<uint8>(EAsceActionState::Idle);
			int32 ActionDefId = 0;
			uint32 Counter = 0;
			float ActionPlayRate = 1.f;
			if (bHasAction)
			{
				if (bAuthority)
				{
					UnitTypeId = (Defs.Num() == Num) ? Defs[i].UniqueTypeId : 0;
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
					UnitTypeId = Visual->UnitTypeId;
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
					const FName ClipName = ResolveClipName(ActionState, ActionDefId, UnitTypeId);
					bool bLoop = false;
					if (const FAsceAnimClip* Clip = ResolveClip(AnimSet, ClipName, ActionState, bLoop))
					{
						Playback->AnimIndex = Clip->AnimIndex;
						Playback->StartFrame = Clip->StartFrame;
						Playback->EndFrame = Clip->EndFrame;
						Playback->FrameRate = AnimSet ? FMath::Max(AnimSet->FrameRate, 1.f) : 30.f;
						Playback->PlayRate = FMath::Max(ActionPlayRate, 0.01f) * FMath::Max(Clip->BasePlayRate, 0.01f);
						Playback->bLoop = Clip->bLoop;
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
