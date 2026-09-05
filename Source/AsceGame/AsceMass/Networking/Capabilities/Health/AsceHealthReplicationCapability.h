// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "AsceMass/Networking/Core/AsceScalarReplicationCapability.h"
#include "AsceHealthReplication.h"

/**
 * "生命值同步"能力：只有挂了FAsceHealthFragment的entity才会被这个能力实际处理，
 * 跟坐标同步（FAsceTransformReplicationCapability）完全独立，不要求entity同时具备Transform。
 *
 * 就是TAsceScalarReplicationCapability套上FAsceHealthRatioPolicy，不用手写任何逻辑——
 * 新增护盾/蓝量等标量属性时，照这一行抄一份换个Policy即可（不需要.cpp）。
 */
using FAsceHealthReplicationCapability = TAsceScalarReplicationCapability<FAsceHealthRatioPolicy>;
