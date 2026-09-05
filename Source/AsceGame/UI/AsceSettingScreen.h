// #pragma once
//
// #include "Engine/DataTable.h"
// #include "Widgets/GameSettingScreen.h"
//
// #include "AsceSettingScreen.generated.h"
//
// class UGameSettingRegistry;
// class UAsceTabListWidgetBase;
// class UObject;
//
// UCLASS(Abstract, meta = (Category = "Settings", DisableNativeTick))
// class UAsceSettingScreen : public UGameSettingScreen
// {
// 	GENERATED_BODY()
//
// public:
//
// protected:
// 	virtual void NativeOnInitialized() override;
// 	virtual UGameSettingRegistry* CreateRegistry() override;
//
// 	void HandleBackAction();
// 	void HandleApplyAction();
// 	void HandleCancelChangesAction();
//
// 	virtual void OnSettingsDirtyStateChanged_Implementation(bool bSettingsDirty) override;
// 	
// protected:
// 	//绑定顶层的tablistWidget
// 	UPROPERTY(BlueprintReadOnly, Category = Input, meta = (BindWidget, OptionalWidget = true, AllowPrivateAccess = true))
// 	TObjectPtr<UAsceTabListWidgetBase> TopSettingsTabs;
// 	
// 	UPROPERTY(EditDefaultsOnly)
// 	FDataTableRowHandle BackInputActionData;
//
// 	UPROPERTY(EditDefaultsOnly)
// 	FDataTableRowHandle ApplyInputActionData;
//
// 	UPROPERTY(EditDefaultsOnly)
// 	FDataTableRowHandle CancelChangesInputActionData;
//
// 	FUIActionBindingHandle BackHandle;
// 	FUIActionBindingHandle ApplyHandle;
// 	FUIActionBindingHandle CancelChangesHandle;
// };
