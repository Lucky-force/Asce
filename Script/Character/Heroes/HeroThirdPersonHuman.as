class AHeroThirdPersonHuman : AHeroDefault
{
    UPROPERTY(DefaultComponent)
    UAscePawnComponent_CharacterParts PawnCosmeticComponent;



    UFUNCTION(BlueprintOverride)
    void ConstructionScript()
    {
        Super::ConstructionScript();
        
    }

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        Super::BeginPlay();
        
        //绑定Cosmetic相关的委托
        PawnCosmeticComponent.OnCharacterPartsChanged.AddUFunction(this, n"OnCosmeticPartsChanged");

        //TODO 处理Team相关


        //隐藏这个Character，直到其组件完全初始化完成
        SetActorHiddenInGame(true);

        FActorInitStateChangedBPDelegate Delegate;
        Delegate.BindUFunction(this, n"ShowPawn");

        PawnExtComponent.RegisterAndCallForInitStateChange(GameplayTags::InitState_GameplayReady, Delegate, true);
    }


    //被Controller占用时
    UFUNCTION(BlueprintOverride)
    void Possessed(AController NewController)
    {
        //初始化背包相关数据

    }

    UFUNCTION()
    void ShowPawn(const FActorInitStateChangedParams&in Params)
    {
        SetActorHiddenInGame(false);
    }

    //死亡和重置Actor的时候调用背包清理
    UFUNCTION(BlueprintOverride)
    void OnDeathFinished()
    {
        ClearInventory();
    }

    UFUNCTION(BlueprintOverride)
    void OnReset()
    {
        ClearInventory();
    }

    UFUNCTION()
    void ClearInventory()
    {
        //清空装备到Pawn身上的物品
    }

    UFUNCTION()
    void OnCosmeticPartsChanged(UAscePawnComponent_CharacterParts CharacterParts)
    {
        Print("Parts Changed");
    }
}