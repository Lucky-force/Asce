// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "AsceMass/Networking/Core/AsceValueReplicationCapability.h"
#include "AsceCombatAttributesReplication.h"

/** 攻击属性复制能力：服务器侧差量判脏 + 标脏，全由通用模板驱动 */
using FAsceCombatAttributesReplicationCapability = TAsceValueReplicationCapability<FAsceCombatAttributesPolicy>;
