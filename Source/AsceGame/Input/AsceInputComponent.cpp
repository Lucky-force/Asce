
#include "AsceInputComponent.h"

#include "EnhancedInputSubsystems.h"
#include "Player/AsceLocalPlayer.h"
#include "Settings/AsceSettingsLocal.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceInputComponent)

class UAsceInputConfig;

UAsceInputComponent::UAsceInputComponent(const FObjectInitializer& ObjectInitializer)
{
}

void UAsceInputComponent::AddInputMappings(const UAsceInputConfig* InputConfig, UEnhancedInputLocalPlayerSubsystem* InputSubsystem) const
{
	check(InputConfig);
	check(InputSubsystem);

	// Here you can handle any custom logic to add something from your input config if required
}

void UAsceInputComponent::RemoveInputMappings(const UAsceInputConfig* InputConfig, UEnhancedInputLocalPlayerSubsystem* InputSubsystem) const
{
	check(InputConfig);
	check(InputSubsystem);

	// Here you can handle any custom logic to remove input mappings that you may have added above
}

void UAsceInputComponent::RemoveBinds(TArray<uint32>& BindHandles)
{
	for (uint32 Handle : BindHandles)
	{
		RemoveBindingByHandle(Handle);
	}
	BindHandles.Reset();
}
