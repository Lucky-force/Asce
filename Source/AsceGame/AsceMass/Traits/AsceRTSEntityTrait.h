// AsceRTSEntityTrait.h
#pragma once

#include "MassEntityTraitBase.h"
#include "DataTableSystem/AsceRowNameWrapper.h"
#include "AsceRTSEntityTrait.generated.h"

/** 复制 LOD 调优（映射到引擎 `FMassReplicationParameters`；`ReplicatorClass`/`BubbleInfoClass` 由代码强制，不暴露）。 */
USTRUCT()
struct FAsceReplicationTuning
{
	GENERATED_BODY()

	/** LOD 距离（cm）：超过对应距离降到下一档；Off 之外不再复制。默认 = 引擎默认值。 */
	UPROPERTY(EditAnywhere, Category = "LOD") float LODDistanceHigh = 0.f;
	UPROPERTY(EditAnywhere, Category = "LOD") float LODDistanceMedium = 1000.f;
	UPROPERTY(EditAnywhere, Category = "LOD") float LODDistanceLow = 2500.f;
	UPROPERTY(EditAnywhere, Category = "LOD") float LODDistanceOff = 5000.f;

	/** 距离档之间的滞回百分比（防抖动反复切档） */
	UPROPERTY(EditAnywhere, Category = "LOD") float BufferHysteresisOnDistancePercentage = 10.f;

	/** 每档全局实体数上限（0 = 不限；引擎默认 1600/3200/48000/0） */
	UPROPERTY(EditAnywhere, Category = "LOD") int32 LODMaxCountHigh = 1600;
	UPROPERTY(EditAnywhere, Category = "LOD") int32 LODMaxCountMedium = 3200;
	UPROPERTY(EditAnywhere, Category = "LOD") int32 LODMaxCountLow = 48000;
	UPROPERTY(EditAnywhere, Category = "LOD") int32 LODMaxCountOff = 0;

	/** 每档每视野者的实体数上限（引擎默认 100/200/300/0） */
	UPROPERTY(EditAnywhere, Category = "LOD") int32 LODMaxCountPerViewerHigh = 100;
	UPROPERTY(EditAnywhere, Category = "LOD") int32 LODMaxCountPerViewerMedium = 200;
	UPROPERTY(EditAnywhere, Category = "LOD") int32 LODMaxCountPerViewerLow = 300;
	UPROPERTY(EditAnywhere, Category = "LOD") int32 LODMaxCountPerViewerOff = 0;

	/** 每档的复制更新间隔（秒；引擎默认 0.1/0.2/0.3/0.5） */
	UPROPERTY(EditAnywhere, Category = "LOD") float UpdateIntervalHigh = 0.1f;
	UPROPERTY(EditAnywhere, Category = "LOD") float UpdateIntervalMedium = 0.2f;
	UPROPERTY(EditAnywhere, Category = "LOD") float UpdateIntervalLow = 0.3f;
	UPROPERTY(EditAnywhere, Category = "LOD") float UpdateIntervalOff = 0.5f;
};

/**
 * 实体**公共核心** Trait（单位 / 建筑都挂）：
 * - 身份：`FAsceUnitDefFragment`（`UniqueType` = 实体定义表行名数字，跨 session/两端一致）；
 * - 公共部分：Transform / 阵营 / 血量（可关） / NetID（可关） / AliveTag；
 * - **复制组**（`bReplicateEntity`，默认开）：委托引擎 `UMassReplicationTrait` 加复制 fragment + 组参数
 *   （`ReplicatorClass=UAsceUnitReplicator`、代表 `BubbleInfoClass` 代码强制；LOD 参数见 `ReplicationTuning`）。
 *   **模板不要再手动加引擎的 "Replication" trait**——同类型 params 加两次第二次会被忽略/ensure。
 *   ⇒ 实体只要挂上能力 fragment（Health/Mana/Team/Transform…）就会自动同步，无需逐模板配复制。
 *
 * 单位专有内容见 `UAsceRTSUnitTrait`（移动/视野/订单/能力），建筑见 `UAsceRTSBuildingTrait`
 * （footprint/建筑标记）；MassEntityConfig 里按需组合。
 */
UCLASS(BlueprintType, meta = (DisplayName = "Asce RTS Entity"))
class UAsceRTSEntityTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

public:
	/** 实体类型（`AsceEntityDefRow` 行名 = 数字 ID；下拉选行）。 */
	UPROPERTY(EditAnywhere, Category = "Definition",
		meta = (DataTableBinding = "AsceEntityDefRow:DisplayName,Description"))
	FAsceRowNameWrapper UniqueType;

	// 注意：**阵营不在这里配**——归属取决于谁生成/建造，由生成方在创建时写入
	// （单位走 `UAsceEntityFactory` 的 TeamKey 参数；建筑走 `FAsceBuildingPlacement::TeamKey`）。

	UPROPERTY(EditAnywhere, Category = "Attributes")
	bool bAddHealth = true;

	UPROPERTY(EditAnywhere, Category = "Attributes", meta = (EditCondition = "bAddHealth"))
	float MaxHealth = 100.f;

	/** 联机同步用的 NetID（服务器权威分配，见 `UAsceNetIDRegistrySubsystem`）。 */
	UPROPERTY(EditAnywhere, Category = "Replication")
	bool bAddNetworkID = true;

	/** 是否参与 MassReplication（自动委托引擎 Replication trait 加复制组；单机自动跳过）。
	 *  能力（fragment）→ 自动同步即由此承接；**不要再给模板手动加引擎 "Replication" trait**。 */
	UPROPERTY(EditAnywhere, Category = "Replication")
	bool bReplicateEntity = true;

	/** 复制 LOD 调优（等价引擎 "Replication" trait 的那组参数） */
	UPROPERTY(EditAnywhere, Category = "Replication", meta = (EditCondition = "bReplicateEntity"))
	FAsceReplicationTuning ReplicationTuning;

protected:
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
};
