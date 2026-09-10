// ──────────────────────────────────────────
// 基本 RTS 用法示例
//
// 编辑器步骤：
//   1. 创建 DataAsset（UMassEntityConfigAsset）
//   2. 添加 3 个 Traits：
//      - UMassDistanceLODCollectorTrait  （距离采集）
//      - UAsceRTSUnitTrait               （逻辑数据）
//      - UMassMovableVisualizationTrait  （ISM 渲染，需指定 StaticMesh）
//   3. 把 DataAsset 赋给场景中的 Manager Actor 的 SoldierConfig
//   4. 运行，按下快捷键或右键调用 MoveSelectedTo
// ──────────────────────────────────────────

class ARTSManager : AActor
{
    UPROPERTY(DefaultComponent, RootComponent)
    USceneComponent RootComp;

    UPROPERTY()
    UMassEntityConfigAsset SoldierConfig;

    UPROPERTY()
    TArray<int32> SelectedUnits;

    UPROPERTY()
    UAsceMassScriptBridge MassBridge;

    // ──────────────────────────────────────

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        MassBridge = UAsceMassScriptBridge::Get();

        if (!IsValid(SoldierConfig))
        {
            Log("No SoldierConfig assigned!");
            return;
        }

        TArray<FTransform> SpawnTransforms;
        for (int i = 0; i < 30; i++)
        {
            for (int j = 0; j < 30; j++)
            {
                FTransform T;
                T.SetLocation(FVector(i * 50.f, j * 50.f, 100.f) + FVector(1000, 1000, 100));
                SpawnTransforms.Add(T);
            }
        }

        TArray<int64> Handles = MassBridge.SpawnEntities(SoldierConfig, SpawnTransforms);
        Log("Spawned " + Handles.Num() + " soldiers");
    }

    // ── 框选 ──────────────────────────────


    // ── 右键移动 ───────────────────────────


    // ── 攻击移动（带偏移防堆叠） ────────────


    // ── 停止命令 ───────────────────────────


    // ── 攻击命令 ───────────────────────────

    // UFUNCTION()
    // void AttackTarget(int64 TargetEntity)
    // {
    //     if (SelectedUnits.Num() == 0 || MassBridge.IsUnitDead(TargetEntity))
    //     {
    //         return;
    //     }

    //     MassBridge.IssueAttackOrder(SelectedUnits, TargetEntity);
    //     Log("Attacking entity " + TargetEntity);
    // }

    // ── 停止攻击 ───────────────────────────

    // UFUNCTION()
    // void StopAttack()
    // {
    //     MassBridge.ClearAttackOrder(SelectedUnits);
    // }

    // ── 打印单位信息 ────────────────────────

}
