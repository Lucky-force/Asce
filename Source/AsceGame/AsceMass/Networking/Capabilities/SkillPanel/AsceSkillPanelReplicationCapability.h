// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "AsceMass/Networking/Core/AsceValueReplicationCapability.h"
#include "AsceSkillPanelReplication.h"

/** 技能面板复制能力（槽位配置 + 每槽 CD）：服务器侧差量判定 + 标脏，全由通用模板驱动 */
using FAsceSkillPanelReplicationCapability = TAsceValueReplicationCapability<FAsceSkillPanelPolicy>;
