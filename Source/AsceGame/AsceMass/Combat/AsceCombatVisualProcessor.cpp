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

#include "AsceMass/Combat/AsceCombatFragments.h"   // FAsceAnimPlaybackFragment（cue）

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceCombatVisualProcessor)

namespace
{
	/** per-instance custom data = AnimToTexture 的 `FAnimToTextureFrameData` 布局（2 floats：Frame, PrevFrame） */
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
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Standalone);
	bRequiresGameThreadExecution = true;

	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::Representation;
	ExecutionOrder.ExecuteAfter.Add(UMassVisualizationProcessor::StaticClass()->GetFName());
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

	// cue 播放状态（Optional：不改变匹配集；缺失时写中立 custom data 保持缓冲对齐）
	EntityQuery.AddRequirement<FAsceAnimPlaybackFragment>(EMassFragmentAccess::ReadWrite, EMassFragmentPresence::Optional);
}

void UAsceCombatVisualProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = Context.GetWorld();
	if (!World)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();

	EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& Ctx)
	{
		UMassRepresentationSubsystem* RepresentationSubsystem = Ctx.GetSharedFragment<FMassRepresentationSubsystemSharedFragment>().RepresentationSubsystem;
		if (!RepresentationSubsystem)
		{
			return;
		}
		FMassInstancedStaticMeshInfoArrayView ISMInfos = RepresentationSubsystem->GetMutableInstancedStaticMeshInfos();

		const TConstArrayView<FMassRepresentationFragment> Reps = Ctx.GetFragmentView<FMassRepresentationFragment>();
		const TConstArrayView<FMassRepresentationLODFragment> RepLODs = Ctx.GetFragmentView<FMassRepresentationLODFragment>();
		const TArrayView<FAsceAnimPlaybackFragment> Playbacks = Ctx.GetMutableFragmentView<FAsceAnimPlaybackFragment>();

		const int32 Num = Ctx.GetNumEntities();
		for (int32 i = 0; i < Num; ++i)
		{
			// 与 UMassUpdateISMProcessor 完全一致的走位条件：只有 StaticMeshInstance 才会进 ISM 更新列表
			if (Reps[i].CurrentRepresentation != EMassRepresentationType::StaticMeshInstance)
			{
				continue;
			}

			// custom data 布局 = AnimToTexture 的 **FrameData**（2 floats：Frame, PrevFrame；帧为烘焙贴图内的**绝对帧**）
			float Floats[2] = { 0.f, 0.f };
			FAsceAnimPlaybackFragment* Playback = (Playbacks.Num() == Num) ? &Playbacks[i] : nullptr;
			if (Playback)
			{
				// cue 边沿：行为树在客户端写 Cue*；Counter 变 ⇒ 按新帧段重播
				if (Playback->bCueActive && Playback->CueCounter != Playback->LastCueCounter)
				{
					Playback->LastCueCounter = Playback->CueCounter;
					Playback->StartTime = Now;
					Playback->AnimIndex = Playback->CueAnimIndex;
					Playback->StartFrame = Playback->CueStartFrame;
					Playback->EndFrame = Playback->CueEndFrame;
					Playback->FrameRate = FMath::Max(Playback->CueFrameRate, 1.f);
					Playback->PlayRate = FMath::Max(Playback->CuePlayRate, 0.01f);
					Playback->bLoop = Playback->bCueLoop;
					Playback->bValid = true;
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

					Floats[0] = Frame;                                                    // 当前帧（可小数，材质做帧间插值）
					Floats[1] = FMath::Clamp(Frame - 1.f, StartFrame, EndFrame);          // 上一帧（运动模糊/混合）
				}
			}

			// ── 追加 per-instance custom data（**append，不是覆盖**；与 UMassUpdateISMProcessor 的 Transform 镜像）──
			const int32 ISMInfoIndex = Reps[i].StaticMeshDescHandle.ToIndex();
			if (!ISMInfos.IsValidIndex(ISMInfoIndex))
			{
				continue;
			}
			FMassInstancedStaticMeshInfo& ISMInfo = ISMInfos[ISMInfoIndex];
			ISMInfo.AddBatchedCustomData<FAsceAnimFrameData>(FAsceAnimFrameData{ Floats[0], Floats[1] }
				, RepLODs[i].LODSignificance, Reps[i].PrevLODSignificance);
		}
	});
}
