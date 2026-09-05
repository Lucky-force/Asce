// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Engine/StreamableManager.h"

//这个用于同步加载时返回进度数据


//当进度发生变化的时候回调此委托
DECLARE_DELEGATE_OneParam(FAsceAssetManagerStartupJobSubstepProgress, float /*NewProgress*/);

/** Handles reporting progress from streamable handles */
/** 通过流式句柄来报告加载进度 */
struct FAsceAssetManagerStartupJob
{
	FAsceAssetManagerStartupJobSubstepProgress SubstepProgressDelegate;
	TFunction<void(const FAsceAssetManagerStartupJob&, TSharedPtr<FStreamableHandle>&)> JobFunc;
	FString JobName;
	float JobWeight;
	mutable double LastUpdate = 0;

	/** Simple job that is all synchronous */
	// 简单的同步加载任务
	FAsceAssetManagerStartupJob(const FString& InJobName, const TFunction<void(const FAsceAssetManagerStartupJob&, TSharedPtr<FStreamableHandle>&)>& InJobFunc, float InJobWeight)
		: JobFunc(InJobFunc)
		, JobName(InJobName)
		, JobWeight(InJobWeight)
	{}

	/** Perform actual loading, will return a handle if it created one */
	// 进行真正的加载，如果创建了句柄将返回此句柄
	TSharedPtr<FStreamableHandle> DoJob() const;

	//子任务进度改变时回调委托
	void UpdateSubstepProgress(float NewProgress) const
	{
		SubstepProgressDelegate.ExecuteIfBound(NewProgress);
	}

	
	//Streamable进度改变时回调, 用来更新进度显示
	void UpdateSubstepProgressFromStreamable(TSharedRef<FStreamableHandle> StreamableHandle) const
	{
		if (SubstepProgressDelegate.IsBound())
		{
			// StreamableHandle::GetProgress traverses() a large graph and is quite expensive
			double Now = FPlatformTime::Seconds();
			if (LastUpdate - Now > 1.0 / 60)
			{
				SubstepProgressDelegate.Execute(StreamableHandle->GetProgress());
				LastUpdate = Now;
			}
		}
	}
};
