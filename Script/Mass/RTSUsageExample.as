// ──────────────────────────────────────────
// 基本 RTS 用法示例
//
// 编辑器步骤：
//   1. 创建 DataAsset（UMassEntityConfigAsset），挂 3 个 Traits：
//      - UAsceRTSEntityTrait                （身份/阵营/血量/NetID）
//      - UAsceRTSUnitTrait                  （移动/视野/订单）
//      - UMassDistanceLODCollectorTrait + UMassMovableVisualizationTrait（LOD/ISM 渲染）
//   2. 实体定义表（AsceEntityDefRow）加一行：行名 = 数字 ID，
//      Def = FAsceRTSUnitDef（配半径/速度/血量…），EntityTemplate = 上面这个 DataAsset
//   3. 把行名填到场景 Manager Actor 的 SoldierDefRow
//   4. 运行，按下快捷键或右键调用 MoveSelectedTo
// ──────────────────────────────────────────

class ARTSManager : AActor
{
    UPROPERTY(DefaultComponent, RootComponent)
    USceneComponent RootComp;

    /** 要生成的单位类型（实体定义表行名；编辑器里下拉选行）。 */
    UPROPERTY(EditAnywhere, meta = (DataTableBinding = "AsceEntityDefRow:DisplayName,Description"))
    FAsceRowNameWrapper SoldierDefRow;

    /** 归属阵营（**跨端一致的 TeamId**，由生成方决定；1..N，0=中立。内部会打包成本端 TeamKey 写进 FAsceTeamFragment）。 */
    UPROPERTY(EditAnywhere)
    int32 SoldierTeamId = 0;

    UPROPERTY()
    TArray<int32> SelectedUnits;

    UPROPERTY()
    UAsceMassScriptBridge MassBridge;

    // ──────────────────────────────────────

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        MassBridge = UAsceMassScriptBridge::Get();

        // 生成必须服务器权威：客户端本地生成不走复制（幽灵实体）
        if (!HasAuthority())
        {
            return;
        }

        TArray<FTransform> SpawnTransforms;
        for (int i = 0; i < 1; i++)
        {
            for (int j = 0; j < 1; j++)
            {
                FTransform T;
                T.SetLocation(FVector(i * 50.f, j * 50.f, 100.f) + FVector(1000, 1000, 100));
                SpawnTransforms.Add(T);
            }
        }

        // 走实体定义表：模板 + 类型数值（半径/速度/血量/视野）都在这一条里
        TArray<int64> Handles = MassBridge.SpawnEntitiesByDef(SoldierDefRow.RowName, SpawnTransforms, SoldierTeamId);
        Log("Spawned " + Handles.Num() + " soldiers (def=" + SoldierDefRow.RowName + ")");
    }

    // ── 框选 ──────────────────────────────


    // ── 右键移动 ───────────────────────────


    // ── 攻击移动（带偏移防堆叠） ────────────


    // ── 停止命令 ───────────────────────────


    // ── 攻击：无手动攻击指令，攻击全自动（见 Combat/技能系统设计.md）────

    // ── 打印单位信息 ────────────────────────

}
