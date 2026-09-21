// AsceBuildingDebugger.h
#pragma once

#include "GameFramework/Actor.h"
#include "AsceMass/Building/AsceBuildingTypes.h"
#include "DataTableSystem/AsceRowNameWrapper.h"
#include "AsceBuildingDebugger.generated.h"

/**
 * 建筑系统调试器（开发期用；关卡里摆一个即可，两端都能画，操作只在权威端生效）：
 *
 * - **绘制**：所有建筑的 footprint（旋转后的最终 AABB，按索引着色）+（可选）占格线 +（可选）建筑层粗格占用数；
 * - **运行时操作**（PIE 控制台命令，或 BP 调用）：
 *   `BuildingDebugPlaceCell X Y [Rotation]` / `BuildingDebugDemolishCell X Y` /
 *   `BuildingDebugDemolishAll` / `BuildingDebugDump`
 *   （走 `UAsceBuildingSubsystem` 的公开 API，和游戏内建造/拆除同一条链）
 */
UCLASS()
class AAsceBuildingDebugger : public AActor
{
	GENERATED_BODY()

public:
	AAsceBuildingDebugger();

	virtual void Tick(float DeltaTime) override;

	// ── 配置 ──

	/** 测试建造用的建筑类型（`AsceEntityDefRow` 行名；`BuildingDebugPlaceCell` 用它）。 */
	UPROPERTY(EditAnywhere, Category = "BuildingDebug",
		meta = (DataTableBinding = "AsceEntityDefRow:DisplayName,Description"))
	FAsceRowNameWrapper TestConfig;

	UPROPERTY(EditAnywhere, Category = "BuildingDebug")
	int32 TestTeamKey = 0;

	// ── 绘制开关 ──

	UPROPERTY(EditAnywhere, Category = "BuildingDebug")
	bool bDrawFootprints = true;

	/** 每个占格画一个小线框（大建筑会比较密）。 */
	UPROPERTY(EditAnywhere, Category = "BuildingDebug")
	bool bDrawCellLines = false;

	/** 建筑层：画每个粗格里的建筑数（0 = 不画该格）。 */
	UPROPERTY(EditAnywhere, Category = "BuildingDebug")
	bool bDrawCoarseCellCount = false;

	UPROPERTY(EditAnywhere, Category = "BuildingDebug")
	float DrawHeight = 60.f;

	/** 只画玩家（本地视角）周围 `DrawRange` 内的内容；关掉就画全图。 */
	UPROPERTY(EditAnywhere, Category = "BuildingDebug")
	bool bDrawOnlyAroundView = true;

	UPROPERTY(EditAnywhere, Category = "BuildingDebug")
	float DrawRange = 6000.f;

	// ── 控制台命令（PIE 里直接敲）──

	/** BuildingDebugPlaceCell X Y [Rotation=0]：在逻辑格 (X,Y) 为中心建造 `TestConfig`。 */
	UFUNCTION(Exec)
	void BuildingDebugPlaceCell(int32 CellX, int32 CellY, int32 Rotation = 0);

	/** BuildingDebugDemolishCell X Y：拆掉占用该格的建筑。 */
	UFUNCTION(Exec)
	void BuildingDebugDemolishCell(int32 CellX, int32 CellY);

	/** BuildingDebugDemolishAll：拆掉本系统登记的所有建筑。 */
	UFUNCTION(Exec)
	void BuildingDebugDemolishAll();

	/** BuildingDebugDump：打印当前建筑清单。 */
	UFUNCTION(Exec)
	void BuildingDebugDump() const;

private:
	/** 取绘制中心（世界坐标；只画附近时用本地视角位置，否则返回 false 表示"画全图"）。 */
	bool GetDrawCenter(FVector& OutCenterWorld, float& OutRange) const;

	void DrawCoarseCellCounts(const FVector& CenterWorld, float Range) const;
};
