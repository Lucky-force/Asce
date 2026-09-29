// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Components/ActorComponent.h"
#include "MassCommonTypes.h"
#include "MassEntityHandle.h"   // 默认参数 `FMassEntityHandle()` 需要完整类型（前向声明不够）
#include "AsceOrderChannelComponent.generated.h"

struct FMassEntityHandle;
/**
 * 客户端下单被服务器拒绝的原因，用于ClientRejectWholeOrder/ClientRejectEntityOrder回传给客户端做
 * 视觉/音效反馈（比如选中框闪红、播放"无法移动到此处"提示音），不承载详细文本，
 * 具体文案由客户端按枚举自行映射。
 */
UENUM(BlueprintType)
enum class EAsceOrderRejectReason : uint8
{
	None,
	TooManyUnits,        // 一次下单选中的单位数超过上限
	InvalidTarget,       // 目标坐标/目标NetID本身不合法（比如坐标非有限值），或者依赖的Subsystem缺失
	EntityNotFound,      // NetID在服务器权威表里查不到对应的EntityHandle（可能已经死亡/离线）
	MissingOrderFragment,// 查到了entity，但它身上没有对应的Order Fragment（配置错误/单位类型不支持该指令）
	NotOwned,            // 指挥了非自己阵营的实体（客户端已先过滤；这里是防作弊兜底）

	// —— 技能释放（`ServerRequestSkillOrder`）逐单位原因（见 `Combat/技能系统设计.md` §3.1）——
	SkillSlotEmpty,      // 该单位这个槽位是空的（或没有槽位 fragment）
	SkillCasterInvalid,  // 施法者本身不合法：缺 fragment / 技能表查不到 / 缺 `CastConfig`（多为配置错误）
	SkillOnCooldown,     // 槽位 CD 中
	SkillNotEnoughMana,  // 蓝不够
	SkillTargetInvalid,  // 目标类型/关系不合法（指向技能没点到单位 / Self 规则 / Enemy-Friendly 不符）
	SkillOutOfRange,     // 距目标超出射程
	SkillTargetNotVisible,// 服务器兜底：目标/落点不可见
};

/** 技能释放：一次请求里被拒绝的**单个单位**（一批里每个实体带自己的原因）。
 *  与 `ServerRequestSkillOrder`（一次请求一条 RPC、带 N 个实体）对称地由 `ClientRejectSkillOrder` 一次带回。 */
USTRUCT()
struct FAsceEntityReject
{
	GENERATED_BODY()

	UPROPERTY()
	FMassNetworkID NetID;

	UPROPERTY()
	EAsceOrderRejectReason Reason = EAsceOrderRejectReason::None;
};

/**
 * 承载"下达指令"这一类RPC的组件，专注寻路系统所依赖的Order通道本身，
 * 不关心它被挂在什么Actor上（PlayerController/GameMode相关的绑定逻辑由GameFeature负责，
 * 这里只要求：Owner是一个Replicated Actor，且这个组件本身开启复制，使其RPC按Owner的
 * owning connection正常寻址即可，符合UActorComponent RPC的标准用法）。
 *
 * 用法约定（对应架构文档7.6节）：
 * - 客户端框选单位后调用RequestMoveOrder（本地非RPC入口，见下方说明），不要直接调
 *   ServerRequestMoveOrder——RequestMoveOrder负责NetID寻址+发RPC+（如果是纯客户端）
 *   立即在本地跑一遍寻路做客户端预测，两件事都做全了才算"下单"完整流程。
 * - 服务器收到RPC后校验，通过的entity直接调用共享的FAsceMoveOrderExecutor写入Order Fragment
 *   （客户端本地预测调用的是同一份Executor，保证两边行为一致，不会出现"两处逻辑各写一遍、
 *   以后改一边忘了改另一边"的情况）。
 * - 拒绝分两个粒度：整批拒绝（ClientRejectWholeOrder，比如Subsystem缺失/请求本身就不合法，
 *   这批entity一个都没处理）和单个entity拒绝（ClientRejectEntityOrder，批里大部分都成功了，
 *   只有个别entity失败）。客户端应该区分这两种情况分别处理：整批拒绝意味着这次下单的本地预测
 *   全部要回滚/暂停；单个拒绝只需要处理那一个entity。
 * - **没有手动攻击指令**：攻击由自动攻击承接（见 `Combat/技能系统设计.md`）；历史上的
 *   `ServerRequestAttackOrder` / `FAsceAttackOrderFragment` 已移除，不要再加回来。
 */
UCLASS(ClassGroup=(Mass), meta=(BlueprintSpawnableComponent))
class UAsceOrderChannelComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAsceOrderChannelComponent();

	/** 一次下单最多选中的单位数量，超过直接在_Validate阶段拒绝（防作弊/防止单次请求过大） */
	static constexpr int32 MaxOrderSelectionCount = 250;

	/** 客户端本地预测开关（默认**关**）：客户端不跑移动模拟/碰撞，位置完全由服务器同步驱动，
	 *  下单只发 RPC。以后要恢复"点击立刻动"的短窗口预测时打开它（需配套：服务器 ack 后对齐一次、
	 *  预测窗口内抑制本地碰撞）。 */
	UPROPERTY(EditAnywhere, Category = "Asce|Order")
	bool bEnableClientPrediction = false;

	/**
	 * 客户端本地入口（非RPC）：传入本地选中的entity（客户端自己Mass EntityManager里的Handle，
	 * 不是NetID——这是本地selection的自然产物，不需要反查），内部负责：
	 * 1）读取每个entity的FMassNetworkIDFragment.NetID，拼成数组，调用ServerRequestMoveOrder；
	 * 2）本地预测默认**关闭**（`bEnableClientPrediction=false`）：客户端不跑移动模拟/碰撞，
	 *    位置完全由服务器同步驱动；打开该开关时才会走下面的旧预测逻辑（需配套 ack 对齐）。
	 *    Standalone/ListenServer下Owner本身就是Authority，RPC会在本地同步直接跑到
	 *    _Implementation，这里就不再重复执行一遍，避免PathPlan被多Acquire一次导致引用计数泄漏。
	 */
	void RequestMoveOrder(const TArray<FMassEntityHandle>& LocalSelectedHandles, FVector2D GoalWorldLocation, float AcceptanceRadiusSquared);

	/** `S` 停止指令（客户端入口）：清移动命令 + 取消施法（**不影响自动攻击开关**）。
	 *  v1 不做本地预测（服务器立即生效，位置修正会跟上）。见 `Combat/技能系统设计.md` §3.2 */
	void RequestStopOrder(const TArray<FMassEntityHandle>& LocalSelectedHandles);

	/** 配置技能槽位：把技能 `ActionId` 放进槽位 `SlotIndex`（1/2/3 ↔ Q/W/E）。
	 *  该技能已在别的槽位 ⇒ **两槽交换**（技能连同 CD 一起换位，见 `Combat/技能系统设计.md` §2.2）；
	 *  新技能 ⇒ 写入并 CD 归零；`ActionId == 0` ⇒ 清空该槽位。 */
	void RequestSetSkillSlot(const TArray<FMassEntityHandle>& LocalSelectedHandles, int32 SlotIndex, int32 ActionId);

	/** 释放技能（客户端入口）：`SlotId` 是槽位号（1/2/3 ↔ Q/W/E），服务器按**该单位**槽位里的技能解析。
	 *  客户端不判断技能与目标类型：统一带上 `TargetPos` 与 `TargetNetID`（可无效），服务器按技能 def 取用。
	 *  **客户端预测表现**（`Combat/技能系统设计.md` §3.1）：本地按已复制数据跑一遍同序校验，通过就把
	 *  `FAsceCombatVisualFragment` 写成本次施法（立刻播前摇 + 转身）；**指令始终照发**。
	 *  `TargetLocalHandle` = 目标在本端的实体句柄（指向技能用；客户端没有全局 NetID 反查表，由调用方给）。 */
	void RequestSkillOrder(const TArray<FMassEntityHandle>& LocalSelectedHandles, int32 SlotId, FVector2D TargetPos, FMassNetworkID TargetNetID, FMassEntityHandle TargetLocalHandle = FMassEntityHandle());

	/** 客户端框选N个单位后发起移动指令，Goal是2D世界坐标（跟FlowField系统一致，不含Z）。
	 * AcceptanceRadiusSquared是到达判定半径的平方值（见AsceOrderFragments.h），
	 * 会随RPC透传给服务器，保证Server执行和客户端预测的到达判定一致。
	 * 正常不要直接调用这个，走上面的RequestMoveOrder（它会顺带处理本地预测）。
	 * OrderSequence由调用方自行递增分配，仅用于拒绝回执让客户端知道具体是哪一次下单被拒绝，
	 * 服务器不校验其单调性/唯一性（不作为防作弊依据，纯粹是客户端自己的关联ID）。 */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRequestMoveOrder(const TArray<FMassNetworkID>& SelectedNetIDs, FVector2D GoalWorldLocation, float AcceptanceRadiusSquared, int32 OrderSequence);

	/** `S` 停止指令（服务器权威）：逐单位清移动命令 + 取消施法 */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRequestStopOrder(const TArray<FMassNetworkID>& SelectedNetIDs, int32 OrderSequence);

	/** 配置技能槽位（服务器权威）：**槽位即"拥有"**（没有独立的技能拥有清单）——
	 *  校验技能在动作表里存在且为主动技能 + `NotOwned`，然后写入/交换槽位 */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRequestSetSkillSlot(const TArray<FMassNetworkID>& SelectedNetIDs, int32 SlotIndex, int32 ActionId, int32 OrderSequence);

	/** 释放技能（服务器权威）：逐单位"**先无条件停止移动 → 校验（槽位/CD/蓝/目标/射程）→ 进施法动作**"。
	 *  射程不够/目标不合法/没蓝/CD 中 ⇒ **停在原地不放**（不移动过去） */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRequestSkillOrder(const TArray<FMassNetworkID>& SelectedNetIDs, int32 SlotId, FVector2D TargetPos, FMassNetworkID TargetNetID, int32 OrderSequence);

	/** 整批请求被拒绝：这一次OrderSequence下的所有entity都没有被处理
	 * （比如依赖的Subsystem缺失、请求本身不合法）。客户端收到后应该把这次下单在本地预测里
	 * 涉及的所有entity都回滚/暂停，而不是只处理某一个。 */
	UFUNCTION(Client, Reliable)
	void ClientRejectWholeOrder(int32 OrderSequence, EAsceOrderRejectReason Reason);

	/** 单个entity被拒绝：批里其它entity可能已经成功处理，只有UnitNetID对应的这一个失败
	 * （查不到/没有对应Fragment）。客户端只需要处理这一个entity（比如撤销它的本地预测路径）。 */
	UFUNCTION(Client, Reliable)
	void ClientRejectEntityOrder(int32 OrderSequence, FMassNetworkID UnitNetID, EAsceOrderRejectReason Reason);

	/** 技能释放：一次请求的**逐单位拒绝明细**（**一次请求一条**，与 `ServerRequestSkillOrder` 对称；空数组不发）。
	 *  客户端按 `OrderSequence` 反查本次下单的本地 entity，逐个撤销预测（见 `Combat/技能系统设计.md` §3.1）。 */
	UFUNCTION(Client, Reliable)
	void ClientRejectSkillOrder(int32 OrderSequence, const TArray<FAsceEntityReject>& RejectedUnits);

private:
	// 发起本次 RPC 的玩家的**团队身份 TeamId**（Owner→Controller→GenericTeamId；取不到 → 0 中立）。
	// 指挥授权用 TeamId 比较（跨端一致；TeamIndex 只是各端自己的下标）
	int32 GetRequesterTeamId();

	bool ServerRequestMoveOrder_Validate(const TArray<FMassNetworkID>& SelectedNetIDs, FVector2D GoalWorldLocation, float AcceptanceRadiusSquared, int32 OrderSequence);
	void ServerRequestMoveOrder_Implementation(const TArray<FMassNetworkID>& SelectedNetIDs, FVector2D GoalWorldLocation, float AcceptanceRadiusSquared, int32 OrderSequence);


	bool ServerRequestStopOrder_Validate(const TArray<FMassNetworkID>& SelectedNetIDs, int32 OrderSequence);
	void ServerRequestStopOrder_Implementation(const TArray<FMassNetworkID>& SelectedNetIDs, int32 OrderSequence);

	bool ServerRequestSetSkillSlot_Validate(const TArray<FMassNetworkID>& SelectedNetIDs, int32 SlotIndex, int32 ActionId, int32 OrderSequence);
	void ServerRequestSetSkillSlot_Implementation(const TArray<FMassNetworkID>& SelectedNetIDs, int32 SlotIndex, int32 ActionId, int32 OrderSequence);

	bool ServerRequestSkillOrder_Validate(const TArray<FMassNetworkID>& SelectedNetIDs, int32 SlotId, FVector2D TargetPos, FMassNetworkID TargetNetID, int32 OrderSequence);
	void ServerRequestSkillOrder_Implementation(const TArray<FMassNetworkID>& SelectedNetIDs, int32 SlotId, FVector2D TargetPos, FMassNetworkID TargetNetID, int32 OrderSequence);

	void ClientRejectWholeOrder_Implementation(int32 OrderSequence, EAsceOrderRejectReason Reason);
	void ClientRejectEntityOrder_Implementation(int32 OrderSequence, FMassNetworkID UnitNetID, EAsceOrderRejectReason Reason);
	void ClientRejectSkillOrder_Implementation(int32 OrderSequence, const TArray<FAsceEntityReject>& RejectedUnits);

	/** 客户端本地递增的下单序号分配器，纯本地状态，不需要跟服务器同步（服务器只是原样透传收到的值）。 */
	int32 NextOrderSequence = 0;

	/** 客户端专用：记录每次本地预测下单时实际用到的LocalSelectedHandles，供收到拒绝回执时
	 * 反查该找哪些本地entity处理（不需要额外的NetID->EntityHandle反查表——见架构文档4.1节，
	 * 客户端本来就没有服务器那份GlobalNetIDRegistry，但这里从一开始存的就是本地EntityHandle，
	 * 天然不需要反查）。为避免无限增长，超过PendingLocalOrdersCapacity会丢弃最旧的条目
	 * （对应订单早就该被服务器确认/拒绝过了，理论上不应该堆积到这个量级）。 */
	TMap<int32, TArray<FMassEntityHandle>> PendingLocalOrders;
	static constexpr int32 PendingLocalOrdersCapacity = 64;
};
