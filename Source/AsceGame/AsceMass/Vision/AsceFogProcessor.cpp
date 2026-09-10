// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceFogProcessor.h"

#include "Engine/World.h"
#include "MassEntityQuery.h"
#include "MassExecutionContext.h"
#include "MassCommonFragments.h"

#include "AsceMass/Fragments/AsceCoreFragments.h"
#include "AsceMass/Vision/AsceVisionFragments.h"
#include "AsceMass/Vision/AsceFogSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceFogProcessor)

UAsceFogProcessor::UAsceFogProcessor()
	: UnitQuery(*this)
{
	// Server/Client 各跑一份（Server 用视野做复制过滤；Client 用本队视野做表现，见架构文档 §6）
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);

	// 本处理器由 UAsceFogSubsystem 在 Tick 里手动 UE::Mass::Executor::Run 驱动，
	// 因此不进 Mass 全局 ProcessingPhase 自动调度，避免每帧被跑两遍（与 AsceGridEntitiesUpdateProcessor 同模式）。
	bAutoRegisterWithProcessingPhases = false;
}

void UAsceFogProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	// 注意：本处理器由 UAsceFogSubsystem 手动 UE::Mass::Executor::Run 驱动（不进 Mass 全局调度，
	// 与 AsceGridEntitiesUpdateProcessor 同模式），因此不声明 AddSubsystemRequirement——
	// 子系统在 Execute 里直接用 World::GetSubsystem 取（见 Execute 开头）。
	UnitQuery.RegisterWithProcessor(*this);
	UnitQuery.AddRequirement<FAsceVisionViewFragment>(EMassFragmentAccess::ReadOnly);
	UnitQuery.AddRequirement<FAsceTeamFragment>(EMassFragmentAccess::ReadOnly);
	UnitQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	UnitQuery.AddRequirement<FAsceVisionFogStateFragment>(EMassFragmentAccess::ReadWrite);
}

void UAsceFogProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UAsceFogSubsystem* Fog = UWorld::GetSubsystem<UAsceFogSubsystem>(GetWorld());
	if (!Fog || !Fog->IsReady())
	{
		return; // 地图高度场尚未初始化（加载期），跳过
	}

	// ① 视野观察者单位：逐单位 diff 视野贡献
	// 注意：单位消亡（死亡/销毁）不用在这里处理——由 AsceVisionViewRemoveObserverProcessor
	// （观察 FAsceVisionViewFragment 的 Remove）在实体销毁前读其"上帧贡献状态"按旧参数下线。
	UnitQuery.ParallelForEachEntityChunk(Context, [Fog](FMassExecutionContext& Ctx)
	{
		const TConstArrayView<FAsceVisionViewFragment> Views = Ctx.GetFragmentView<FAsceVisionViewFragment>();
		const TConstArrayView<FAsceTeamFragment> Teams = Ctx.GetFragmentView<FAsceTeamFragment>();
		const TConstArrayView<FTransformFragment> Transforms = Ctx.GetFragmentView<FTransformFragment>();
		const TArrayView<FAsceVisionFogStateFragment> PrevCells = Ctx.GetMutableFragmentView<FAsceVisionFogStateFragment>();

		FAsceVisionHeightMap& HMap = Fog->GetHeightMap();

		const int32 Num = Ctx.GetNumEntities();
		for (int32 i = 0; i < Num; ++i)
		{
			const FVector2D WorldPos(Transforms[i].GetTransform().GetLocation().X, Transforms[i].GetTransform().GetLocation().Y);
			const FIntVector2 Cell = HMap.WorldToCell(WorldPos);
			const int32 CellIndex = HMap.IsInside(Cell) ? HMap.CellToIndex(Cell) : -1;

			const uint16 He = UAsceFogSubsystem::ComputeHe(
				CellIndex >= 0 ? HMap.GetEffectiveHeight(CellIndex) : 0,
				Views[i].EyeHeightCm);

			// 与单位本地"上帧贡献状态" diff（Fog 不维护单位级表，见 AsceVisionFragments.h）：
			// 只有 (cell / He / R / team) 任一变化才做 下线旧档 → 上线新档。
			FAsceVisionFogStateFragment& Prev = PrevCells[i];
			const int32 TeamKey = Teams[i].TeamKey;
			const uint16 R = Views[i].SightRadius;
			if (Prev.LastVisionCellIndex != CellIndex
				|| Prev.LastHe != He
				|| Prev.LastR != R
				|| Prev.LastTeamKey != TeamKey)
			{
				if (Prev.LastVisionCellIndex >= 0)
				{
					Fog->RemoveContribution(Prev.LastTeamKey, Prev.LastVisionCellIndex, Prev.LastHe, Prev.LastR);
				}
				if (CellIndex >= 0)
				{
					Fog->AddContribution(TeamKey, CellIndex, He, R);
				}

				Prev.LastVisionCellIndex = CellIndex;
				Prev.LastHe = He;
				Prev.LastR = R;
				Prev.LastTeamKey = TeamKey;
			}
		}
	});

	// ② 帧末：合并活跃档变更（并行阶段塞入分片的增删）+ 遮挡变化（dirty）的缓存结果翻转
	Fog->ProcessHeightChanges();
}
