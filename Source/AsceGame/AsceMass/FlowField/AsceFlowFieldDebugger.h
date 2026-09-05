// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#if WITH_EDITORONLY_DATA
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AsceFlowFieldCores.h"
#include "AscePathFinder.h"
#include "AsceFlowFieldDebugger.generated.h"

class UAsceRTSCommanderComponent;
class UAscePathFindingSubsystem;

/**
 * 流场寻路调试器：
 *  - BeginPlay 初始化 WorldMapData + PathFinder
 *  - 可选生成测试墙（矩形障碍物）
 *  - 右键点击：从地图中心网格寻路到点击位置，绘制 Portal 链 / LOS / 积分场 / 流场
 *  - 持续绘制：扇区网格、墙壁、传送门
 */
UCLASS()
class AAsceFlowFieldDebugger : public AActor
{
	GENERATED_BODY()

public:
	AAsceFlowFieldDebugger(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// UAscePathFindingSubsystem::RegisterFlowFieldDebugger 用来把PathFinder换网到这份数据上
	FWorldMapData* GetWorldMapDataForDebugger() { return &WorldMapData; }
	UPathFinder* GetPathFinderForDebugger() { return PathFinder; }

	// ── 配置 ───────────────────────────────────────────
	UPROPERTY(EditAnywhere, Category = "FlowFieldDebug")
	int32 SectorCountX = 4;

	UPROPERTY(EditAnywhere, Category = "FlowFieldDebug")
	int32 SectorCountY = 4;

	// 绘制高度（离地面）
	UPROPERTY(EditAnywhere, Category = "FlowFieldDebug")
	float DrawHeight = 60.f;

	// 只绘制玩家周围该半径范围内的内容（世界单位），0 = 不限制
	UPROPERTY(EditAnywhere, Category = "FlowFieldDebug")
	float DebugDrawRange = 4000.f;

	// 玩家移动触发跟随重绘的最小间隔（秒），防止拖动时每帧全量重绘
	UPROPERTY(EditAnywhere, Category = "FlowFieldDebug")
	float DebugRedrawInterval = 0.25f;

	// 生成测试墙: (GridX, GridY, 宽(格), 高(格))
	UPROPERTY(EditAnywhere, Category = "FlowFieldDebug")
	bool bGenerateTestWalls = true;

	UPROPERTY(EditAnywhere, Category = "FlowFieldDebug", meta = (EditCondition = "bGenerateTestWalls"))
	TArray<FVector4> TestWallRects;

	// ── 显示开关 ───────────────────────────────────────
	UPROPERTY(EditAnywhere, Category = "FlowFieldDebug")
	bool bDrawSectorGrid = true;

	UPROPERTY(EditAnywhere, Category = "FlowFieldDebug")
	bool bDrawWalls = true;

	UPROPERTY(EditAnywhere, Category = "FlowFieldDebug")
	bool bDrawPortals = true;

	// 岛屿场着色（不同IslandId不同填充颜色）
	UPROPERTY(EditAnywhere, Category = "FlowFieldDebug")
	bool bDrawIslandFields = true;

	// 每格像素数（地图纹理分辨率）
	UPROPERTY(EditAnywhere, Category = "FlowFieldDebug")
	int32 CellPixelSize = 4;

	// 地图纹理的材质（unlit直出纹理，可复用M_SpatialGridDebug，纹理参数名DebugTexture）
	UPROPERTY(EditAnywhere, Category = "FlowFieldDebug")
	TObjectPtr<UMaterialInterface> DebugDecalMaterial;

	// UV调试开关（纹理方向不对时调整）
	UPROPERTY(EditAnywhere, Category = "FlowFieldDebug")
	bool bSwapUV = false;

	UPROPERTY(EditAnywhere, Category = "FlowFieldDebug")
	bool bFlipU = false;

	UPROPERTY(EditAnywhere, Category = "FlowFieldDebug")
	bool bFlipV = false;

	UPROPERTY(EditAnywhere, Category = "FlowFieldDebug")
	bool bDrawPortalChain = true;

	UPROPERTY(EditAnywhere, Category = "FlowFieldDebug")
	bool bDrawLOS = true;

	UPROPERTY(EditAnywhere, Category = "FlowFieldDebug")
	bool bDrawIntegration = true;

	UPROPERTY(EditAnywhere, Category = "FlowFieldDebug")
	bool bDrawFlowArrows = true;

	// 控制台命令: FlowFieldDebugPath X Y
	UFUNCTION(Exec)
	void FlowFieldDebugPath(float WorldX, float WorldY);

	// 控制台命令: 清除所有source标记
	UFUNCTION(Exec)
	void FlowFieldDebugClearSources();

protected:
	// source标记集合（空格键在鼠标位置添加，用于测试合并A*）
	TArray<FVector2D> SourceWorldPositions;

	// 最近一次寻路
	FVector2D LastGoalWorldPosition = FVector2D::ZeroVector;
	bool bHasPathResult = false;
	TArray<FPortalPathResult> LastPathResults;
	TArray<TSharedPtr<FFlowFieldResult>> LastFlowFieldResults;
	// RunMergeablePathRequest返回的Plan句柄，Debugger自己也算一个"entity"，Acquire/Release成对使用
	FMergedPathPlanHandle LastPathPlanHandle;

	void BindDebugInput();
	void OnRightMousePressed();
	void OnSpacePressed();

	void IssueDebugPathRequest(const FVector2D& GoalWorldPos);

	// 选中变化：从Commander的选中entity刷新source位置
	void OnSelectionChanged();
	void RefreshSourcesFromSelection();
	// 监听第一玩家Controller的Pawn绑定/换绑：RTS指挥官组件挂在Pawn上，BeginPlay时Pawn还没上场
	void BindToSelectionSource();
	// OnPossessedPawnChanged回调（AController上的动态委托，NewPawn可能是nullptr）
	UFUNCTION()
	void OnPawnPossessedChanged(APawn* NewPawn, APawn* OldPawn);

	// ── 绘制 ───────────────────────────────────────────
	void RedrawAll();
	void DrawPortals();
	void DrawPortalChain();
	void DrawFlowFieldResults();
	void UpdateMapTexture();
	void FlushTexturePixelUpdates();
	void WriteGridPixelToBuffer(uint8* Pixels, int32 TexWidth, uint32 GridIndex);

	// 世界数据变更回调（编辑器调试：增量更新纹理）
	void OnCostFieldChanged(uint32 GridIndex);
	void OnStructureChanged();

	FVector GridToWorld(const FIntVector2& GridLocation) const;
	FVector2D GetPortalRepresentativePos(const FPortal& Portal) const;

	// 获取玩家2D世界位置；无玩家时返回false
	bool GetPlayerWorldPos(FVector2D& OutPos) const;

	// 是否在玩家绘制范围内（DebugDrawRange<=0 时恒true）
	bool IsInDrawRange(const FVector2D& WorldPos) const;

	// ── 数据 ───────────────────────────────────────────
	UPROPERTY()
	FWorldMapData WorldMapData;

	UPROPERTY()
	TObjectPtr<UPathFinder> PathFinder;

	/** 监听选中变化的Commander（nullptr=没找到，此时source退回空格手动添加） */
	TWeakObjectPtr<UAsceRTSCommanderComponent> SelectionSourceCommander;

	/** OnSelectionChanged的绑定句柄 */
	FDelegateHandle OnSelectionChangedHandle;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> DebugMID;

	UPROPERTY()
	TObjectPtr<UTexture2D> DebugTexture;

	UPROPERTY()
	TObjectPtr<UDecalComponent> DebugDecal;
	
	UPROPERTY(EditAnywhere, Category = "DebugLine")
	float SectorLineThickness = 5.f;
	
	UPROPERTY(EditAnywhere, Category = "DebugLine")
	float GridLineThickness = 3.f;

	// 上次重绘时玩家的位置（用于检测玩家移动触发跟随重绘）
	FVector2D LastRedrawPlayerPos = FVector2D(TNumericLimits<float>::Max(), TNumericLimits<float>::Max());

	// 上次重绘时间（节流用）
	float LastRedrawTime = -TNumericLimits<float>::Max();

	// 上次编辑的网格索引（F/E 拖动编辑时避免同格重复入队）
	uint32 LastEditGridIndex = MAX_uint32;

	// 地图纹理需要全量更新（结构变化时置位，Tick末尾统一重写）
	bool bMapTextureDirty = true;

	// 待增量更新的格子索引（去重集合，Tick末尾批量写像素+一次UpdateResource）
	TSet<uint32> PendingTexturePixelUpdates;

	bool bNeedsRedraw = true;
};

#endif