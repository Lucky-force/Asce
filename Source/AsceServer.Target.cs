// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

[SupportedPlatforms(UnrealPlatformClass.Server)]
public class AsceServerTarget : TargetRules
{
	public AsceServerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Server;

		ExtraModuleNames.AddRange(new string[] { "AsceGame" });

		AsceGameTarget.ApplySharedAsceTargetSettings(this);

		bUseChecksInShipping = true;
	}
}
