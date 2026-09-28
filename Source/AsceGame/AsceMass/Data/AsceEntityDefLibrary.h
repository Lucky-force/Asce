// AsceEntityDefLibrary.h
#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "AsceMass/Data/AsceEntityDefTypes.h"
#include "AsceMass/Building/AsceBuildingDef.h"
#include "AsceMass/Ether/AsceEtherBuildingDef.h"
#include "AsceEntityDefLibrary.generated.h"

/**
 * 实体定义表（`FAsceEntityDefRow`）查询/校验的统一入口（运行时查表只走这里）。
 * 行名 = 数字 ID 字符串（0..65535）。
 */
class UWorld;

UCLASS()
class UAsceEntityDefLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 把定义表里所有行的 `EntityTemplate` 在当前 World 预注册（`GetOrCreateEntityTemplate`）。
	 *  **服务器和客户端都必须跑**：客户端不跑 EntityFactory（生成被权威端拦截），但 Mass 复制
	 *  在客户端是按 TemplateID 本地 spawn 实体的（bubble PostReplicatedAdd →
	 *  UMassSpawnerSubsystem::SpawnEntities），客户端没注册模板会命中
	 *  checkf("SpawnEntities: TemplateID must have been registered!")。
	 *  模板 id 由 UMassEntityConfigAsset 路径的确定性 GUID 决定，两端一致。
	 *  @return 成功预注册的模板数（表缺失/行未配模板会跳过并告警）。 */
	static int32 PreRegisterAllEntityTemplates(UWorld& World);
	/** 行名 → ID（必须全数字且在 0..65535）；失败返回 false。 */
	static bool ParseEntityDefId(FName RowName, uint16& OutId);

	/** 行名 → ID 的**强制版**：非法（非数字串 / 0 / >65535）**直接 check 崩**。
	 *  用在"身份必须正确"的路径（建筑创建、trait 构建、调试器建实体）：静默变 0 会让实体在
	 *  Ether 里"没有端口"（BuildingId="0" 查不到 def）或注册成错误的类型，极难排查 ⇒ 数据错误就崩。 */
	static uint16 ParseEntityDefIdChecked(FName RowName);

	/** 按 ID 取定义行（行名 = `FString::FromInt(Id)`）。失败不动 OutRow。 */
	UFUNCTION(BlueprintCallable, Category = "Asce|EntityDef")
	static bool GetEntityDefRow(int32 Id, FAsceEntityDefRow& OutRow);

	/** 按行名取定义行。失败不动 OutRow。 */
	static bool GetEntityDefRowByName(FName RowName, FAsceEntityDefRow& OutRow);

	/** 取 Ether 建筑定义（`Def` 必须是 `FAsceEtherBuildingDef`）。失败不动 OutDef。 */
	UFUNCTION(BlueprintCallable, Category = "Asce|EntityDef")
	static bool GetEtherBuildingDef(int32 Id, FAsceEtherBuildingDef& OutDef);

	static bool GetEtherBuildingDefByName(FName RowName, FAsceEtherBuildingDef& OutDef);

	/** 取**建筑通用**定义（`Def` 是 `FAsceBuildingDefBase` 子类即可，不限 Ether）。失败不动 OutDef。 */
	UFUNCTION(BlueprintCallable, Category = "Asce|EntityDef")
	static bool GetBuildingDef(int32 Id, FAsceBuildingDefBase& OutDef);

	static bool GetBuildingDefByName(FName RowName, FAsceBuildingDefBase& OutDef);

	/** 校验整张定义表（行名数字/范围、`Def` 有效且是基类子类）；错误追加到 OutErrors，返回错误条数。 */
	UFUNCTION(BlueprintCallable, Category = "Asce|EntityDef")
	static int32 ValidateEntityDefTable(TArray<FString>& OutErrors);
};
