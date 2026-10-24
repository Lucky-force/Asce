// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AsceGame : ModuleRules
{
	public AsceGame(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[] {
				"AsceGame"
			}
		);

		PrivateIncludePaths.AddRange(
			new string[] {
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreOnline",
				"CoreUObject",
				"ApplicationCore",
				"Engine",
				"PhysicsCore",
				"GameplayTags",
				"GameplayTasks",
				"GameplayAbilities",
				"AIModule",
				"ModularGameplay",
				"ModularGameplayActors",
				"DataRegistry",
				"ReplicationGraph",
				"GameFeatures",
				"SignificanceManager",
				"Hotfix",
				"CommonLoadingScreen",
				"Niagara",
				"AsyncMixin",
				"ControlFlows",
				"PropertyPath",
				"MassEntity",
				"MassCommon",
				"MassSimulation",
				"MassMovement",
				"MassSpawner",
				"MassRepresentation",
				"MassSignals",
				"MassLOD",
				"MassReplication",
				"MassActors",
				// 技能/行为运行时：Mass × StateTree（`Combat/技能编辑器设计.md`）
				// 公开头（Behavior/AsceStateTreeProcessors.h）直接 include MassStateTreeProcessors.h
				"MassAIBehavior",
				"StateTreeModule",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"InputCore",
				"Slate",
				"SlateCore",
				"RenderCore",
				"Renderer", // ScreenPass/PostProcessMaterialInputs 的符号定义在 Renderer（Engine 只给了它的include路径，不链接）
				"AsceShaders", // 项目全局shader模块（独立PostConfigInit，见其Build.cs）
				"DeveloperSettings",
				"EnhancedInput",
				"NetCore",
				"RHI",
				"Projects",
				"Gauntlet",
				"UMG",
				"CommonUI",
				"CommonInput",
				"GameSettings",
				"CommonGame",
				"CommonUser",
				"GameSubtitles",
				"GameplayMessageRuntime",
				"AudioMixer",
				"NetworkReplayStreaming",
				"UIExtension",
				"ClientPilot",
				"AudioModulation",
				"EngineSettings",
				"DTLSHandlerComponent",
				"Json",
			}
		);

		DynamicallyLoadedModuleNames.AddRange(
			new string[] {
			}
		);

		// Generate compile errors if using DrawDebug functions in test/shipping builds.
		PublicDefinitions.Add("SHIPPING_DRAW_DEBUG_ERROR=1");

		// Basic setup for External RPC Framework.
		// Functionality within framework will be stripped in shipping to remove vulnerabilities.
		PrivateDependencyModuleNames.Add("ExternalRpcRegistry");
		PrivateDependencyModuleNames.Add("HTTPServer"); // Dependency for ExternalRpcRegistry
		if (Target.Configuration == UnrealTargetConfiguration.Shipping)
		{
			PublicDefinitions.Add("WITH_RPC_REGISTRY=0");
			PublicDefinitions.Add("WITH_HTTPSERVER_LISTENERS=0");
		}
		else
		{
			PublicDefinitions.Add("WITH_RPC_REGISTRY=1");
			PublicDefinitions.Add("WITH_HTTPSERVER_LISTENERS=1");
		}

		// VAT 烘焙工具（`UAsceAnimSet::RebuildFromSources`，见 `Combat/技能系统设计.md` §7.1）：
		// 只在**编辑器构建**里链接插件（`AnimToTexture` = runtime 模块类型/BP 库；`AnimToTextureEditor` = 烘焙 API；`AssetRegistry` = 注册新建资产）
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "AnimToTexture", "AnimToTextureEditor", "AssetRegistry" });
		}

		SetupGameplayDebuggerSupport(Target);
		SetupIrisSupport(Target);
	}
}
