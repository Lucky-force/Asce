// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassClientBubbleHandler.h"
#include "MassEntityView.h"
#include "MassEntityQuery.h"
#include "MassExecutionContext.h"
#include "AsceReplicationValueHandlers.generated.h"

/**
 * 通用"值同步"能力的数据/Handler 层（按**基本类型**泛化，且支持**一个 Agent 多字段**）。
 *
 * 设计：
 * - UHT 不支持给 USTRUCT 加模板参数，也不支持用宏生成反射声明（引擎硬限制）
 *   ⇒ **每种基本类型手写一个载荷 USTRUCT**（下面这一排），它们都提供 `SetValue/GetValue`，
 *   值类型就是各自的原始类型；
 * - 一个 Channel 的 Agent 里可以放**多个**载荷成员（例：Health = MaxHealth + CurrentHealth），
 *   Policy 用 `NumFields` + `FieldIndex` 把它们和 Fragment 字段一一对应；
 * - Handler/Capability/BubbleHandler 全是模板，新增能力只需要：选/加载荷类型 + 写一个 Policy。
 *
 * 新增一个"值同步"能力（例：技能 CD、旗帜归属……）：
 *   1. 选/加载荷类型（FReplicatedValueXxx）；Agent 里放 `NumFields` 个具名成员；
 *   2. 写 FieldPolicy（FragmentType / ValueType / FValueData / NumFields / 三个类型别名 /
 *      GetMutableData(Agent, Field) / GetData(Agent, Field) /
 *      ExtractFromFragment(Fragment, Field) / ApplyToFragment(Fragment, Field, Value) /
 *      ValuesDiffer(New, Old, Field)）；
 *   3. `using FAsceXxxClientBubbleHandler = TAsceValueClientBubbleHandler<...>;`
 *   4. `using FAsceXxxReplicationCapability = TAsceValueReplicationCapability<...>;`
 *   5. Bootstrap 注册 capability + BubbleInfoClass（同 Health/Mana/Team）。
 */

// ────────────────────────────────────────────
// 载荷 USTRUCT（每种基本类型一个）
// ────────────────────────────────────────────

USTRUCT()
struct FReplicatedValueUInt8
{
	GENERATED_BODY()
	void SetValue(const uint8 InValue) { Value = InValue; }
	uint8 GetValue() const { return Value; }
private:
	UPROPERTY(Transient)
	uint8 Value = 0;
};

USTRUCT()
struct FReplicatedValueUInt16
{
	GENERATED_BODY()
	void SetValue(const uint16 InValue) { Value = InValue; }
	uint16 GetValue() const { return Value; }
private:
	UPROPERTY(Transient)
	uint16 Value = 0;
};

USTRUCT()
struct FReplicatedValueUInt32
{
	GENERATED_BODY()
	void SetValue(const uint32 InValue) { Value = InValue; }
	uint32 GetValue() const { return Value; }
private:
	UPROPERTY(Transient)
	uint32 Value = 0;
};

USTRUCT()
struct FReplicatedValueUInt64
{
	GENERATED_BODY()
	void SetValue(const uint64 InValue) { Value = InValue; }
	uint64 GetValue() const { return Value; }
private:
	UPROPERTY(Transient)
	uint64 Value = 0;
};

USTRUCT()
struct FReplicatedValueInt8
{
	GENERATED_BODY()
	void SetValue(const int8 InValue) { Value = InValue; }
	int8 GetValue() const { return Value; }
private:
	UPROPERTY(Transient)
	int8 Value = 0;
};

USTRUCT()
struct FReplicatedValueInt16
{
	GENERATED_BODY()
	void SetValue(const int16 InValue) { Value = InValue; }
	int16 GetValue() const { return Value; }
private:
	UPROPERTY(Transient)
	int16 Value = 0;
};

USTRUCT()
struct FReplicatedValueInt32
{
	GENERATED_BODY()
	void SetValue(const int32 InValue) { Value = InValue; }
	int32 GetValue() const { return Value; }
private:
	UPROPERTY(Transient)
	int32 Value = 0;
};

USTRUCT()
struct FReplicatedValueInt64
{
	GENERATED_BODY()
	void SetValue(const int64 InValue) { Value = InValue; }
	int64 GetValue() const { return Value; }
private:
	UPROPERTY(Transient)
	int64 Value = 0;
};

USTRUCT()
struct FReplicatedValueFloat
{
	GENERATED_BODY()
	void SetValue(const float InValue) { Value = InValue; }
	float GetValue() const { return Value; }
private:
	UPROPERTY(Transient)
	float Value = 0.f;
};

USTRUCT()
struct FReplicatedValueDouble
{
	GENERATED_BODY()
	void SetValue(const double InValue) { Value = InValue; }
	double GetValue() const { return Value; }
private:
	UPROPERTY(Transient)
	double Value = 0.0;
};

USTRUCT()
struct FReplicatedValueBool
{
	GENERATED_BODY()
	void SetValue(const bool InValue) { Value = InValue; }
	bool GetValue() const { return Value; }
private:
	UPROPERTY(Transient)
	bool Value = false;
};

// ────────────────────────────────────────────
// 客户端 Handler（生成实体时/条目变化时，把同步值写回 Fragment）
// ────────────────────────────────────────────

/**
 * 客户端侧的通用值同步 Handler（模板）。
 *
 * FieldPolicy 需要提供：
 *   static constexpr int32 NumFields;                 该 Agent 里的字段数
 *   using FValueData = FReplicatedValueXxx;           与 ValueType 对应的载荷结构
 *   using ValueType  = ...;                           原始类型（float/uint16/...）
 *   static void ApplyToFragment(FragmentType&, int32 FieldIndex, ValueType);
 *   static const FValueData& GetData(const FAgentType&, int32 FieldIndex);
 *
 * 字段按 `FieldIndex` **升序**写回（字段之间有依赖时，把"被依赖的"放小下标，
 * 例：Health 的 0 = MaxHealth、1 = CurrentHealth）。
 */
template<typename AgentArrayItem, typename FieldPolicy>
class TMassClientBubbleValueHandler
{
public:
	typedef typename FieldPolicy::FragmentType FFragmentType;
	typedef typename FieldPolicy::FAgentType FAgentType;

	explicit TMassClientBubbleValueHandler(TClientBubbleHandlerBase<AgentArrayItem>& InOwnerHandler)
		: OwnerHandler(InOwnerHandler)
	{}

#if UE_REPLICATION_COMPILE_CLIENT_CODE
	static void AddRequirementsForSpawnQuery(FMassEntityQuery& InQuery)
	{
		InQuery.AddRequirement<FFragmentType>(EMassFragmentAccess::ReadWrite);
	}

	void CacheFragmentViewsForSpawnQuery(FMassExecutionContext& InExecContext)
	{
		FragmentList = InExecContext.GetMutableFragmentView<FFragmentType>();
	}

	void ClearFragmentViewsForSpawnQuery()
	{
		FragmentList = TArrayView<FFragmentType>();
	}

	void SetSpawnedEntityData(const int32 EntityIdx, const FAgentType& Agent) const
	{
		FFragmentType& Fragment = FragmentList[EntityIdx];
		for (int32 FieldIndex = 0; FieldIndex < FieldPolicy::NumFields; ++FieldIndex)
		{
			FieldPolicy::ApplyToFragment(Fragment, FieldIndex, FieldPolicy::GetData(Agent, FieldIndex).GetValue());
		}
	}

	static void SetModifiedEntityData(const FMassEntityView& EntityView, const FAgentType& Agent)
	{
		FFragmentType& Fragment = EntityView.GetFragmentData<FFragmentType>();
		for (int32 FieldIndex = 0; FieldIndex < FieldPolicy::NumFields; ++FieldIndex)
		{
			FieldPolicy::ApplyToFragment(Fragment, FieldIndex, FieldPolicy::GetData(Agent, FieldIndex).GetValue());
		}
	}
#endif // UE_REPLICATION_COMPILE_CLIENT_CODE

protected:
	TArrayView<FFragmentType> FragmentList;
	TClientBubbleHandlerBase<AgentArrayItem>& OwnerHandler;
};

// ────────────────────────────────────────────
// 服务器 Handler（从 Fragment 提取值 → 写 Agent、判脏）
// ────────────────────────────────────────────

/**
 * 服务器侧的通用值同步 Handler（模板）。
 * ModifyEntity 最终调 `BubbleHandlerType::SetValueFromFragment(...)`——逐字段比较 + 统一标脏，
 * 实现在 `TAsceValueClientBubbleHandler` 模板里（见 Core/AsceValueReplicationBubbleHandler.h）。
 *
 * Presence 默认 Any：会被合并进 UAsceUnitReplicator 共用的 EntityQuery；
 * 用 All 会导致"必须同时具备所有能力"，Optional 又会让"没有任何能力的实体也白跑"。
 */
template<typename FieldPolicy>
class TMassReplicationProcessorValueHandler
{
public:
	typedef typename FieldPolicy::FragmentType FFragmentType;
	typedef typename FieldPolicy::FAgentType FAgentType;

	static void AddRequirements(FMassEntityQuery& InQuery, const EMassFragmentPresence Presence = EMassFragmentPresence::Any)
	{
		InQuery.AddRequirement<FFragmentType>(EMassFragmentAccess::ReadOnly, Presence);
	}

	void CacheFragmentViews(FMassExecutionContext& ExecContext)
	{
		FragmentList = ExecContext.GetFragmentView<FFragmentType>();
	}

	void AddEntity(const int32 EntityIdx, FAgentType& InOutAgent) const
	{
		for (int32 FieldIndex = 0; FieldIndex < FieldPolicy::NumFields; ++FieldIndex)
		{
			FieldPolicy::GetMutableData(InOutAgent, FieldIndex)
				.SetValue(FieldPolicy::ExtractFromFragment(FragmentList[EntityIdx], FieldIndex));
		}
	}

	template<typename BubbleHandlerType>
	void ModifyEntity(const FMassReplicatedAgentHandle Handle, const int32 EntityIdx, BubbleHandlerType& Bubble) const
	{
		Bubble.SetValueFromFragment(Handle, FragmentList[EntityIdx]);
	}

protected:
	TConstArrayView<FFragmentType> FragmentList;
};
