// #include "AsceSettingScreen.h"
//
// #include "Input/CommonUIInputTypes.h"
// #include "Player/AsceLocalPlayer.h"
// #include "Settings/AsceGameSettingRegistry.h"
//
// #include UE_INLINE_GENERATED_CPP_BY_NAME(AsceSettingScreen)
//
// class UGameSettingRegistry;
//
// void UAsceSettingScreen::NativeOnInitialized()
// {
// 	Super::NativeOnInitialized();
//
// 	BackHandle = RegisterUIActionBinding(FBindUIActionArgs(BackInputActionData, true, FSimpleDelegate::CreateUObject(this, &ThisClass::HandleBackAction)));
// 	ApplyHandle = RegisterUIActionBinding(FBindUIActionArgs(ApplyInputActionData, true, FSimpleDelegate::CreateUObject(this, &ThisClass::HandleApplyAction)));
// 	CancelChangesHandle = RegisterUIActionBinding(FBindUIActionArgs(CancelChangesInputActionData, true, FSimpleDelegate::CreateUObject(this, &ThisClass::HandleCancelChangesAction)));
// }
//
// UGameSettingRegistry* UAsceSettingScreen::CreateRegistry()
// {
// 	UAsceGameSettingRegistry* NewRegistry = NewObject<UAsceGameSettingRegistry>();
//
// 	if (UAsceLocalPlayer* LocalPlayer = CastChecked<UAsceLocalPlayer>(GetOwningLocalPlayer()))
// 	{
// 		NewRegistry->Initialize(LocalPlayer);
// 	}
//
// 	return NewRegistry;
// }
//
// void UAsceSettingScreen::HandleBackAction()
// {
// 	if (AttemptToPopNavigation())
// 	{
// 		return;
// 	}
//
// 	ApplyChanges();
//
// 	DeactivateWidget();
// }
//
// void UAsceSettingScreen::HandleApplyAction()
// {
// 	ApplyChanges();
// }
//
// void UAsceSettingScreen::HandleCancelChangesAction()
// {
// 	CancelChanges();
// }
//
// void UAsceSettingScreen::OnSettingsDirtyStateChanged_Implementation(bool bSettingsDirty)
// {
// 	if (bSettingsDirty)
// 	{
// 		if (!GetActionBindings().Contains(ApplyHandle))
// 		{
// 			AddActionBinding(ApplyHandle);
// 		}
// 		if (!GetActionBindings().Contains(CancelChangesHandle))
// 		{
// 			AddActionBinding(CancelChangesHandle);
// 		}
// 	}
// 	else
// 	{
// 		RemoveActionBinding(ApplyHandle);
// 		RemoveActionBinding(CancelChangesHandle);
// 	}
// }
