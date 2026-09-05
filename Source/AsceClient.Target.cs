// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class AsceClientTarget : TargetRules
{
	public AsceClientTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Client;

		ExtraModuleNames.AddRange(new string[] { "AsceGame" });

		AsceGameTarget.ApplySharedAsceTargetSettings(this);
	}
}
