
delegate void FMontagePlayStateDelegate(UAbilityTask Task);

class UAbilityWithWidget : UAsceGameplayAbility
{
    //在能力被授予角色的时候，在各个UI扩展点添加对应的UI效果

    //WidgetClass和WidgetExtensionPointTag一一对应
    //要添加到扩展点的UI控件
    UPROPERTY()
    TArray<TSubclassOf<UUserWidget>> WidgetClassess;

    //要添加到的扩展点
    UPROPERTY()
    TArray<FGameplayTag> WidgetExtensionPointTags;

    //返回的Handles
    UPROPERTY()
    TArray<FUIExtensionHandle> WidgetExtensionHandles;


    UFUNCTION(BlueprintOverride)
    void OnAbilityAdded()
    {
        if(WidgetClassess.Num() != WidgetExtensionPointTags.Num())
        {
            Error(n"Ability", "Widget Classess dosen't match Tags!");
            return;
        }

        UUIExtensionSubsystem UIExtensionSubsystem = UUIExtensionSubsystem::Get();

        if(!IsValid(UIExtensionSubsystem))
        {
            return;
        }

        for(int i = 0; i < WidgetClassess.Num(); i++)
        {
            FUIExtensionHandle Handle = UIExtensionSubsystem.RegisterExtensionAsWidgetForContext(WidgetExtensionPointTags[i], WidgetClassess[i], GetOwningActorFromActorInfo());
            WidgetExtensionHandles.Add(Handle);
        }
    }

    UFUNCTION(BlueprintOverride)
    void OnAbilityRemoved()
    {
        if(WidgetClassess.Num() != WidgetExtensionPointTags.Num())
        {
            Error(n"Ability", "Widget Classess dosen't match Tags!");
            return;
        }

        UUIExtensionSubsystem UIExtensionSubsystem = UUIExtensionSubsystem::Get();

        if(!IsValid(UIExtensionSubsystem))
        {
            return;
        }

        for(int i = 0; i < WidgetExtensionHandles.Num(); i++)
        {
            UIExtensionSubsystem.UnregisterExtension(WidgetExtensionHandles[i]);
        }

        WidgetExtensionHandles.Empty();
    }

    //播放montage，在打断或者取消的时候自动结束能力
    //仅仅是一个基础操作，由于AS有限制，因此无法传入委托来根据情况具体调用
    //复杂的实现请直接重载此函数
    UFUNCTION(BlueprintEvent)
    void AS_PlayMontageAndWait(UAnimMontage MontageToPlay, float Rate, FName StartSection, bool StopWhenAbilityEnds, float AnimRootMootionTranslationScale, float StartTimeSeconds, bool AllowInterruptAfterBlendOut)
    {
    }
}