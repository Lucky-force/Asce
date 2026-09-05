mixin void AnimMotionEffect(AFootStep Self
        , const FName Bone
		, const FGameplayTag MotionEffect
		, USceneComponent StaticMeshComponent
		, const FVector LocationOffset
		, const FRotator RotationOffset
		, const UAnimSequenceBase AnimationSequence
		, const bool bHitSuccess
		, const FHitResult HitResult
		, FGameplayTagContainer Contexts
		, FVector VFXScale = FVector(1)
		, float AudioVolume = 1
		, float AudioPitch = 1)
        {
            if(!Self.bEnableFootPlantEffect || !bHitSuccess)
            {
                return;
            }

            EPhysicalSurface SurfaceType = Gameplay::GetSurfaceType(HitResult);
            
            FName CurrentSocket = FName(Bone.ToString().Append("_Socket"));

            FTransform FootStepTransform = Self.OwnerSkeletalMesh.GetSocketTransform(CurrentSocket);

            FVector Right = HitResult.Normal.CrossProduct(AngelScript::TransformDirection(FootStepTransform, FVector(1, 0, 0)));
            Right.Normalize(0.0001);

            FVector Forward = Right.CrossProduct(HitResult.Normal);

            FVector Up = HitResult.Normal;

            FQuat Orientation = FQuat(AngelScript::MakeRotationFromAxes(Forward, Right, Up));
            
            //首次生成这个Actor并且生成NiagaraComponent的时候，设置Trigger为false，随后里面被设置为true
            //之后再次调用时，Trigger切换状态变为false，随后再次调用，变为true，一直来回震荡
            //使用的这个niagara是特制的，会在触发的时候在指定位置生成一个材质，这个niagara的trigger参数切换的时候，会从头播放niagara
            //此时根据当前的steplocation参数决定脚印生成地点，Orientation决定脚印的旋转，surfacetype来决定脚印特效，stepside决定本次生成在左边还是右边
            //这个niagara的播放时间是10秒钟，如果十秒内再次触发，就会从头播放，直到十秒钟没有播放，此时会销毁niagara组件，此时进入每5秒的检测回调就会对这个actor进行销毁
            if(!IsValid(Self.NS_FootPrint) || !Self.NS_FootPrint.IsActive())
            {
                Self.NS_FootPrint = Niagara::SpawnSystemAttached(Self.FootPrintEffect, Self.Root, n"None", FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, true, true, ENCPoolMethod::None);
                Self.bFootPrintTrigger = false;
            }
            
            Self.bFootPrintTrigger = !Self.bFootPrintTrigger;

            Self.NS_FootPrint.SetNiagaraVariableBool("User.Trigger", Self.bFootPrintTrigger);
            Self.NS_FootPrint.SetNiagaraVariableVec3("User.StepLocation", HitResult.Location);
            Self.NS_FootPrint.SetNiagaraVariableQuat("User.Orientation", Orientation);
            Self.NS_FootPrint.SetNiagaraVariableInt("User.SurfaceType", int32(SurfaceType));
            Self.NS_FootPrint.SetNiagaraVariableBool("User.StepSide", Bone == Self.RightFootBoneName);

            //每隔三秒检测一下自己创建的Niagara是不是被销毁了，被销毁了就销毁自己
            Self.Timer = System::SetTimer(Self, n"DestroySelf", 3, true);
        }

//用来实际生成脚印特效，如果直接在角色身上挂Niagara组件，角色死亡时Niagara组件也需要跟着销毁，否则生命周期不好管理
//因此生成一个脚印Actor用来维护Niagara组件，当Niagara特效播放完成后自动销毁自己
class AFootStep : AActor
{
    UPROPERTY(DefaultComponent, RootComponent)
    USceneComponent Root;

    //是否启用脚印effect生成
    UPROPERTY()
    bool bEnableFootPlantEffect = true;

    UPROPERTY()
    bool bFootPrintTrigger = false;

    //要创建的niagara
    UPROPERTY()
    UNiagaraSystem FootPrintEffect;

    //创建出来之后的实例组件
    UPROPERTY()
    UNiagaraComponent NS_FootPrint;

    //拥有者的骨骼
    UPROPERTY()
    USkeletalMeshComponent OwnerSkeletalMesh;

    //右脚的骨骼名字，根据检测当前触发的骨骼是不是右脚来在粒子内部决定是显示左脚的脚印效果还是右脚的
    UPROPERTY()
    FName RightFootBoneName;

    UPROPERTY()
    FTimerHandle Timer;

    UFUNCTION()
    void DestroySelf()
    {
        if(!IsValid(NS_FootPrint))
        {
            Print("NigaraComponent is destroyed, this footstep actor will desctroy self now!");
            System::ClearTimer(this, "DestroySelf");
            DestroyActor();
        }
    };
}