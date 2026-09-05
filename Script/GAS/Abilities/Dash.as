class UDash : UAbilityWithWidget
{

    UPROPERTY()
    float Strength = 1850;

    UPROPERTY()
    float RootMotionDuration = 0.25;

    UPROPERTY()
    float AbilityDuration = 0.55;

    UPROPERTY()
    FVector Direction = FVector::ZeroVector;

    UPROPERTY()
    UAnimMontage Dass_Fwd;

    UPROPERTY()
    UAnimMontage Dash_Bwd;

    UPROPERTY()
    UAnimMontage Dash_Left;

    UPROPERTY()
    UAnimMontage Dash_Right;

    UPROPERTY()
    float Cooldown;

    //本次dash选中的Montage
    UPROPERTY()
    UAnimMontage CurrentDashMontage;


    //传出值都是方向，当前面向的方向，上一帧输入的移动方向，当前的移动方向
    UFUNCTION()
    void GetDashDirection(FVector&out Facing, FVector&out LastMovementInput, FVector&out Movement, bool&out Positive)
    {
        //根据激活能力的Actor是AI还是玩家来获取面朝方向和移动方向
        AAsceCharacter Caster = GetAsceCharacterFromActorInfo();

        FVector ProjectedLocation = FVector::ZeroVector;

        UNavigationSystemV1::ProjectPointToNavigation(Caster.GetActorLocation(), ProjectedLocation, nullptr, nullptr, FVector(500, 500, 500));

        //投影偏移方向（在正下方时为0，否则具有水平偏移）
        FVector NavDirection = ((ProjectedLocation - Caster.GetActorLocation()) * FVector(1, 1, 0)).GetSafeNormal(0.00001);

        Facing = Caster.GetActorForwardVector();

        LastMovementInput = Caster.GetLastMovementInputVector();

        bool bIsAIController = IsValid(AIHelper::GetAIController(Caster));

        Positive = bIsAIController || (LastMovementInput.Size() > 0);

        if(bIsAIController)
        {
            //如果是AI，那么上一次的移动输入和本次的移动方向都使用NavDirection
            LastMovementInput = NavDirection;

            Movement = NavDirection;
        }
        else
        {
            //如果是玩家，本次移动输入的方向使用摄像机的方向向量
            Movement = Caster.CameraComponent.GetForwardVector() * FVector(1, 1, 1.2);
        }
    }


    //获取要播放的蒙太奇，返回true代表向前冲刺
    UFUNCTION()
    bool SelectDirectionMontage(FVector FacingDirection, FVector MovementDirection, UAnimMontage&out DirectionMontage)
    {
        FRotator FacingRotator = FacingDirection.ToOrientationRotator();
        FRotator MovementRotator = MovementDirection.ToOrientationRotator();

        FRotator Delta = (FacingRotator - MovementRotator).GetNormalized();

        //移动方向和面朝方向的角度差值
        float Yaw = Delta.Yaw;

        if(Math::Abs(Yaw) < 45.f)
        {
            //差值在-45到45，看作向前方
            DirectionMontage = Dass_Fwd;
            return true;
        }
        else if(Math::Abs(Yaw) > 135.f)
        {
            //差值在135到-135度，看作向后方
            DirectionMontage = Dash_Bwd;
        }
        else if(Yaw < 0.f)
        {
            //差值在-45到-135度，看作向左方
            DirectionMontage = Dash_Left;
        }
        else
        {
            //差值在45度到135度，看作向右方
            DirectionMontage = Dash_Right;
        }

        return false;

    }

    UFUNCTION(BlueprintOverride)
    void ActivateAbility()
    {
        if(!IsLocallyControlled())
        {
            //只有本地controller能尝试激活此能力（即客户端额主控代理，单机模式，服务器上的远端非主控代理）
            //因此即使是本地预测，服务器在执行此代码的时候会直接退出
            //随后客户端发送rpc让服务器真正执行，这是为了让客户端来计算要播放的montage和冲刺方向并通过rpc发给服务器
            //否则让服务器重新计算一遍会因为网络延迟导致和客户端数据不一致
            return;
        }

        FVector FacingDirection;//当前面朝方向
        FVector LastMovementInputDirection;//上一帧移动输入的方向
        FVector CurrentMovementInputDirection;//当前移动输入的方向
        bool Positive;

        GetDashDirection(FacingDirection, LastMovementInputDirection, CurrentMovementInputDirection, Positive);

        bool bDashForward = SelectDirectionMontage(FacingDirection, LastMovementInputDirection, CurrentDashMontage);

        if(bDashForward)
        {
            //如果是向前冲刺，使用当前的移动输入
            Direction = CurrentMovementInputDirection;
        }
        else
        {
            //如果不是向前冲刺，使用上一帧的移动输入
            Direction = LastMovementInputDirection;
        }

        if(!Direction.IsNearlyZero(0.0001))
        {
            if(CommitAbility())
            {   
                ACharacter Caster = GetAsceCharacterFromActorInfo();
                if(Caster.bIsCrouched)
                {
                    Caster.UnCrouch();
                }

                if(!HasAuthority())
                {
                    //发送rpc让服务器也执行
                    SendInfo(Direction, CurrentDashMontage);
                }
                ActivateAbility_Internal();
            }
        }

        //如果没有移动输入，或者能力代价支付失败，就直接取消
        CancelAbility();

    }

    UFUNCTION(Server)
    void SendInfo(FVector DashDirection, UAnimMontage MontageToPlay)
    {
        //服务器执行rpc
        CurrentDashMontage = MontageToPlay;
        Direction = DashDirection;
        CommitAbility();
        ActivateAbility_Internal();
    }

    //大部分有异步事件节点的逻辑还是只有在蓝图里面写
    UFUNCTION(BlueprintEvent)
    void ActivateAbility_Internal()
    {
        
    }

}