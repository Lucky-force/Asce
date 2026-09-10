// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

/**
 * 项目全局 Shader 模块（所有自定义 Compute/Fullscreen/Global shader 都放这里，不再每个 shader 新建模块）。
 *
 * 必须独立成模块并在 .uproject 里声明 LoadingPhase = PostConfigInit：
 * FShaderTypeRegistration 要求 shader 类型在 LaunchEngineLoop 的 CommitAll 之前完成静态注册，
 * 游戏模块（AsceGame，Default 阶段）加载太晚会触发 "Shader type was loaded too late" 断言。
 * 参考引擎的 NiagaraShader 模块（同样 PostConfigInit + 依赖 Engine）。
 */
public class AsceShaders : ModuleRules
{
	public AsceShaders(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"RenderCore",
				"RHI",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Engine", // SceneView.h（FViewUniformShaderParameters，FAsceFogPS 的 View uniform 参数）
			}
		);
	}
}
