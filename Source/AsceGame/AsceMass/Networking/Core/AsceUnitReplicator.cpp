// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceUnitReplicator.h"
#include "Engine/World.h"
#include "MassCommonFragments.h"
#include "MassReplicationProcessor.h" // EMassLOD / FMassReplicationSharedFragment（经 AsceUnitReplicator.h 已有，这里显式确保）

#include "AsceMass/Fragments/AsceCoreFragments.h"
#include "AsceMass/Fragments/AsceFootprintFragment.h"
#include "AsceMass/Vision/AsceVisionDefines.h"
#include "AsceMass/Vision/AsceFogSubsystem.h"
#include "GameModes/AsceGameState.h"
#include "GameFramework/PlayerController.h"
#include "MassClientBubbleInfoBase.h"
#include "Teams/AsceTeamAgentInterface.h"

namespace
{
	/** client(Index) → **打包 TeamKey**（`AsceMakeTeamKey(Index, TeamId)`，和 Fog 投影同口径）。
	 *  可选覆盖用；默认不注册（自动从 bubble Owner 解析，见 ResolveClientTeamKey）。Server 主线程访问。 */
	TMap<int32, int32>& GetClientTeamRegistry()
	{
		static TMap<int32, int32> Registry;
		return Registry;
	}

	/**
	 * 解析"当前 client 属于哪一队"：引擎给每个 client 的每个 bubble 类 spawn 一个实例、**Owner 就是对应 PlayerController**
	 * （`MassReplicationSubsystem.cpp` AddClient 里 `SpawnParams.Owner = &InController`），共享 fragment 的
	 * `BubbleInfos[ClientIndex]` 就是其中一个实例。TeamId 从 PC（委托给 PlayerState）的 GenericTeamId 拿，
	 * 再经 GameState 打包成 TeamKey（Fog 的投影按 TeamKey 索引）。
	 * @return INDEX_NONE = 解析不出来/中立，调用方跳过过滤（保持不裁剪的安全默认）。
	 */
	int32 ResolveClientTeamKey(const FMassReplicationSharedFragment& RepShared, const FMassClientHandle& ClientHandle, UWorld& World)
	{
		const int32 ClientIndex = ClientHandle.GetIndex();
		if (!RepShared.BubbleInfos.IsValidIndex(ClientIndex))
		{
			return INDEX_NONE;
		}

		AMassClientBubbleInfoBase* Bubble = RepShared.BubbleInfos[ClientIndex];
		APlayerController* PC = Bubble ? Cast<APlayerController>(Bubble->GetOwner()) : nullptr;
		IGenericTeamAgentInterface* TeamAgent = PC ? Cast<IGenericTeamAgentInterface>(PC) : nullptr;
		if (!TeamAgent)
		{
			return INDEX_NONE;
		}

		const int32 TeamId = AsceNormalizeTeamId(GenericTeamIdToInteger(TeamAgent->GetGenericTeamId()));
		if (TeamId == AsceNeutralTeamId)
		{
			return INDEX_NONE; // 中立/观战：不裁剪
		}

		if (AAsceGameState* GameState = World.GetGameState<AAsceGameState>())
		{
			return GameState->GetOrAssignTeamKey(TeamId);
		}
		return INDEX_NONE;
	}

	/**
	 * 视野复制过滤：在本 chunk 的各能力被单趟驱动之前，
	 * 把候选 entity 相对"当前 client 队伍"不可见的 LOD 覆写为 EMassLOD::Off；
	 * 可见的保底为非 Off（原距离 Off → 抬到最低非 Off 档 Low，否则保留原距离档）。
	 * 之后引擎模板按 LOD 分支决定 bubble 进出（LOD < Off → Add/Modify，LOD >= Off → Remove）。
	 *
	 * 规则：友方（同队，含建筑）恒可见；敌方/中立按覆盖格任一可见判定。
	 *
	 * @param ClientTeamKey 打包 TeamKey（不是裸 TeamId）：Fog 的 `IsEntityVisible`/投影按 TeamKey 索引；
	 *        好友比较再从 TeamKey 里取出裸 TeamId。
	 */
	void ApplyVisionFilterToChunk(FMassExecutionContext& Context, UAsceFogSubsystem& Fog, int32 ClientTeamKey)
	{
		const int32 ClientTeamId = AsceTeamId(ClientTeamKey);
		// LOD 一定在查询里（基类 EntityQuery 要求 FMassReplicationLODFragment ReadWrite）
		if (!Context.DoesArchetypeHaveFragment<FMassReplicationLODFragment>())
		{
			return;
		}
		TArrayView<FMassReplicationLODFragment> LODList = Context.GetMutableFragmentView<FMassReplicationLODFragment>();

		// 其余 fragment 只对"命中自己能力的 entity"存在（Shared EntityQuery 用 Any 合并），逐项 Any 检查
		TConstArrayView<FAsceTeamFragment> Teams;
		TConstArrayView<FTransformFragment> Transforms;
		TConstArrayView<FAgentRadiusFragment> Radii;
		TConstArrayView<FAsceGridFootprintFragment> Footprints;
		if (Context.DoesArchetypeHaveFragment<FAsceTeamFragment>())
		{
			Teams = Context.GetFragmentView<FAsceTeamFragment>();
		}
		if (Context.DoesArchetypeHaveFragment<FTransformFragment>())
		{
			Transforms = Context.GetFragmentView<FTransformFragment>();
		}
		if (Context.DoesArchetypeHaveFragment<FAgentRadiusFragment>())
		{
			Radii = Context.GetFragmentView<FAgentRadiusFragment>();
		}
		if (Context.DoesArchetypeHaveFragment<FAsceGridFootprintFragment>())
		{
			Footprints = Context.GetFragmentView<FAsceGridFootprintFragment>();
		}

		// footprint（按流场格计）→ 视野格换算比例（与 AsceFogProcessor 相同，架构文档 §2 整除约束）
		const int32 Scale = FMath::RoundToInt32(AsceVision::FlowFieldGridSizeCm / Fog.GetHeightMap().GetCellSizeCm());

		const int32 Num = Context.GetNumEntities();
		for (int32 i = 0; i < Num; ++i)
		{
			// 没有队伍/位置的 entity 不在我们处理范围：不动它的 LOD
			if (Teams.Num() != Num || Transforms.Num() != Num)
			{
				continue;
			}

			// 敌我比较用 **TeamId**（跨端一致）；fragment 里的 TeamIndex 只是写入方的本端下标
			const int32 EntityTeam = AsceTeamId(Teams[i].TeamKey);

			// 友方（含建筑）恒可见（§6.1.4）
			if (EntityTeam == ClientTeamId)
			{
				if (LODList[i].LOD == EMassLOD::Off)
				{
					LODList[i].LOD = EMassLOD::Low; // 保底进 bubble 的最低档
				}
				continue;
			}

			// 敌方/中立：按覆盖格任一可见判定（§4.1）
			FAsceVisionEntityGeometry Geometry;
			if (Footprints.Num() == Num)
			{
				// 建筑：footprint 换算成视野格矩形
				Geometry.bUseFootprint = true;
				Geometry.FootprintX0 = Footprints[i].OriginGridLocation.X * Scale;
				Geometry.FootprintY0 = Footprints[i].OriginGridLocation.Y * Scale;
				Geometry.FootprintX1 = Geometry.FootprintX0 + Footprints[i].SizeX * Scale - 1;
				Geometry.FootprintY1 = Geometry.FootprintY0 + Footprints[i].SizeY * Scale - 1;
			}
			else
			{
				// 单位：位置 + 物理覆盖半径（无 AgentRadius 时退化为 0 → 只看中心所在格）
				const FVector Loc = Transforms[i].GetTransform().GetLocation();
				Geometry.CenterWorld = FVector2D(Loc.X, Loc.Y);
				Geometry.CoverageRadiusCm = (Radii.Num() == Num) ? Radii[i].Radius : 0.f;
			}

			const bool bVisible = Fog.IsEntityVisible(ClientTeamKey, Geometry);
			if (bVisible)
			{
				if (LODList[i].LOD == EMassLOD::Off)
				{
					LODList[i].LOD = EMassLOD::Low; // 从距离 Off 抬回可见（模板会 AddEntity）
				}
				// 已非 Off：保留原距离档，作为可见范围内的更新频率/精度分档
			}
			else
			{
				LODList[i].LOD = EMassLOD::Off; // 模板会 RemoveEntity / 不入 bubble
			}
		}
	}
}

/**
 * 生命周期过滤：**请求销毁**（`FAsceLifecycleFragment`）的实体 ⇒ 把 LOD 覆写成 Off
 * （随后各能力按 LOD 走 Remove；真正的销毁由 `UAsceSimulationSubsystem` 在"bubble 引用计数归 0"后做）。
 * 必须紧跟在**视野过滤之后**：否则视野过滤可能把 Off 又抬回 Low（可见）⇒ 销毁实体被复制。
 */
static void ApplyLifecycleFilterToChunk(FMassExecutionContext& Context)
{
	if (!Context.DoesArchetypeHaveFragment<FAsceLifecycleFragment>()
		|| !Context.DoesArchetypeHaveFragment<FMassReplicationLODFragment>())
	{
		return;
	}
	TArrayView<FMassReplicationLODFragment> LODList = Context.GetMutableFragmentView<FMassReplicationLODFragment>();
	const TConstArrayView<FAsceLifecycleFragment> Lifecycles = Context.GetFragmentView<FAsceLifecycleFragment>();
	const int32 Num = Context.GetNumEntities();
	if (Lifecycles.Num() != Num)
	{
		return;
	}
	for (int32 i = 0; i < Num; ++i)
	{
		if (Lifecycles[i].IsDestroyRequested())
		{
			LODList[i].LOD = EMassLOD::Off;
		}
	}
}

TArray<TUniquePtr<FAsceReplicationCapabilityBase>>& UAsceUnitReplicator::GetCapabilities()
{
	static TArray<TUniquePtr<FAsceReplicationCapabilityBase>> Capabilities;
	return Capabilities;
}

void UAsceUnitReplicator::RegisterCapability(TUniquePtr<FAsceReplicationCapabilityBase> Capability)
{
	GetCapabilities().Add(MoveTemp(Capability));
}

void UAsceUnitReplicator::SetClientTeam(int32 ClientIndex, int32 TeamKey)
{
	// TeamKey = AsceMakeTeamKey(本端队伍 index, 裸 TeamId)，必须和 Fog 投影同口径
	GetClientTeamRegistry().Add(ClientIndex, TeamKey);
}

void UAsceUnitReplicator::ClearAllClientTeams()
{
	GetClientTeamRegistry().Reset();
}

void UAsceUnitReplicator::AddRequirements(FMassEntityQuery& EntityQuery)
{
	// 视野复制过滤要用到的"可选"数据（单位半径 / 建筑 footprint）：
	// **必须在这里显式声明**——DoesArchetypeHaveFragment 只查 archetype 有没有该 fragment，
	// 不保证 Query 绑定过视图；少了声明 GetFragmentView 会直接 checkf("Requested fragment type not bound")。
	// 用 Any：不强制 entity 同时具备，视图对"没有该 fragment 的 chunk"为空，
	// 过滤逻辑靠 `Radii.Num() == Num` / `Footprints.Num() == Num` 判断退化路径。
	EntityQuery.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Any);
	EntityQuery.AddRequirement<FAsceGridFootprintFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Any);
	// 生命周期（同上：必须显式声明）。用 **ReadWrite**：过滤只读它，但各能力的 Add/Remove 要**写**引用计数
	// （同一份共享 Query，访问权限按最宽的来）——否则能力的 `GetMutableFragmentView` 会命中 "not bound for writing"。
	EntityQuery.AddRequirement<FAsceLifecycleFragment>(EMassFragmentAccess::ReadWrite, EMassFragmentPresence::Any);

	for (const TUniquePtr<FAsceReplicationCapabilityBase>& Capability : GetCapabilities())
	{
		Capability->AddRequirements(EntityQuery);
	}
}

void UAsceUnitReplicator::ProcessClientReplication(FMassExecutionContext& Context, FMassReplicationContext& ReplicationContext)
{
#if UE_REPLICATION_COMPILE_SERVER_CODE

	// 引擎对"当前client的每个archetype chunk"各调一次本函数（Chunk Context）。
	// FMassReplicatedAgentFragment::AgentData.Handle 在这个 Context 里是**每个entity一个**的
	// scratch槽（per client，SyncToMass/SyncFromMass 负责和 ClientReplicationInfo.AgentsData
	// 同步），所以绝不能"每个能力各跑一遍引擎的 CalculateClientReplication"——
	// 那样能力1把handle写进槽后，能力2会读到能力1的handle并拿它去 Modify 自己的bubble，
	// 触发 TIndexedHandleManager::IsValidHandle（ManagerID不匹配）断言；且能力2..N永远
	// 走不到Add。这里改成：所有能力对同一批entity做单趟驱动，各能力用**自己bubble的
	// NetID→Handle表**独立判断 Add/Modify/Remove，最后再把"该entity是否已被任一能力
	// 复制"聚合回引擎的scratch槽（引擎的Off清理会断言Handle必须无效）。

	const FMassReplicationSharedFragment& RepShared = Context.GetSharedFragment<FMassReplicationSharedFragment>();
	const FMassClientHandle ClientHandle = RepShared.CurrentClientHandle;
	check(ClientHandle.IsValid());

	// ── 视野复制过滤────────────────────────────
	// 决策者入口：在所有数据能力被驱动之前，按当前 client 队伍的可见集覆写 LOD
	// （不可见 → Off）。之后各能力按 LOD 的最终值自行 Add/Modify/Remove。
	// 基础设施未就绪（高度场未初始化 / client 队伍未注册）→ 跳过，保持引擎距离 LOD（安全默认）。
	UAsceFogSubsystem* Fog = UWorld::GetSubsystem<UAsceFogSubsystem>(&ReplicationContext.World);
	if (Fog && Fog->IsReady())
	{
		// 队伍来源：优先显式注册（可选覆盖/测试），否则从"该 client 的代表 bubble 实例的 Owner(PC)"自动解析。
		int32 ClientTeamKey = INDEX_NONE;
		if (const int32* Registered = GetClientTeamRegistry().Find(ClientHandle.GetIndex()))
		{
			ClientTeamKey = *Registered;
		}
		else
		{
			ClientTeamKey = ResolveClientTeamKey(RepShared, ClientHandle, ReplicationContext.World);
		}

		if (ClientTeamKey != INDEX_NONE)
		{
			ApplyVisionFilterToChunk(Context, *Fog, ClientTeamKey);
		}
		else
		{
			// 中立/观战/依赖未就绪：不裁剪（安全默认）
			UE_LOG(LogVision, VeryVerbose, TEXT("UnitReplicator：client(%d) 队伍解析失败，跳过视野过滤"),
				ClientHandle.GetIndex());
		}
	}
	// ────────────────────────────────────────────────────────────

	// 生命周期过滤：请求销毁的实体一律 LOD Off（放在视野过滤之后 ⇒ Off 优先级最高）
	ApplyLifecycleFilterToChunk(Context);

	const int32 NumEntities = Context.GetNumEntities();
	if (NumEntities == 0)
	{
		return;
	}

	// per-entity聚合标记：哪个能力先把这个entity复制给当前client，就记谁的handle；
	// 后续能力只读不覆写（有些能力不关心这个entity，不会写）。所有能力跑完后再写回引擎。
	TArray<FMassReplicatedAgentHandle, TInlineAllocator<64>> Markers;
	Markers.SetNumUninitialized(NumEntities);
	for (int32 EntityIdx = 0; EntityIdx < NumEntities; ++EntityIdx)
	{
		Markers[EntityIdx].Invalidate();
	}

	for (const TUniquePtr<FAsceReplicationCapabilityBase>& Capability : GetCapabilities())
	{
		Capability->ProcessChunk(Context, ReplicationContext, ClientHandle, Markers);
	}

	// 写回引擎的scratch AgentData（引擎随后 SyncFromMass 会把它存进当前client的AgentsData）。
	// LOD 用视野过滤后的最终值；Handle 只有"至少一个能力持有"时才有效——否则引擎在
	// LOD==Off 的清理里会 checkf(!AgentData.Handle.IsValid()) 失败。
	const TConstArrayView<FMassReplicationLODFragment> LODList = Context.GetFragmentView<FMassReplicationLODFragment>();
	TArrayView<FMassReplicatedAgentFragment> ReplicatedAgentList = Context.GetMutableFragmentView<FMassReplicatedAgentFragment>();
	const double Time = ReplicationContext.World.GetRealTimeSeconds();
	for (int32 EntityIdx = 0; EntityIdx < NumEntities; ++EntityIdx)
	{
		FMassReplicatedAgentData& AgentData = ReplicatedAgentList[EntityIdx].AgentData;
		AgentData.Handle = Markers[EntityIdx];
		AgentData.LOD = LODList[EntityIdx].LOD;
		if (Markers[EntityIdx].IsValid())
		{
			AgentData.LastUpdateTime = Time;
		}
	}

#endif // UE_REPLICATION_COMPILE_SERVER_CODE
}
