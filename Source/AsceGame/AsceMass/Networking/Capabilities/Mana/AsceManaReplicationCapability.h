// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "AsceMass/Networking/Core/AsceValueReplicationCapability.h"
#include "AsceManaReplication.h"

/** 蓝量复制能力：服务器侧差量判定 + 标脏，全由通用模板驱动 */
using FAsceManaReplicationCapability = TAsceValueReplicationCapability<FAsceManaPolicy>;
