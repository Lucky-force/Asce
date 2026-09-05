class ATeleportListSpawner : AActor
{

    UPROPERTY(DefaultComponent, RootComponent)
    USceneComponent RootComponent;

    UPROPERTY(DefaultComponent, Attach=RootComponent)
    UBillboardComponent Billboard;

    //中间值，用来缓存本次要生成的UserfacingExperience
    UPROPERTY()
    TArray<UAsceUserFacingExperienceDefinition> UserfacingExperienceList;

    //额外要显示的UserfacingExperience
    UPROPERTY()
    TArray<UAsceUserFacingExperienceDefinition> AdditionalUserfacingExperienceToShow;

    UPROPERTY()
    TArray<ATeleportToUserFacingExperience> ExperiencePortals;

    //为false的时候只显示被标记为默认Experience的传送门，为true的时候显示所有，但是默认的总是在列表第一项
    UPROPERTY()
    bool bShowAllExperience = true;

    UPROPERTY()
    TSubclassOf<ATeleportToUserFacingExperience> TeleportSubClass = ATeleportToUserFacingExperience;

    TArray<FPrimaryAssetId> PrimaryAssetIdList;

    //传送门间距
    UPROPERTY()
    float PortalSpacing = 500.f;

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        UserfacingExperienceList.Empty();

        UserfacingExperienceList.Append(AdditionalUserfacingExperienceToShow);
        
        PrimaryAssetIdList.Empty();

        UAssetManager AssetManager = UAssetManager::Get();
        if(IsValid(AssetManager))
        {
            AssetManager.GetPrimaryAssetIdList(FPrimaryAssetType(n"AsceUserfacingExperienceDefinition"), PrimaryAssetIdList);
            AssetManager.LoadPrimaryAssets(PrimaryAssetIdList, TArray<FName>(), 0, this, n"OnLoadCompleted");
        }
    }

    UFUNCTION()
    void OnLoadCompleted()
    {
        UAssetManager AssetManager = UAssetManager::Get();
        if(IsValid(AssetManager))
        {
            for(FPrimaryAssetId PrimaryAssetId : PrimaryAssetIdList)
            {
                UAsceUserFacingExperienceDefinition UserfacingExperienceDefinition = Cast<UAsceUserFacingExperienceDefinition>(AssetManager.GetPrimaryAssetObject(PrimaryAssetId));

                if(IsValid(UserfacingExperienceDefinition))
                {
                    if(UserfacingExperienceDefinition.bShowInFrontEnd && UserfacingExperienceDefinition.bIsDefaultExperience)
                    {
                        UserfacingExperienceList.Insert(UserfacingExperienceDefinition, 0);
                    }
                    else if(bShowAllExperience)
                    {
                        UserfacingExperienceList.Add(UserfacingExperienceDefinition);
                    }
                }
            
            }

            FVector RootLocation = GetActorLocation();
            FRotator Rotation = GetActorRotation();

            for(int i = 0; i < UserfacingExperienceList.Num(); i++)
            {
                FVector Location = RootLocation + GetActorRightVector() * PortalSpacing * i;
                ATeleportToUserFacingExperience Portal = SpawnActor(TeleportSubClass, Location, Rotation, bDeferredSpawn = true, Level = GetLevel());
                Portal.UserFacingExperienceToLoad = UserfacingExperienceList[i];
                FinishSpawningActor(Portal);
                ExperiencePortals.Add(Portal);
            }
        }

    }

}