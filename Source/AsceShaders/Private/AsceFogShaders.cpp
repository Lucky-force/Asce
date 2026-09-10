// Fill out your copyright notice in the Description page of Project Settings.

#include "AsceFogShaders.h"

#include "Modules/ModuleManager.h"

// 独立 PostConfigInit 模块（见 AsceShaders.Build.cs 说明）：
// 保证 shader 类型在 LaunchEngineLoop 的 CommitAll 之前完成静态注册。
IMPLEMENT_MODULE(FDefaultModuleImpl, AsceShaders);

// MOD-POINT 2026-09-10 14:49:13
// 注册黑雾 Compute Shader（源：项目 Shaders/AsceFog.usf，映射到虚拟路径 /Project/AsceFog.usf）
IMPLEMENT_GLOBAL_SHADER(FAsceFogCS, "/Project/AsceFog.usf", "MainCS", SF_Compute);

// MOD-POINT 2026-09-10 16:20:00
// 注册黑雾 Fullscreen Pixel Shader
IMPLEMENT_GLOBAL_SHADER(FAsceFogPS, "/Project/AsceFog.usf", "MainPS", SF_Pixel);

// MOD-POINT 2026-09-11
// 黑雾边缘平滑：Pass1 去凸角 + Pass2 3×3 平均（软膨胀/模糊）
IMPLEMENT_GLOBAL_SHADER(FAsceFogCornerCS, "/Project/AsceFog.usf", "MainCornerCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FAsceFogSmoothCS, "/Project/AsceFog.usf", "MainSmoothCS", SF_Compute);
