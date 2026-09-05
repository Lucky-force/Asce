// Fill out your copyright notice in the Description page of Project Settings.


#include "AngelScriptFunctionLibrary.h"

#include "Kismet/KismetMathLibrary.h"


void UAngelScriptFunctionLibrary::DelayCall(float DelayTime, UObject* ContextObject, FCommonCallbackDelegate Callback, FTimerHandle& TimerHandle)
{
	if (UWorld* World = ContextObject->GetWorld())
	{
		World->GetTimerManager().SetTimer(TimerHandle, FTimerDelegate::CreateWeakLambda(ContextObject, [Callback, ContextObject, &TimerHandle]()
		{
			Callback.ExecuteIfBound();
			if (UWorld* World = ContextObject->GetWorld())
			{
				World->GetTimerManager().ClearTimer(TimerHandle);
			}
		}), DelayTime, false);
	}
}

FRotator UAngelScriptFunctionLibrary::MakeRotationFromAxes(FVector Forward, FVector Right, FVector Up)
{
	return UKismetMathLibrary::MakeRotationFromAxes(Forward, Right, Up);
}

FVector UAngelScriptFunctionLibrary::TransformDirection(const FTransform& T, FVector Direction)
{
	return UKismetMathLibrary::TransformDirection(T, Direction);
}
