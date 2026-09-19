// AsceEntityInspectorController.h
#pragma once

#include "CoreMinimal.h"
#include "Tickable.h"
#include "UObject/Object.h"
#include "AsceMass/Data/AsceEntityDefTypes.h"
#include "AsceMass/Building/AsceBuildingCore.h"
#include "UI/Inspector/AsceInspectorTypes.h"
#include "AsceEntityInspectorController.generated.h"

class APlayerController;
class APawn;
class UAsceRTSCommanderComponent;
class UAsceBuildingSubsystem;
class UMassEntitySubsystem;
class UTexture2D;
struct FMassEntityManager;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FAsceInspectorChanged);

/** 头像异步加载完成（`EntityNumber` 对应请求方；`Texture` 失败为 null）。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FAsceOnEntityIconLoaded, int64, EntityNumber, UTexture2D*, Texture);

/**
 * 实体检视器控制器（UI 逻辑层）：
 * - **工作集 = 当前选中集合**（主单位 = 第一个）；面板不可见时**不 tick、不读数据**；
 * - 选择变化 → `OnSelectionChanged`；主单位数值/功能键变化（10Hz 节流 + 快照 diff）→ `OnPrimaryValuesChanged`；
 * - 只读访问 Mass fragment / 实体定义表；操作（功能键）转发给 `UAsceBuildingSubsystem` 的 core；
 * - Widget 取数据用 BlueprintCallable getter，事件用 BlueprintAssignable 委托（Controller 模式，非 MVVM）。
 */
UCLASS(BlueprintType, meta = (DisplayName = "Asce Entity Inspector Controller"))
class UAsceEntityInspectorController : public UObject, public FTickableGameObject
{
	GENERATED_BODY()

public:
	/** 创建（outer = PlayerController，保证 GetWorld；widget 用 UPROPERTY 持有防 GC）。 */
	UFUNCTION(BlueprintCallable, Category = "Asce|UI", meta = (WorldContext = "WorldContextObject"))
	static UAsceEntityInspectorController* Create(UObject* WorldContextObject, APlayerController* PlayerController);

	/** 面板可见性：不可见时不 tick（零开销）。 */
	UFUNCTION(BlueprintCallable, Category = "Asce|UI")
	void SetViewActive(bool bActive);

	UFUNCTION(BlueprintPure, Category = "Asce|UI")
	bool IsViewActive() const { return bViewActive; }

	/** 立刻重建选择/快照并广播（widget Construct 里绑定委托后调一次）。 */
	UFUNCTION(BlueprintCallable, Category = "Asce|UI")
	void RefreshNow();

	// ── 选择数据 ──

	UFUNCTION(BlueprintPure, Category = "Asce|UI")
	int32 GetSelectedCount() const { return Selection.Num(); }

	UFUNCTION(BlueprintCallable, Category = "Asce|UI")
	void GetSelectedEntities(TArray<int64>& OutResult) const;

	UFUNCTION(BlueprintPure, Category = "Asce|UI")
	bool HasPrimaryEntity() const { return PrimaryEntity.IsSet(); }

	UFUNCTION(BlueprintPure, Category = "Asce|UI")
	int64 GetPrimaryEntityNumber() const { return PrimaryEntity.IsSet() ? PrimaryEntity.AsNumber() : 0; }

	UFUNCTION(BlueprintCallable, Category = "Asce|UI")
	void GetSelectionList(TArray<FAsceInspectorListEntry>& OutEntries) const;

	// ── 主单位数据 ──

	/** 主单位的实体定义行（失败返回 false，不动 Out）。 */
	UFUNCTION(BlueprintCallable, Category = "Asce|UI")
	bool GetPrimaryDefRow(FAsceEntityDefRow& OutRow, int32& OutTypeId) const;

	UFUNCTION(BlueprintCallable, Category = "Asce|UI")
	void GetStatRows(TArray<FAsceInspectorStatRow>& OutRows) const;

	UFUNCTION(BlueprintCallable, Category = "Asce|UI")
	void GetActions(TArray<FAsceBuildingAction>& OutActions) const;

	UFUNCTION(BlueprintCallable, Category = "Asce|UI")
	bool ExecuteAction(FName ActionId);

	UFUNCTION(BlueprintPure, Category = "Asce|UI")
	bool IsPrimaryBuilding() const;

	/** **异步**请求某实体的头像（结果走 `OnEntityIconLoaded`；请求期间同实体去重）。 */
	UFUNCTION(BlueprintCallable, Category = "Asce|UI")
	void RequestHeadIcon(int64 EntityNumber);

	// ── 事件 ──

	UPROPERTY(BlueprintAssignable, Category = "Asce|UI")
	FAsceInspectorChanged OnSelectionChanged;

	UPROPERTY(BlueprintAssignable, Category = "Asce|UI")
	FAsceInspectorChanged OnPrimaryValuesChanged;

	UPROPERTY(BlueprintAssignable, Category = "Asce|UI")
	FAsceOnEntityIconLoaded OnEntityIconLoaded;

	//~ Begin FTickableGameObject Interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return bViewActive; }
	virtual UWorld* GetTickableGameObjectWorld() const override;
	//~ End FTickableGameObject Interface

protected:
	virtual void BeginDestroy() override;

private:
	/** Pawn 晚到/重生时重绑指挥官（HUD 先于 Pawn 构建，不能只靠 Tick 重绑）。 */
	UFUNCTION()
	void HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn);

	FMassEntityManager* GetEntityManager() const;
	void RebindCommander();
	void RebuildSelection();
	void RefreshSnapshot(bool bForceBroadcast);
	void SetPrimarySelectionHooks(const FMassEntityHandle& OldPrimary, const FMassEntityHandle& NewPrimary);
	void BuildStatRowsInternal(TArray<FAsceInspectorStatRow>& OutRows) const;
	void BuildActionsInternal(TArray<FAsceBuildingAction>& OutActions) const;

	TWeakObjectPtr<APlayerController> PC;
	TWeakObjectPtr<UAsceRTSCommanderComponent> Commander;
	TWeakObjectPtr<UMassEntitySubsystem> MassSubsystem;
	TWeakObjectPtr<UAsceBuildingSubsystem> BuildingSubsystem;

	TArray<FMassEntityHandle> Selection;
	FMassEntityHandle PrimaryEntity;

	/** 上次的属性快照（"Label|Value"），用于节流 diff。 */
	TArray<FString> PrevStatSnapshot;
	/** 上次的功能键快照（"ActionId|Enabled"）。 */
	TArray<FString> PrevActionSnapshot;

	float UpdateAccumulator = 0.f;
	float UpdateInterval = 0.1f;
	bool bViewActive = false;

	FDelegateHandle CommanderSelectionHandle;

	/** 正在异步加载头像的实体（去重）。 */
	TSet<int64> PendingIconRequests;
};
