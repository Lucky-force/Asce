// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MassEntityHandle.h"

class UWorld;

/**
 * 本地死亡表现（**两端共用**）：权威端（standalone / 听服 host）与客户端都走这里。
 *
 *  - **隐藏**：把 `FAsceLifecycleFragment::bDying` 置上（客户端本地写；权威端死亡现场已置，这里幂等）。
 *    真正把实体从画面上撤掉的是 `AsceLocalFogVisibilityProcessor`——它每帧把 `bDying` 实体的
 *    表现 LOD 覆写成 `Off`（必须每帧写：距离 LOD 每帧重算，一次性写会被覆盖）。见 `Networking/Mass复制设计.md` §8。
 *  - **特效**：生成"客户端本地死亡特效实体"（**下一步接**；目前只置位 + 日志）。
 *
 * 为什么两端共用一个入口：权威端（standalone / 听服 host）**不会给自己发 Client RPC**，
 * 必须在服务器侧本地直接调用，才能让 host 的画面和客户端一致。
 */
namespace AsceDeath
{
	/** 对本端表现层执行一次"死亡表现"（实体已失效则直接忽略）。 */
	void PlayLocalDeath(UWorld& World, FMassEntityHandle Entity);
}
