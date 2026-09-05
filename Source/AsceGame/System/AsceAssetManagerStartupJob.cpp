// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceAssetManagerStartupJob.h"

#include "AsceLogChannels.h"

TSharedPtr<FStreamableHandle> FAsceAssetManagerStartupJob::DoJob() const
{
	const double JobStartTime = FPlatformTime::Seconds();

	TSharedPtr<FStreamableHandle> Handle;
	UE_LOG(LogAsce, Display, TEXT("Startup job \"%s\" starting"), *JobName);
	
	//JobFunc是自定义的，即使是调用的异步加载，也会等待加载完成
	JobFunc(*this, Handle);

	if (Handle.IsValid())
	{
		Handle->BindUpdateDelegate(FStreamableUpdateDelegate::CreateRaw(this, &FAsceAssetManagerStartupJob::UpdateSubstepProgressFromStreamable));
		Handle->WaitUntilComplete(0.0f, false);
		Handle->BindUpdateDelegate(FStreamableUpdateDelegate());
	}

	UE_LOG(LogAsce, Display, TEXT("Startup job \"%s\" took %.2f seconds to complete"), *JobName, FPlatformTime::Seconds() - JobStartTime);

	return Handle;
}