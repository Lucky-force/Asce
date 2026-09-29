# Mass 实体复制设计（owner / 被动 bubble、单项开销、预算）

> 适用：`Source/AsceGame/AsceMass/Networking/**`。
> 相关：`Combat/技能系统设计.md` §6（值通道/动作通道的具体载荷）。

## 1. 架构：一个 owner + 若干被动 sub-bubble

项目按**属性**把一个 Mass 实体拆成多条复制通道，每条通道 = 一个 `AMassClientBubbleInfoBase` 子类（每个客户端一份），
内部是一个 `FFastArraySerializer`。

- **owner（主导）通道 = Transform**（`Capabilities/Transform/AsceTransformReplication.h`）：
  - 每个复制实体都有 `FTransformFragment`；
  - **它是唯一跑引擎默认 spawn 路径的通道**：客户端 `TClientBubbleHandlerBase::PostReplicatedAddEntitiesHelper`
    按 `TemplateID` 分组 → `UMassSpawnerSubsystem::SpawnEntities(TemplateID, ...)` 生成实体；
  - **也是唯一拥有共享表 `UMassReplicationSubsystem::EntityInfoMap` 的 add/remove 语义的通道**
    （一个 NetID 只能由一个通道负责登记/销毁）。
- **被动（sub）通道 = 其余全部**（Velocity / Action / Health / Mana / Team / SkillPanel …）：
  - 派生自 `Core/AscePassiveClientBubbleHandler.h`；
  - 客户端**不生成、不销毁实体**，只按 `NetID` 去 `FindMassEntityInfo(NetID)` 找 owner 已生成的实体，把 payload
    写回自己的 fragment；实体还没到就进 `PendingAgents`，`Tick` 里补写。

```
客户端生成实体 —— 只有 owner（Transform）通道需要 TemplateID
被动通道的每一条，实体早由 owner 生成，只需要 NetID 去认领
```

**不变量**：凡是走被动通道复制的实体，**必须同时被 owner（Transform）通道复制**（否则没人给它 spawn）。
项目里所有复制实体都带 `FTransformFragment`，满足。

## 2. 单项实际上线字节（关键：引擎身份开销占大头）

FastArray 每次 `MarkItemDirty` 是**整项全量**序列化
（`FFastArraySerializer::FastArrayDeltaSerialize`：写一个 `uint32` 元素 ID + `NetSerializeStruct(整 item)`，
见 `FastArraySerializer.h`），所以每项每次变化都要发：

| 来源 | 内容 | 字节 |
|---|---|---|
| FastArray 元素 ID | `uint32 ID`（"dont pack"） | 4 |
| `FReplicatedAgentBase` | `NetID` | ~4 |
| `FReplicatedAgentBase` | **`TemplateID`**（`FGuid`16 + `uint32 FlavorHash`4 + `uint64 TotalHash`8，无 NetSerialize） | **28** |
| 各通道 payload | Action 7 / Health 8 / Mana 8 / Team 2 / SkillPanel 24 / Velocity 6 / Transform ~12 | 2–24 |

**≈ 38–60 B/项**，其中身份开销 ≈ 32B（TransformerID 占 28B）。

## 3. 单帧上限与容量

引擎限制（普通游戏连接）：

- 单包 `MAX_PACKET_SIZE = 1024`（`CoreNet.h`）——单个 bunch 会被拆成多个 1024B 的包；
- **单个 actor 通道每帧构造出的一个 bunch 上限 = `net.MaxConstructedPartialBunchSizeBytes`，默认 `1024*64 = 65536`**
  （`DataChannel.cpp`）。超了：
  - Development/Editor：`UChannel::SendBunch` 的 `ensureMsgf` **断点**并丢弃；
  - Shipping：发送侧 guard 被编译掉，但接收侧是普通 `if` → 重组后 >64KB 判定 `PartialTooLarge` **断开连接**。

**容量 ≈ 65536 / 每项字节**：

| 通道 | 每项字节 | 每帧可容纳（≈） |
|---|---|---|
| Action | ~43 | ~1500 |
| Health / Mana | ~44 | ~1490 |
| Team | ~38 | ~1720 |
| SkillPanel | ~60 | ~1090 |
| Velocity | ~42 | ~1560 |

⇒ **任一通道单帧同时变化超过一千多个实体，就会顶到上限**（如一次 AOE 打几千个单位）。
spawn 队列只管"新增"，管不了"已存在实体属性同时变化"，两者都要考虑。

## 4. 轻量 Agent：被动通道不复制 `TemplateID`

被动通道客户端只需要 `NetID`，不需要 `TemplateID`。所以被动通道用
`Core/AsceLightReplicatedAgent.h` 的 `FAsceLightReplicatedAgentBase` 替代引擎的 `FReplicatedAgentBase`：

```cpp
USTRUCT()
struct FAsceLightReplicatedAgentBase
{
    GENERATED_BODY()
    void SetNetID(const FMassNetworkID v);  FMassNetworkID GetNetID() const;
    void SetTemplateID(const FMassEntityTemplateID v);  FMassEntityTemplateID GetTemplateID() const;
private:
    UPROPERTY()              FMassNetworkID NetID;        // 复制
    UPROPERTY(NotReplicated) FMassEntityTemplateID TemplateID;  // 服务器内存用，不上线
};
```

可行性依据（引擎源码）：

- **`NotReplicated`（`CPF_RepSkip`）在 struct 序列化里会被跳过**：`FRepLayout::InitFromStruct`（`RepLayout.cpp`）里
  `if (It->PropertyFlags & CPF_RepSkip) continue;`；FastArray item 走
  `FNetSerializeCB::NetSerializeStruct` → `RepLayout->SerializePropertiesForStruct`（默认非 native 路径）⇒ 生效。
  引擎自己在同一场景就这么用：`FMassFastArrayItemBase::Handle` 就是 `UPROPERTY(NotReplicated)`。
- 客户端读 `TemplateID` 的地方**只有 spawn 那一处**（`PostReplicatedAddEntitiesHelper`），被动 handler 已覆写
  `PostReplicatedAdd`，不会调它。
- 服务器侧读 `TemplateID` 的地方（`DebugValidateBubbleOnServer` 的 `checkf(GetTemplateID().IsValid())`、
  与 `FReplicationTemplateIDFragment.ID` 比对）读的是**内存值**，服务器照常 `SetTemplateID` ⇒ 通过。
- 客户端 debug 校验 `check(GetTemplateID().IsValid())` 被 `UE_ALLOW_DEBUG_REPLICATION` 包住，**默认 0**
  （`MassReplicationTypes.h`）。**若手动开 `UE_DEBUG_REPLICATION=1`**，该 check 会在客户端失败——那是调试开关，
  正常构建不受影响。

**收益**：被动通道每项每次 **-28B**，容量翻倍以上（Action ~1500 → ~4300）。**owner（Transform）通道不能去**
（它就是靠 `TemplateID` spawn 的）。

采用该基类的通道：`Velocity / Action / Health / Mana / Team / SkillPanel`；`Transform` 保持 `FReplicatedAgentBase`。

## 5. 注意事项

- **跨通道没有顺序保证**：一条通道内绝对值为最新即真相；跨通道（如 Action vs Health）可能短暂不一致。
- **别用复制值做客户端门控逻辑**：值有延迟/合并，关键判定一律服务器权威。
- **绝对值可合并，事件/边沿不可**：状态（HP/位置/当前动作）用 latest-wins；必须"每次都观察到"的事件（挥砍/死亡/
  命中特效）应另开**追加式事件通道**，不要靠轮询绝对值。
- **单帧硬预算 + 公平分帧（防崩）**：见 §7。值为绝对值 ⇒ 超预算的项**跳过写入**（不写影子），下帧仍是候选；
  **不要**调大 `net.MaxConstructedPartialBunchSizeBytes`（只是把断言换成带宽尖峰/接收侧断线）。

## 6. 通道清单

| 通道 | 文件 | 角色 | Agent 基类 |
|---|---|---|---|
| Transform | `Capabilities/Transform/*` | **owner**（spawn/销毁/EntityInfoMap） | `FReplicatedAgentBase` |
| Velocity | `Capabilities/Velocity/*` | 被动 | `FAsceLightReplicatedAgentBase` |
| Action | `Capabilities/Action/*` | 被动 | `FAsceLightReplicatedAgentBase` |
| Health | `Capabilities/Health/*` | 被动 | `FAsceLightReplicatedAgentBase` |
| Mana | `Capabilities/Mana/*` | 被动 | `FAsceLightReplicatedAgentBase` |
| Team | `Capabilities/Team/*` | 被动 | `FAsceLightReplicatedAgentBase` |
| SkillPanel | `Capabilities/SkillPanel/*` | 被动 | `FAsceLightReplicatedAgentBase` |

## 7. 单帧硬预算 + 公平分帧（防崩，已实现）

**问题**：单通道每帧构造的 bunch 有硬上限（`net.MaxConstructedPartialBunchSizeBytes` 默认 64KB，见 §3）。
一帧内变化的项数可能远超它（AOE 一次改几千个、拖视角一大批进入相关集、大部队每帧位置都在变）
⇒ Development 断言 / Shipping 断线。**光靠"静态估算容量"救不了（数据会动态变大）**，必须有运行时的硬上限。

**做法**（`Core/AsceServeWindow.h`，每 **通道 × 客户端** 一份）：

- **单帧硬上限**：按字节/项数记账，超了就**停止**（不再 MarkItemDirty）⇒ 写出去的 bunch **永不超 64KB**，
  与"这帧变了多少"无关 ⇒ **永不崩**。超出的项**跳过 = 不写影子副本** ⇒ 下帧比较仍不等 ⇒ **仍是候选（不丢）**。
- **Add/Remove 也落在同一条票据轴上（待加入票表）**：光给 item 打票不够——待加入的实体还没有 item；而且 flapping（反复进出）时 item 销毁会把票也带走，前排会反复刷额度饿死后排。所以：
  - `ServeWindow` 旁边多一张 `TMap<FMassNetworkID, uint32> PendingTickets`，**只装"当前不在本 bubble 里"的实体**（待加入 / 已被移除，key = NetID，**0 = 从未服务**）。
  - **Add**：先查这张表拿旧票（无条目 = 从未服务）；票落在窗口内 ⇒ **挡住**（不许加）；否则扣额度、发、`CommitAdd` **把票移入 item**（新票 + 时间）。
  - **Remove**：**无条件执行**（不扣额度、不下窗口）；执行前 `SavePendingTicket` **把 item 的票收回表**（仅当仍在窗口内）⇒ flapping 实体"再进来"时仍被挡住。
  - **Modify**：直接读 **item 上的票** ⇒ **热路径零查表**。⇒ **只有 Add 路径多一次（很小的）查表**。
  - 表有界（满了整体清空，保护只是临时的、丢了会自愈）；票已出窗口的条目等价于"没票"，下次 Add 遇到时自然清掉。
- **Remove 也走共用预算**（它每项只写一个 `int32 ID` ≈ 4B）：额度满 ⇒ 下帧重试（实体仍在 bubble、LOD 仍 Off，自愈）。
  共用是安全的：吃预算的项（Add/Modify）都会**打票** ⇒ 下一帧让位 ⇒ 总会有空帧轮到 Remove；代价只是"幽灵"多挂 1~几帧。
  - 额度满时**跳过昂贵的距离计算**（Transform/Velocity），但**仍走完循环**以处理 Remove。
- **公平轮转（服务票据 + 轮询窗口）**：
  - 每项存 `LastServedTicket`（`uint32`，**0 = 从未服务**）；通道级 `NextTicket` 单调自增（环形回绕，跳过 0）。
  - `RoundMarks`：每帧开头 push 当前 `NextTicket`；`[Last() .. [0]]` = "本轮已服务"的号段窗口，落在其中的项本帧跳过。
  - 某帧"**没发满**" ⇒ pop 最老标记（缩小窗口）⇒ 更早发过的项又能被发。满载时窗口 ≈ 旋转周期 `ceil(M/N)` 帧（个位数，不会无限涨）。
  - ⇒ **`M` 个待发、每帧 `N` 个 ⇒ 分 `ceil(M/N)` 帧公平发完，不饿死**（跳过者留下、发出者退出本轮）。
- 落地：`FAsceLightReplicatedAgentBase::LastServedTicket`（被动通道）+ `FAsceTransformReplicatedAgent::LastServedTicket`（owner）；
  门控加在三处 `SetXxxFromFragment`（值通道模板 / 位置 / 速度）。

**收益**：无论数据怎么涨，最坏从"**崩/掉线**"降级为"**更新变慢**"，且是**公平轮转**（不是"永远只有最前面那批"）。

**按距离的更新间隔（近快远慢）** —— 已实现于**连续量通道（Transform / Velocity）**（`Core/AsceReplicationInterval.h`）：

- 在票据门**之前**再加一道 `Now - LastUpdateTime >= Interval(dist)`；没到点 ⇒ 跳过（不写、不 stamp ⇒ 不算"本轮已服务"）。
- 距离 = `DistSquared(实体位置, viewer)`；viewer = 该客户端 bubble 的 Owner PC 的 Pawn（被客户端相机焦点驱动）。
- 分档（`DistSquared` 比较，避免 sqrt）：近 0.01s（≈每帧）/ 中 0.05s / 远 0.3s / 更远 1s；数值在 `AsceReplicationInterval.h`，实跑后调。
- **值/边沿通道（Health/Mana/Team/SkillPanel/Action）不做**：它们低频/边沿、payload 也小，interval 省不了多少。

## 8. 实体生命周期（先停复制、再销毁，已实现）

目标：任何来源（阵亡 / 子弹命中 / 建筑被拆 / 召唤物到期）都能让实体"**先从客户端消失、再在服务器销毁**"，且**零幽灵**。

**数据** `FAsceLifecycleFragment`（核心 trait 挂载，**服务端专用、不复制**）：
- 低 15 位 = `BubbleRefCount`（还有多少个 bubble item 持有它）；最高位 = `bDestroyRequested`（打包进一个 `uint16`，零浪费）。
- "请求销毁" = `SetDestroyRequested(true)`。

**流程**：
1. **复制侧**（`UAsceUnitReplicator` 单趟，**紧跟在视野过滤之后**）：`bDestroyRequested ⇒ LOD = Off`
   ⇒ 各能力按现成的 `LOD Off ⇒ Remove` 路径移除（走 Remove 的预算/票据，额度满就下帧补）。
2. **能力的 Add 成功 `IncBubbleRefCount` / Remove 成功 `DecBubbleRefCount`**（值/动作/位置/速度四处）。
3. **销毁阶段**（`UAsceSimulationSubsystem::RunDestroyStage`，**帧首、阶段 2 之前**、权威端门控）：
   `bDestroyRequested && BubbleRefCount == 0 ⇒ DestroyEntity`。

**为什么用引用计数而不是"复制侧置的标志"**：Remove 会被单帧额度**推迟**；若按"LOD 曾 Off"就销毁，会早于真正的移除 ⇒ **永久幽灵**。计数归 0 才销毁 ⇒ 与推迟无关。

**为什么在帧首**：项目不变式"实体创建/销毁必须在阶段 2（空间数据）之前"（否则空间层收录已销毁实体/留悬空句柄）。

**死亡流程（`UAsceDestroyQueueSubsystem`，已实现）**：
1. **死亡现场**（HP ≤ 0 的结算处，战斗 processor）：`FAsceLifecycleFragment::SetDying(true)`（去重；战斗扫描/索敌据此跳过它）+ **清移动命令、取消当前动作**（停行动）+ `Enqueue(Entity)`。
2. **帧首 drain ≤ `MaxPerFrame`**（限流，防大批死亡一帧砸爆 RPC/移除/销毁）：对每个待销毁实体
   - 按**各 controller 视野**判定发给谁 —— 复用 `Fog.IsEntityVisibleToLocalTeam(EM, Entity, TeamKey)`（**与复制过滤同一口径**：友方恒可见、高度场未就绪不裁）；
   - **本机 PC**（standalone / 听服 host）**不走 RPC**（引擎不会给自己发 Client RPC）⇒ 直接本地调 `AsceDeath::PlayLocalDeath`；
   - 其余 ⇒ **每个 controller 一条 `Client_NotifyDeaths(NetID 数组)`**（不广播，也不用去查 bubble 持有状态）；
   - `SetDestroyRequested(true)` ⇒ 复制侧 Off ⇒ 各 bubble Remove ⇒ 计数归 0 ⇒ 销毁阶段销毁。
3. **本地死亡表现**（`AsceMass/Lifecycle/AsceDeathPresentation.h` 的 `AsceDeath::PlayLocalDeath`，**两端共用**：客户端在 `Client_NotifyDeaths` 里调、权威端在 drain 里调）：
   - **隐藏** = `FAsceLifecycleFragment::SetDying(true)`（**复用同一个 bit，不新增字段**）：
     - 客户端**本地写**：该 fragment **不复制**（`Packed` 不在任何复制通道里）没人覆盖；核心 trait **恒挂**它 ⇒ 客户端实体也有它；将来也不会复活，所以不需要清除。
     - 权威端死亡现场已置（幂等）。
   - **真正撤画面的是** `AsceLocalFogVisibilityProcessor`：它**每帧**把 `IsDying()` 实体的表现 LOD 覆写成 `Off`（⇒ `LODRepresentation[Off]=None` ⇒ 引擎撤实例）。**必须每帧写** —— 距离 LOD 每帧重算，一次性写（比如在 RPC 里）会被同帧覆盖。
   - **死亡特效**：生成"客户端本地死亡特效实体"（`query` 只匹配本地实体，绝不用"全实体"的 fragment）—— **下一步接**，目前只置位 + 日志。
   - **RPC 先于 Remove**：客户端 `FindEntity(NetID)` 时实体通常还在；万一没找到（被视野裁掉 / 已移除）⇒ 直接跳过 —— 隐藏本来就无所谓，只有"特效要读实体信息"才依赖它。
4. **销毁阶段**（`UAsceSimulationSubsystem::RunDestroyStage`，帧首、阶段 2 之前、权威端，见上）：`bDestroyRequested && BubbleRefCount == 0 ⇒ DestroyEntity`。

**为什么死亡用"逐客户端 RPC + 客户端本地特效"，而不是让实体自己播 `Dying`**：
- 让实体播要**等动画结束**才 Off/移除 ⇒ 服务器为表现滞留实体；
- 更关键：**设备/建筑这类实体没有动作通道**，做不到；
- 特效做成**独立的客户端本地实体**后，服务器一发通知就能立刻走移除/销毁，不被客户端动画时长牵制。
- 逐客户端视野裁剪：死亡那一刻不在某客户端视野里 ⇒ 它本来也没有这个实体 ⇒ 不发（也不用处理）。

**不把死亡标志塞进 Remove**：引擎 FastArray 的 Remove 只写一个 `int32 ID`（`WriteDeltaHeader` 的 `DeletedIndices`），**没法附带任何 payload**。

**不做**：客户端不跑销毁阶段（实体销毁靠收 Remove，引擎已做）。

## 9. 待办 / 后续可选优化

- **【待办】单位运行时属性同步**：`FAsceCombatFragment`（攻速/攻击力/射程…）与移动/视野等**运行时属性**目前只在服务器，
  客户端拿不到（`FAsceCombatFragment` 是服务器权威、不复制）。等被动技能/装备/buff 要改这些属性时，需要把它们同步给客户端
  —— 像 Health/Mana 那样开一条"属性"值通道（`TAsceValueReplicationCapability`），或在属性变化的边沿推一次。
  - **触发条件**：做"改攻速/攻击力"的 buff 或被动物品时。
  - **连带收益**：客户端一旦能拿到攻速 ⇒ **删掉动作 payload 里的 `PlayRateQ`**（客户端本地按 `AttackSpeed/200+0.1`
    自己算；技能播放速度放进技能表/动作 def，反正动作表也要下发到客户端）。
- **payload 精简**（尾数）：
  - ✅ 已去掉 Action 的 `UnitTypeId` —— 单位类型是**实体模板常量**（`UAsceRTSEntityTrait::UniqueType` →
    `FAsceUnitDefFragment.UniqueTypeId`，两端注册模板时写入），客户端 spawn 即有；与"被动通道不复制 TemplateID"同源
    （都是"模板已给的东西不必再发"）。
  - ⏳ `PlayRateQ`：**暂时保留** —— 它是服务器按**真实攻速**算的，攻速被 buff 改时会自动跟着变；等上面的"属性同步"落地后再删。
  - 可选：把 `ActionState(3b) + ActionDefId(8b) + Counter(6b) + FacingQ(7b)` 位打包进一个 `uint32`（payload 5B→4B）。
    注意：struct/FastArray **按 UPROPERTY 逐字段序列化，`uint8` 就是 1 字节，不会自动位打包** ⇒ 得合并成一个整数字段
    自己读写位，或自定义 `NetSerialize`（`WithNetSerializer=true`）。
- **可选**：把"按距离的更新间隔"（§7）从 Transform/Velocity 扩展到值通道（价值不大，低频/边沿）。
- **事件化**：AOE 这类"一个事件导致 N 个单位变化"的场景，复制一个事件让客户端本地结算，而不是 N 份值增量。
