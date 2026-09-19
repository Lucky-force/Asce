// ─────────────────────────────────────────────────────────────
// RTS 单位/建筑检视器（Inspector）
//
// 用法（编辑器）：
//   1. 建 3 个行 Widget（父类如下，控件名必须一致）：
//      - WBP_InspectorStatRow      父类 URTSInspectorStatRow   （Text_Label / Text_Value）
//      - WBP_InspectorActionButton 父类 URTSInspectorActionButton（Button_Root / Text_Label）
//      - WBP_InspectorSelectionRow 父类 URTSInspectorSelectionRow（Text_Name / Image_Portrait）
//   2. 建 WBP_UnitInspector（父类 URTSUnitInspector）：
//      - Root            (Overlay，命名 root：整块面板；由脚本控制折叠/展开)
//      - Column1Switcher (UWidgetSwitcher：Slot0=单选头像+名字，Slot1=多选列表)
//      - Image_Portrait  (UImage) / Text_PrimaryName (UTextBlock)   ← 放 Slot0
//      - SelectionBox    (UPanelWidget：多选列表容器)               ← 放 Slot1
//      - StatsBox        (UPanelWidget：属性行容器，建议 ScrollBox)
//      - ActionBox       (UPanelWidget：功能键容器)
//      并把上面 3 个行 WBP 类填到 StatRowClass / ActionRowClass / SelectionRowClass
//   3. 把 WBP_UnitInspector 放进 HUD（WBP_RTS_HUD 子控件，或推 Game 层）
//
// 头像：全部走控制器异步加载（`RequestHeadIcon` → `OnEntityIconLoaded`），脚本里不做同步加载。
// ─────────────────────────────────────────────────────────────

// ── 属性行 ──
class URTSInspectorStatRow : UUserWidget
{
    UPROPERTY(BindWidget)
    UTextBlock Text_Label;

    UPROPERTY(BindWidget)
    UTextBlock Text_Value;

    void SetStat(FText Label, FText Value)
    {
        Text_Label.SetText(Label);
        Text_Value.SetText(Value);
    }
};

// ── 功能键（点击回传控制器执行）──
class URTSInspectorActionButton : UUserWidget
{
    UPROPERTY(BindWidget)
    UButton Button_Root;

    UPROPERTY(BindWidget)
    UTextBlock Text_Label;

    UPROPERTY()
    UAsceEntityInspectorController Controller;

    FName ActionId;

    UFUNCTION(BlueprintOverride)
    void Construct()
    {
        Button_Root.OnClicked.AddUFunction(this, n"HandleClicked");
    }

    void Setup(UAsceEntityInspectorController InController, const FAsceBuildingAction&in Action)
    {
        Controller = InController;
        ActionId = Action.ActionId;
        Text_Label.SetText(Action.Label);
        Button_Root.SetToolTipText(Action.Tooltip);
        Button_Root.SetIsEnabled(Action.bEnabled);
    }

    UFUNCTION()
    void HandleClicked()
    {
        if (IsValid(Controller))
        {
            Controller.ExecuteAction(ActionId);
        }
    }
};

// ── 多选列表项 ──
class URTSInspectorSelectionRow : UUserWidget
{
    UPROPERTY(BindWidget)
    UTextBlock Text_Name;

    UPROPERTY(BindWidget)
    UImage Image_Portrait;

    int64 EntityNumber = 0;

    void SetEntry(const FAsceInspectorListEntry&in Entry)
    {
        EntityNumber = Entry.EntityNumber;
        Text_Name.SetText(Entry.DisplayName);
        Image_Portrait.SetVisibility(ESlateVisibility::Hidden);
    }

    // 头像异步加载完成后由面板转发进来
    void SetIcon(UTexture2D Texture)
    {
        if (IsValid(Texture))
        {
            Image_Portrait.SetBrushFromTexture(Texture, false);
            Image_Portrait.SetVisibility(ESlateVisibility::Visible);
        }
        else
        {
            Image_Portrait.SetVisibility(ESlateVisibility::Hidden);
        }
    }
};

// ── 检视器面板 ──
class URTSUnitInspector : UAsceActivatableWidget
{
    // ── BindWidget ──
    // 整个面板的根（Overlay，WBP 里命名 root）：无选中时直接折叠
    UPROPERTY(BindWidget)
    UWidget Root;

    // 左栏切换器：Slot0 = 单选（头像+名字），Slot1 = 多选（SelectionBox 列表）
    UPROPERTY(BindWidget)
    UWidgetSwitcher Column1Switcher;

    UPROPERTY(BindWidget)
    UImage Image_Portrait;

    UPROPERTY(BindWidget)
    UTextBlock Text_PrimaryName;

    UPROPERTY(BindWidget)
    UPanelWidget SelectionBox;

    UPROPERTY(BindWidget)
    UPanelWidget StatsBox;

    UPROPERTY(BindWidget)
    UPanelWidget ActionBox;

    // ── 行 Widget 类（WBP 里指定）──
    UPROPERTY(EditAnywhere, Category = "Inspector")
    TSubclassOf<UUserWidget> StatRowClass;

    UPROPERTY(EditAnywhere, Category = "Inspector")
    TSubclassOf<UUserWidget> ActionRowClass;

    UPROPERTY(EditAnywhere, Category = "Inspector")
    TSubclassOf<UUserWidget> SelectionRowClass;

    // ── 控制器 / 当前列表项 ──
    UPROPERTY()
    UAsceEntityInspectorController Controller;

    UPROPERTY()
    TArray<URTSInspectorSelectionRow> SelectionRows;

    TArray<int64> SelectionRowEntities;

    UFUNCTION(BlueprintOverride)
    void Construct()
    {
        Controller = UAsceEntityInspectorController::Create(GetOwningPlayer());
        if (!IsValid(Controller))
        {
            Log("[Inspector] 控制器创建失败（没有 PlayerController？）");
            return;
        }

        Controller.OnSelectionChanged.AddUFunction(this, n"HandleInspectorChanged");
        Controller.OnPrimaryValuesChanged.AddUFunction(this, n"HandleInspectorChanged");
        Controller.OnEntityIconLoaded.AddUFunction(this, n"HandleEntityIconLoaded");
        OnVisibilityChanged.AddUFunction(this, n"HandleVisibilityChanged");

        // 用当前可见性初始化一次（而不是无条件打开）
        SyncViewActive(GetVisibility());
        Controller.RefreshNow();

        RefreshAll();
    }

    UFUNCTION(BlueprintOverride)
    void Destruct()
    {
        if (IsValid(Controller))
        {
            Controller.SetViewActive(false);
        }
        Controller = nullptr;
    }

    // 面板自身可见性变化（SetVisibility 到本 Widget 上时触发；Collapsed/Hidden ⇒ 控制器停 tick）
    UFUNCTION()
    void HandleVisibilityChanged(ESlateVisibility InVisibility)
    {
        SyncViewActive(InVisibility);
    }

    void SyncViewActive(ESlateVisibility InVisibility)
    {
        if (!IsValid(Controller))
        {
            return;
        }
        bool bVisible = InVisibility != ESlateVisibility::Collapsed && InVisibility != ESlateVisibility::Hidden;
        if (bVisible != Controller.IsViewActive())
        {
            Controller.SetViewActive(bVisible);
            if (bVisible)
            {
                Controller.RefreshNow();
                RefreshAll();
            }
        }
    }

    UFUNCTION()
    void HandleInspectorChanged()
    {
        RefreshAll();
    }

    // 头像异步加载完成：主单位头像 + 多选列表项路由
    UFUNCTION()
    void HandleEntityIconLoaded(int64 EntityNumber, UTexture2D Texture)
    {
        if (!IsValid(Controller))
        {
            return;
        }

        if (Controller.GetPrimaryEntityNumber() == EntityNumber)
        {
            if (IsValid(Texture))
            {
                Image_Portrait.SetBrushFromTexture(Texture, false);
                Image_Portrait.SetVisibility(ESlateVisibility::Visible);
            }
            else
            {
                Image_Portrait.SetVisibility(ESlateVisibility::Hidden);
            }
        }

        for (int i = 0; i < SelectionRowEntities.Num(); i++)
        {
            if (SelectionRowEntities[i] == EntityNumber && IsValid(SelectionRows[i]))
            {
                SelectionRows[i].SetIcon(Texture);
            }
        }
    }

    // ── 刷新 ──

    void RefreshAll()
    {
        if (!IsValid(Controller))
        {
            return;
        }

        int Count = Controller.GetSelectedCount();
        if (Count <= 0)
        {
            // 无选中：root 折叠，控制器停 tick（选择变化事件仍绑着，重新选上会自动展开）
            Root.SetVisibility(ESlateVisibility::Collapsed);
            Controller.SetViewActive(false);
            return;
        }

        Root.SetVisibility(ESlateVisibility::Visible);
        if (!Controller.IsViewActive())
        {
            Controller.SetViewActive(true);
        }

        bool bSingle = Count == 1;
        Column1Switcher.SetActiveWidgetIndex(bSingle ? 0 : 1);

        if (bSingle)
        {
            RefreshSingle();
        }
        else
        {
            RefreshSelectionList();
        }

        RefreshStats();
        RefreshActions();
    }

    void RefreshSingle()
    {
        FAsceEntityDefRow DefRow;
        int32 TypeId = 0;
        if (Controller.GetPrimaryDefRow(DefRow, TypeId))
        {
            Text_PrimaryName.SetText(DefRow.DisplayName);
        }
        else
        {
            Text_PrimaryName.SetText(FText::FromString("未知类型"));
        }

        // 头像走异步：先占位隐藏，加载完成回调里设置
        Image_Portrait.SetVisibility(ESlateVisibility::Hidden);
        Controller.RequestHeadIcon(Controller.GetPrimaryEntityNumber());
    }

    void RefreshSelectionList()
    {
        SelectionBox.ClearChildren();
        SelectionRows.Empty();
        SelectionRowEntities.Empty();

        if (!IsValid(SelectionRowClass))
        {
            return;
        }

        TArray<FAsceInspectorListEntry> Entries;
        Controller.GetSelectionList(Entries);

        for (FAsceInspectorListEntry Entry : Entries)
        {
            UUserWidget Item = WidgetBlueprint::CreateWidget(SelectionRowClass, GetOwningPlayer());
            if (!IsValid(Item))
            {
                continue;
            }
            URTSInspectorSelectionRow Row = Cast<URTSInspectorSelectionRow>(Item);
            if (IsValid(Row))
            {
                Row.SetEntry(Entry);
                SelectionRows.Add(Row);
                SelectionRowEntities.Add(Entry.EntityNumber);
            }
            SelectionBox.AddChild(Item);

            Controller.RequestHeadIcon(Entry.EntityNumber);
        }
    }

    void RefreshStats()
    {
        StatsBox.ClearChildren();
        if (!IsValid(StatRowClass))
        {
            return;
        }

        TArray<FAsceInspectorStatRow> Rows;
        Controller.GetStatRows(Rows);

        for (FAsceInspectorStatRow Stat : Rows)
        {
            UUserWidget Item = WidgetBlueprint::CreateWidget(StatRowClass, GetOwningPlayer());
            if (!IsValid(Item))
            {
                continue;
            }
            URTSInspectorStatRow Row = Cast<URTSInspectorStatRow>(Item);
            if (IsValid(Row))
            {
                Row.SetStat(Stat.Label, Stat.Value);
            }
            StatsBox.AddChild(Item);
        }
    }

    void RefreshActions()
    {
        ActionBox.ClearChildren();
        if (!IsValid(ActionRowClass))
        {
            return;
        }

        TArray<FAsceBuildingAction> Actions;
        Controller.GetActions(Actions);

        for (FAsceBuildingAction Action : Actions)
        {
            UUserWidget Item = WidgetBlueprint::CreateWidget(ActionRowClass, GetOwningPlayer());
            if (!IsValid(Item))
            {
                continue;
            }
            URTSInspectorActionButton Row = Cast<URTSInspectorActionButton>(Item);
            if (IsValid(Row))
            {
                Row.Setup(Controller, Action);
            }
            ActionBox.AddChild(Item);
        }
    }
};
