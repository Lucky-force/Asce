// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Components/ActorComponent.h"
#include "MassCommonTypes.h"
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
 * - 攻击指令目前仍是"查表+写Fragment"的简单版本，本质上也需要先寻路到目标附近再攻击，
 *   这部分留到后续专门讨论，先不接入本地预测/Executor共享逻辑。
 */
UCLASS(ClassGroup=(Mass), meta=(BlueprintSpawnableComponent))
class UAsceOrderChannelComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAsceOrderChannelComponent();

	/** 一次下单最多选中的单位数量，超过直接在_Validate阶段拒绝（防作弊/防止单次请求过大） */
	static constexpr int32 MaxOrderSelectionCount = 250;

	/**
	 * 客户端本地入口（非RPC）：传入本地选中的entity（客户端自己Mass EntityManager里的Handle，
	 * 不是NetID——这是本地selection的自然产物，不需要反查），内部负责：
	 * 1）读取每个entity的FMassNetworkIDFragment.NetID，拼成数组，调用ServerRequestMoveOrder；
	 * 2）如果Owner不是Authority（真正的纯客户端），额外立即在本地EntityManager+PathFinder上
	 *    跑一遍FAsceMoveOrderExecutor::ApplyMergedMoveOrder做客户端预测，不等服务器确认——
	 *    这样单位能立刻响应玩家点击，若干帧后服务器的确认/拒绝到达时再做一致性处理。
	 *    Standalone/ListenServer下Owner本身就是Authority，RPC会在本地同步直接跑到
	 *    _Implementation，这里就不再重复执行一遍，避免PathPlan被多Acquire一次导致引用计数泄漏。
	 */
	void RequestMoveOrder(const TArray<FMassEntityHandle>& LocalSelectedHandles, FVector2D GoalWorldLocation, float AcceptanceRadiusSquared);

	/** 客户端框选N个单位后发起移动指令，Goal是2D世界坐标（跟FlowField系统一致，不含Z）。
	 * AcceptanceRadiusSquared是到达判定半径的平方值（见AsceOrderFragments.h），
	 * 会随RPC透传给服务器，保证Server执行和客户端预测的到达判定一致。
	 * 正常不要直接调用这个，走上面的RequestMoveOrder（它会顺带处理本地预测）。
	 * OrderSequence由调用方自行递增分配，仅用于拒绝回执让客户端知道具体是哪一次下单被拒绝，
	 * 服务器不校验其单调性/唯一性（不作为防作弊依据，纯粹是客户端自己的关联ID）。 */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRequestMoveOrder(const TArray<FMassNetworkID>& SelectedNetIDs, FVector2D GoalWorldLocation, float AcceptanceRadiusSquared, int32 OrderSequence);

	/** 客户端框选N个单位后发起攻击指令，目标同样用NetID寻址（见AsceOrderFragments.h的说明）。
	 * 攻击指令的本地预测/Executor共享逻辑留到后续讨论，目前只是单纯的服务器权威写入。 */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRequestAttackOrder(const TArray<FMassNetworkID>& SelectedNetIDs, FMassNetworkID TargetNetID, int32 OrderSequence);

	/** 整批请求被拒绝：这一次OrderSequence下的所有entity都没有被处理
	 * （比如依赖的Subsystem缺失、请求本身不合法）。客户端收到后应该把这次下单在本地预测里
	 * 涉及的所有entity都回滚/暂停，而不是只处理某一个。 */
	UFUNCTION(Client, Reliable)
	void ClientRejectWholeOrder(int32 OrderSequence, EAsceOrderRejectReason Reason);

	/** 单个entity被拒绝：批里其它entity可能已经成功处理，只有UnitNetID对应的这一个失败
	 * （查不到/没有对应Fragment）。客户端只需要处理这一个entity（比如撤销它的本地预测路径）。 */
	UFUNCTION(Client, Reliable)
	void ClientRejectEntityOrder(int32 OrderSequence, FMassNetworkID UnitNetID, EAsceOrderRejectReason Reason);

private:
	bool ServerRequestMoveOrder_Validate(const TArray<FMassNetworkID>& SelectedNetIDs, FVector2D GoalWorldLocation, float AcceptanceRadiusSquared, int32 OrderSequence);
	void ServerRequestMoveOrder_Implementation(const TArray<FMassNetworkID>& SelectedNetIDs, FVector2D GoalWorldLocation, float AcceptanceRadiusSquared, int32 OrderSequence);

	bool ServerRequestAttackOrder_Validate(const TArray<FMassNetworkID>& SelectedNetIDs, FMassNetworkID TargetNetID, int32 OrderSequence);
	void ServerRequestAttackOrder_Implementation(const TArray<FMassNetworkID>& SelectedNetIDs, FMassNetworkID TargetNetID, int32 OrderSequence);

	void ClientRejectWholeOrder_Implementation(int32 OrderSequence, EAsceOrderRejectReason Reason);
	void ClientRejectEntityOrder_Implementation(int32 OrderSequence, FMassNetworkID UnitNetID, EAsceOrderRejectReason Reason);

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
