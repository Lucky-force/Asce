class URTSGameHUD : UAsceActivatableWidget
{
    // ── BindWidget ──────────────────────────────
    UPROPERTY(BindWidget)
    UBorder SelectionBox;

    // ── 缓存 ────────────────────────────────────
    UAsceObserverComponent CachedObserver;

    APlayerController CachedController;

    // ── 初始化 ──────────────────────────────────
    UFUNCTION(BlueprintOverride)
    void Construct()
    {
        SelectionBox.SetVisibility(ESlateVisibility::Collapsed);
        TryCacheObserver();
    }

    // ── 每帧画选择框 ────────────────────────────
    UFUNCTION(BlueprintOverride)
    void Tick(FGeometry MyGeometry, float InDeltaTime)
    {
        if (!IsValid(CachedObserver))
        {
            TryCacheObserver();
        }

        if(!IsValid(CachedController))
        {
            TryCachePlayerController();
        }
        

        if (!IsValid(CachedObserver) || !CachedObserver.IsSelecting() || !IsValid(CachedController))
        {
            SelectionBox.SetVisibility(ESlateVisibility::Collapsed);
            return;
        }

        FVector2D StartPos = CachedObserver.GetSelectionStartPosition();

        float32 PosX = 0, PosY = 0;
        CachedController.GetMousePosition(PosX, PosY);

        float DPIScale = WidgetLayout::GetViewportScale();

        FVector2D StartLocal = StartPos / DPIScale;
        FVector2D CurrentLocal = FVector2D(PosX, PosY) / DPIScale;

        float X = Math::Min(StartLocal.X, CurrentLocal.X);
        float Y = Math::Min(StartLocal.Y, CurrentLocal.Y);
        float W = Math::Abs(CurrentLocal.X - StartLocal.X);
        float H = Math::Abs(CurrentLocal.Y - StartLocal.Y);

        if (W < 2.f && H < 2.f)
        {
            SelectionBox.SetVisibility(ESlateVisibility::Collapsed);
            return;
        }

        SelectionBox.SetVisibility(ESlateVisibility::Visible);
        UCanvasPanelSlot CanvasSlot = Cast<UCanvasPanelSlot>(SelectionBox.Slot);
        if (IsValid(CanvasSlot))
        {
            CanvasSlot.SetPosition(FVector2D(X, Y));
            CanvasSlot.SetSize(FVector2D(W, H));
        }
    }

    void TryCacheObserver()
    {
        APawn Pawn = GetOwningPlayerPawn();
        if (!IsValid(Pawn)) 
        {
            return;
        }

        CachedObserver = Cast<UAsceObserverComponent>(Pawn.GetComponentByClass(UAsceObserverComponent));
    }

    void TryCachePlayerController()
    {
        APawn Pawn = GetOwningPlayerPawn();
        if (!IsValid(Pawn))
        {
            return;
        }

        CachedController = Cast<APlayerController>(Pawn.GetController());
    }
};
