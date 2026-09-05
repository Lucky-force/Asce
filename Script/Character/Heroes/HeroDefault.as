mixin void AnimMotionEffect(AHeroDefault Self
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
            //服务器上不生成脚印actor
            if(System::IsServer())
            {
                return;
            }

            if(!IsValid(Self.CurrentFootprintSpawner))
            {
                
                Self.CurrentFootprintSpawner = SpawnActor(AFootStep, FVector::ZeroVector, FRotator::ZeroRotator, NAME_None, true, Self.GetLevel());
                Self.CurrentFootprintSpawner.OwnerSkeletalMesh = Self.Mesh;
                Self.CurrentFootprintSpawner.RightFootBoneName = n"foot_right";
                FinishSpawningActor(Self.CurrentFootprintSpawner);
                Self.CurrentFootprintSpawner.AttachToComponent(Self.RootComponent, NAME_None, EAttachmentRule::KeepRelative, EAttachmentRule::KeepRelative, EAttachmentRule::KeepRelative, true);
            }

            Self.CurrentFootprintSpawner.AnimMotionEffect(Bone, MotionEffect, StaticMeshComponent, LocationOffset, RotationOffset, AnimationSequence, bHitSuccess, HitResult, Contexts, VFXScale, AudioVolume, AudioPitch);
        }

class AHeroDefault : ACharacterDefault
{
    UPROPERTY(DefaultComponent)
    UAIPerceptionStimuliSourceComponent AIPerceptionStimuliSource;

    UPROPERTY(DefaultComponent)
    UAsceHeroComponent AsceHeroComponent;

    UPROPERTY(DefaultComponent)
    UAsceContextEffectComponent AsceContextEffectComponent;

    UPROPERTY()
    float RagdollImpulseStrength = 500.f;

    UPROPERTY()
    FName RagdollImpulseBone = n"Pelvis";

    UPROPERTY()
    TArray<UAnimMontage> DeathMontages;  

    UPROPERTY()
    AFootStep CurrentFootprintSpawner;  

    UFUNCTION(BlueprintOverride)
    void ConstructionScript()
    {
        AIPerceptionStimuliSource.RegisterAsSourceForSenses.Add(UAISense_Sight);
        AIPerceptionStimuliSource.RegisterAsSourceForSenses.Add(UAISense_Hearing);
    }

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        HealthComponent.OnDeathStarted.AddUFunction(this, n"OnDeathStarted_AS");
    }

    UFUNCTION()
    void OnDeathStarted_AS(AActor OwningActgor)
    {
        if(DeathMontages.Num() > 0)
        {
            int RandNumber = Math::RandRange(0, DeathMontages.Num() - 1);
            PlayAnimMontage(DeathMontages[RandNumber]);
        }

        AIPerceptionStimuliSource.UnregisterFromSense(UAISense_Sight);
        AIPerceptionStimuliSource.UnregisterFromSense(UAISense_Hearing);
        

        FTimerHandle TimerHandle;
        float RandFloat = Math::RandRange(0.1, 0.6);
        FCommonCallbackDelegate cb;
        cb.BindUFunction(this, n"DeathDelayToDo");
        //TimerHandle不需要维护（除非想在回调之前清理，否则如果回调时Actor已销毁，此时会直接不执行函数）
        AngelScript::DelayCall(RandFloat, this, cb, TimerHandle);
    }

    UFUNCTION()
    void DeathDelayToDo()
    {
        Ragdoll();
        HideEquipmentWeapons();
        Death();
    }

    //死亡后启用布娃娃模拟
    UFUNCTION()
    void Ragdoll()
    {
        Mesh.SetCollisionProfileName(n"Ragdoll");
        //让所有层级低于传入的骨骼开始模拟物理（pelvis通常是最高层级，意味着所有骨骼都模拟物理）
        Mesh.SetAllBodiesBelowSimulatePhysics(RagdollImpulseBone, true);

        FVector LastVelocity = CharacterMovement.GetLastUpdateVelocity();

        FVector DirectionVector = LastVelocity;

        DirectionVector.Normalize(0.0001);
        
        FVector Impulse = LastVelocity +  DirectionVector * RagdollImpulseStrength;

        Mesh.AddImpulse(Impulse, RagdollImpulseBone, true);
    }

    UFUNCTION()
    void HideEquipmentWeapons()
    {
        
    }

    UFUNCTION()
    void Death()
    {
                
    }

}