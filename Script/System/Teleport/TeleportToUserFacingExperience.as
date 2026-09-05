class ATeleportToUserFacingExperience : AActor
{
    
    UPROPERTY(DefaultComponent, RootComponent)
    USceneComponent RootComponent;

    UPROPERTY(DefaultComponent, Attach=RootComponent)
    UCapsuleComponent CollisionCapsule;

    UPROPERTY(DefaultComponent, Attach=RootComponent)
    UStaticMeshComponent TeleportBaseMesh;

    UPROPERTY(DefaultComponent, Attach=TeleportBaseMesh)
    UNiagaraComponent TeleportParticle;

    UPROPERTY(DefaultComponent, Attach=RootComponent)
    UWidgetComponent TeleportWidget;

    UPROPERTY()
    UAsceUserFacingExperienceDefinition UserFacingExperienceToLoad;

    //编辑器模式下只会有一个玩家，直接缓存触发者的controller
    APlayerController CachedTriggerPlayerController;

    UPROPERTY()
    TSoftClassPtr<UCommonActivatableWidget> WidgetToPush;

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        //这一块还是在蓝图的BeginPlay里面写吧，要cast成WidgetComponent的UserWidget到指定蓝图类

    }

    UFUNCTION()
    void LoadIntoExperience(APlayerController PlayerController, TSoftClassPtr<UCommonActivatableWidget> WidgetClass)
    {
        if(IsValid(UserFacingExperienceToLoad))
        {
            CachedTriggerPlayerController = PlayerController;
            FStateChangeDelegate BeforePushCallBack, AfterPushCallBack;
            BeforePushCallBack.BindUFunction(this, n"BeforePushWidgetCallBack");
            AfterPushCallBack.BindUFunction(this, n"AfterPushWidgetCallBack");
            UPrimaryGameLayout::PushWidgetToPlayerLayerStackAsync(GameplayTags::UI_Layer_Menu, true, WidgetClass, BeforePushCallBack, AfterPushCallBack, PlayerController, this);
        }
    }

    UFUNCTION()
    void BeforePushWidgetCallBack(UCommonActivatableWidget WidgetClass)
    {

    }

    UFUNCTION()
    void AfterPushWidgetCallBack(UCommonActivatableWidget WidgetClass)
    {
        if(IsValid(UserFacingExperienceToLoad) && IsValid(CachedTriggerPlayerController))
        {
            UCommonSession_HostSessionRequest HostRequest = UserFacingExperienceToLoad.CreateHostingRequest();
            //通过编辑器默认关卡载入，直接设置为单机模式
            HostRequest.OnlineMode = ECommonSessionOnlineMode::Offline;
            UCommonSessionSubsystem CommonSessionSubsystem = UCommonSessionSubsystem::Get();
            CommonSessionSubsystem.HostSession(CachedTriggerPlayerController, HostRequest);
        }
    }

    UFUNCTION(BlueprintOverride)
    void ActorBeginOverlap(AActor OtherActor)
    {
        APawn OtherPawn = Cast<APawn>(OtherActor);
        if(OtherPawn != nullptr)
        {
            APlayerController OtherController = Cast<APlayerController>(OtherPawn.GetController());
            if(OtherController != nullptr && OtherController.IsLocalController())
            {
                if(!WidgetToPush.IsNull()){
                    LoadIntoExperience(OtherController, WidgetToPush);
                }
            }
        }
    }
}