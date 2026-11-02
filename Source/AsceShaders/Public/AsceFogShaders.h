// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphResources.h"
#include "SceneView.h"

// MOD-POINT 2026-09-10 14:49:13
// 黑雾 Compute Shader：按脏 tile 把投影 bits 展开写入持久 Fog(R8_UINT)。
// Payload 布局：[ TilesCount 个 tile 索引 ][ TilesCount*TileWords 个 word ]。
// 每 group = 1 个 tile，128 线程各处理 1 个 word；tile 内 64×64 格、每行 2 word。

class ASCESHADERS_API FAsceFogCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FAsceFogCS);
	SHADER_USE_PARAMETER_STRUCT(FAsceFogCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, Payload)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutFog)
		SHADER_PARAMETER(int32, GridW)
		SHADER_PARAMETER(int32, GridH)
		SHADER_PARAMETER(int32, TilesCount)
		SHADER_PARAMETER(int32, TilesX)
		SHADER_PARAMETER(int32, TileCells)
		SHADER_PARAMETER(int32, TileWords)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		// 只在支持 SM5 的平台上编译（Compute）
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

// MOD-POINT 2026-09-10 16:20:00
// 黑雾 Fullscreen Pixel Shader：按深度重建世界 XY → 查 Fog(R8) → 压黑 SceneColor。
class ASCESHADERS_API FAsceFogPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FAsceFogPS);
	SHADER_USE_PARAMETER_STRUCT(FAsceFogPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SceneColorSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, FogTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, FogSampler)
		SHADER_PARAMETER(FIntPoint, GridSize)
		SHADER_PARAMETER(FVector2f, WorldOrigin)
		SHADER_PARAMETER(FVector4f, SceneColorViewRect)
		SHADER_PARAMETER(FIntPoint, SceneColorExtent)
		SHADER_PARAMETER(FIntPoint, SceneDepthExtent)
		SHADER_PARAMETER(float, CellSize)
		SHADER_PARAMETER(float, Darkness)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

// MOD-POINT 2026-09-11
// 黑雾边缘平滑 Pass1（去凸角）：中心可见且某角方向的两个正交邻格+对角格都不可见
// → 该格是"凸出来的角" → 直接去掉（0）。削掉格状边界的凸角/台阶端点，边界圆润。
class ASCESHADERS_API FAsceFogCornerCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FAsceFogCornerCS);
	SHADER_USE_PARAMETER_STRUCT(FAsceFogCornerCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, FogRawTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, CornerUAV)
		SHADER_PARAMETER(FIntPoint, GridSize)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

// MOD-POINT 2026-09-11
// 黑雾边缘平滑 Pass2（3×3 平均 = 软膨胀 + 模糊）：读去凸角结果做 3×3 平均，
// 得到 0~1 连续可见度（边缘一圈 1/9..8/9 渐变），PS 双线性采样后过渡约 2 格宽。
class ASCESHADERS_API FAsceFogSmoothCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FAsceFogSmoothCS);
	SHADER_USE_PARAMETER_STRUCT(FAsceFogSmoothCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, FogCornerTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, FogSmoothUAV)
		SHADER_PARAMETER(FIntPoint, GridSize)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
