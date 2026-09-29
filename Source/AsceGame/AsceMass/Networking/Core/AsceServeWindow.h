// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassCommonTypes.h"   // FMassNetworkID

/**
 * 每(复制通道 × 客户端)一份的"**公平分帧 + 单帧硬预算**"调度器。见 `Networking/Mass复制设计.md` §5。
 *
 * 解决的问题：一帧内变化的项数可能远超单通道 bunch 上限（64KB），直接全发会撞
 * `net.MaxConstructedPartialBunchSizeBytes`（Development 断言 / Shipping 断线）。
 * 本调度器给它一个**写入时的硬上限**（`BytesPerItem × 项数 ≤ ByteBudget`）：超出的项这一帧**不发**
 * （不写影子副本 ⇒ 下帧比较仍不等 ⇒ 仍是候选），并在多帧之间**公平轮转**（不会永远只有最前面那批被发）。
 *
 * 公平机制（服务票据 + 轮询窗口）：
 *  - `NextTicket`：本通道单调自增的"服务号"（uint32 环形回绕；**0 保留 = 从未服务**）。
 *  - 每项存 `LastServedTicket`：上次真正发出去时的服务号。
 *  - `RoundMarks`：每帧开头 push 当前 `NextTicket`；`[Last() .. [0]]` 构成"本轮已服务"的号段窗口，
 *    落在其中的项本帧跳过；某个"**没发满**"的帧会 pop 掉最老的标记（缩小窗口），让更早发过的项又能被发。
 *    ⇒ 满载时窗口 ≈ 旋转周期 `ceil(M/N)` 帧（个位数），不会无限增长。
 *
 * 用法（服务器侧，在 `SetXxxFromFragment` 里，判断"值变了"之后）：
 *   ServeWindow.BeginFrameIfNeeded(GFrameCounter);
 *   if (ServeWindow.IsServedThisRound(Item.Agent.GetLastServedTicket())) return;
 *   if (!ServeWindow.TryReserveBudget()) return;
 *   ...写字段...; Serializer->MarkItemDirty(Item); ServeWindow.MarkServed(Item.Agent.GetLastServedTicketMutable());
 */
struct FAsceServeWindow
{
	/** 单帧字节预算（< 64KB，留 bunch/数组头等余量） */
	int32 ByteBudget = 56 * 1024;

	/** 该通道单项上线字节（用于把字节预算换算成每帧项数；偏保守取值即可） */
	int32 BytesPerItem = 64;

	/** 每帧最多项数（兜底，避免 BytesPerItem 估太小） */
	int32 MaxItemsPerFrame = 1024;

	// ── 运行时状态 ──
	uint32 NextTicket = 0;
	TArray<uint32> RoundMarks;
	uint64 LastFrame = 0;
	bool bHitCap = false;
	int32 BytesThisFrame = 0;
	int32 ItemsThisFrame = 0;

	/** 0 = "从未服务"（有效服务号从 1 开始） */
	static constexpr uint32 NeverServed = 0;

	/** 帧边界（懒调用：首次调用即换帧）：结算上一帧 + 中间空闲帧 → push 本帧起点票号。 */
	void BeginFrameIfNeeded(const uint64 FrameCounter)
	{
		if (LastFrame == FrameCounter)
		{
			return;
		}

		// 上一帧：没发满 ⇒ 缩小窗口（弹出最老标记）
		if (!bHitCap)
		{
			PopMark();
		}
		// 中间完全空闲的帧（无任何服务）都按"没发满"处理；有界循环（最多把窗口 pop 到剩 1 个标记）
		for (uint64 Frame = LastFrame + 1; Frame < FrameCounter && RoundMarks.Num() > 1; ++Frame)
		{
			PopMark();
		}

		RoundMarks.Insert(NextTicket, 0);
		LastFrame = FrameCounter;
		bHitCap = false;
		BytesThisFrame = 0;
		ItemsThisFrame = 0;
	}

	/** 本轮是否已经服务过（落在窗口内）。`NeverServed` 恒返回 false。 */
	bool IsServedThisRound(const uint32 Ticket) const
	{
		if (Ticket == NeverServed || RoundMarks.Num() < 2)
		{
			return false;
		}
		const uint32 Front = RoundMarks[0];
		const uint32 Back = RoundMarks.Last();
		// 环形数轴：Ticket 落在 (Back, Front] 内即本轮已服务
		return (Front - Ticket) < (Front - Back);
	}

	/** 单帧名额/字节是否还够（按本次操作的线上字节 `ItemBytes` 扣）；不够则标记本帧打满并返回 false。 */
	bool TryReserveBudget(const int32 ItemBytes)
	{
		if (BytesThisFrame + ItemBytes > ByteBudget || ItemsThisFrame >= MaxItemsPerFrame)
		{
			bHitCap = true;
			return false;
		}
		BytesThisFrame += ItemBytes;
		++ItemsThisFrame;
		return true;
	}

	/** 真的发出去了：分配下一个服务号并写回项。 */
	void MarkServed(uint32& InOutTicket)
	{
		++NextTicket;
		if (NextTicket == NeverServed)
		{
			++NextTicket;   // 跳过 0（0 = 从未服务）
		}
		InOutTicket = NextTicket;
	}

private:
	void PopMark()
	{
		if (RoundMarks.Num() > 1)
		{
			RoundMarks.SetNum(RoundMarks.Num() - 1);
		}
	}
};

/**
 * bubble handler 的 CRTP 小混入：提供 `ServeWindow` + 门控辅助，**四个 handler 共用**（不再各写一遍）。
 * 被动基类 `TAscePassiveClientBubbleHandler`（值/动作/速度）和 owner `TAscePositionClientBubbleHandler`
 * （Transform，不能继承被动基类）都继承它。
 *
 * 要求 `AgentArrayItem::FReplicatedAgentType` 提供 `GetLastServedTicket()/GetLastServedTicketMutable()/
 * GetLastUpdateTime()/SetLastUpdateTime()`（见 `AsceLightReplicatedAgent.h` / `FAsceTransformReplicatedAgent`）。
 */
template<typename AgentArrayItem>
class TAsceServeWindowHandler
{
public:
	using FAgentType = typename AgentArrayItem::FReplicatedAgentType;
	using ServeWindowType = FAsceServeWindow;

	/** 本帧额度是否已满（用于跳过昂贵计算，如距离） */
	bool IsFrameBudgetExhausted() const { return ServeWindow.bHitCap; }

	/** 三步门控（调用前须已确认"值变了"）：按距离的 interval → 票据窗口 → 单帧预算。
	 *  返回 true = 可以发（额度已占）；interval 未过时**不 stamp**、也不算"本轮已服务"。 */
	bool TryReserveForSend(const FAgentType& Agent, const float IntervalSeconds, const float Now)
	{
		if (IntervalSeconds > 0.f && (Now - Agent.GetLastUpdateTime()) < IntervalSeconds)
		{
			return false;
		}
		EnsureFrame();
		if (ServeWindow.IsServedThisRound(Agent.GetLastServedTicket()))
		{
			return false;
		}
		return ServeWindow.TryReserveBudget(ServeWindow.BytesPerItem);
	}

	/** 真正发出去之后调用：记"上次更新时间 + 服务号" */
	void MarkSent(FAgentType& Agent, const float Now)
	{
		Agent.SetLastUpdateTime(Now);
		ServeWindow.MarkServed(Agent.GetLastServedTicketMutable());
	}

	// ──────────────────────────────────────────────
	// 待加入 / 已移除实体的"票"（让 Add/Remove 也落在同一条票据时间轴上）
	//   - 只有"当前不在本 bubble 里"的实体才有条目；
	//   - Add 成功 ⇒ 移入 item（`CommitAdd`）；Remove ⇒ 从 item 收回（`SavePendingTicket`）；
	//   - 所以 Modify/Remove 只读 item 的票（热路径零查表），**只有 Add 路径查这张表**。
	// ──────────────────────────────────────────────

	/** Add 前判断：票不在窗口内（未"本轮已服务"）+ 额度够 ⇒ 可以 Add。
	 *  无条目 = 从未服务 = 可 Add；有旧条目的 flapping 实体若还在窗口内 ⇒ 挡住（不许反复进出刷额度）。 */
	bool TryReserveForAdd(const FMassNetworkID NetID)
	{
		// 票的生命周期：Remove 时收进本表 → Add 成功时移回 item（`CommitAdd` 里 Remove）→ 出窗口后由定期清扫删。
		const uint32 Ticket = PendingTickets.FindRef(NetID);
		EnsureFrame();
		if (ServeWindow.IsServedThisRound(Ticket))
		{
			return false;
		}
		return ServeWindow.TryReserveBudget(ServeWindow.BytesPerItem);
	}

	/** Remove 也走**共用预算**（它每项只写一个 ID ≈ 4B）；额度满 ⇒ 下帧重试（实体仍在 bubble、LOD 仍 Off，自愈）。
	 *  共用是安全的：吃预算的项（Add/Modify）都会打票 ⇒ 下一帧让位 ⇒ 总会有空帧轮到 Remove。 */
	bool TryReserveForRemove()
	{
		EnsureFrame();
		return ServeWindow.TryReserveBudget(RemoveWireBytes);
	}

	/** Add 成功、`AddAgent` 之前调用：清掉待加入条目 + 给新 item 一张新票/更新时间。 */
	void CommitAdd(const FMassNetworkID NetID, FAgentType& Agent, const float Now)
	{
		PendingTickets.Remove(NetID);
		MarkSent(Agent, Now);
	}

	/** Remove 前调用：把 item 的票收回（仅当它还在窗口内才有意义；否则等价于"没票"，不存）。 */
	void SavePendingTicket(const FMassNetworkID NetID, const uint32 Ticket)
	{
		if (!ServeWindow.IsServedThisRound(Ticket))
		{
			return;
		}
		PendingTickets.Add(NetID, Ticket);
	}

protected:
	/** 换帧时：跑帧边界 + （每 `SweepPeriod` 帧）清扫表里"票已出窗口"的条目。
	 *  只删"等价于没票"的条目 ⇒ **永不删有用的（窗口内的）条目**，所以表大小 ≈ 窗口内条目数 ≈ 活跃实体数，不会无限涨。 */
	void EnsureFrame()
	{
		const bool bNewFrame = (ServeWindow.LastFrame != GFrameCounter);
		ServeWindow.BeginFrameIfNeeded(GFrameCounter);
		if (bNewFrame)
		{
			SweepPendingTickets();
		}
	}

	void SweepPendingTickets()
	{
		if (PendingTickets.Num() == 0 || (GFrameCounter % SweepPeriod) != 0)
		{
			return;
		}
		for (auto It = PendingTickets.CreateIterator(); It; ++It)
		{
			if (!ServeWindow.IsServedThisRound(It.Value()))
			{
				It.RemoveCurrent();
			}
		}
	}

	FAsceServeWindow ServeWindow;

	/** Remove 每项的线上字节（FastArray 删除只写一个 int32 ID） */
	static constexpr int32 RemoveWireBytes = 4;

	/** 待加入/已移除实体的票（NetID → ticket，0 = 从未服务）；只装"当前不在本 bubble 里"的实体 */
	TMap<FMassNetworkID, uint32> PendingTickets;

	/** 清扫周期（帧）：只作用于"出窗口"条目，越大越省 CPU */
	static constexpr uint64 SweepPeriod = 120;
};
