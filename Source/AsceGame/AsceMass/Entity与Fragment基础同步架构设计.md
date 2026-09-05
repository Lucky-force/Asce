# Entity 与 Fragment 基础同步架构设计

> 本文档奠定"MassEntity驱动的建筑/单位系统 + 联机同步"这套架构的基础框架，后续具体功能（各类建筑、技能、UI交互等）都在此框架上做增量扩展，不应反过来修改本文档定义的核心机制。

## 0. 背景与前提

- 游戏类型：RTS/SLG，场上单位规模可达数百~上千，已有基于Sector的格子化流场寻路系统（Eikonal FlowField）。
- 联机规模：同时在线玩家 **≤10人**，属于小规模房间对战，不是大规模MMO。
- 视野系统：项目本身就需要战争迷雾/视野系统，这是本同步框架的核心依赖之一。
- 网络模型：**服务器权威（Server-Authoritative）**，非确定性锁步。理由：
  - 锁步要求全平台bit-exact的确定性模拟，工程风险和维护成本极高，且我们已有的流场计算是浮点驱动的，重构成本大。
  - 服务器权威模型下，Mass模拟只在服务器跑，客户端只是"瘦表现层"，实现和调试成本低得多。
  - 10人规模、带战争迷雾的RTS/SLG，天然适合"按视野做兴趣管理"来控制同步量，不需要考虑大规模横向扩展。

## 1. 总体分层

> 已核实（见`Engine/Plugins/Runtime/MassGameplay/Source/MassReplication/Public/MassClientBubbleHandler.h`）：客户端**并不是"不跑Mass"**，而是跑一套完全独立的、纯表现向的Mass entity。证据：`TClientBubbleHandlerBase::PostReplicatedAddEntitiesHelper`收到FastArray"新增"事件后，会调用`UMassSpawnerSubsystem::SpawnEntities`在客户端本地`FMassEntityManager`里真实创建entity；收到"移除"事件（`PreReplicatedRemove`）则调用`SpawnerSubsystem->DestroyEntities`真实销毁。服务器entity和客户端entity是**两个独立的EntityHandle空间**，之间没有直接对应关系，靠`FMassNetworkID`（网络层稳定ID）做关联——客户端entity身上的`FMassNetworkIDFragment`记录"我对应服务器哪个网络ID"，服务器每帧把`ReplicatedAgent`数据通过FastArray同步过来后，客户端Bubble Handler用这个NetID找到对应的客户端entity并写入其表现Fragment。

```
服务器（Mass EntityManager #1：游戏逻辑权威实例）
  ├─ Template：挂满游戏逻辑Fragment（血量、AI、寻路相关、CostField交互等）
  ├─ 各类Processor：驱动Fragment的每帧行为（移动、交互、技能、CostField写入等）
  └─ 同步层（本文档重点）：把"哪些entity的哪些Fragment"打包成ReplicatedAgent，同步给"哪些客户端"

客户端（Mass EntityManager #2：纯表现向实例，与服务器entity完全独立）
  ├─ Template：只挂Transform/LOD/可视化相关Fragment，不含任何游戏逻辑Fragment
  ├─ 表现Processor：只做"根据Fragment数据驱动模型/动画/UI"，不跑任何权威逻辑
  ├─ 收到FastArray增删改事件 → 通过FMassNetworkID关联 → Spawn/Destroy/更新客户端本地entity
  └─ 玩家操作 → 发RPC给服务器，不在本地直接修改任何权威状态
```

**核心原则**：客户端的Mass entity和服务器的Mass entity是两套完全独立的数据（不同Template、不同Fragment组合、不同EntityHandle），客户端entity永远不持有权威数据，只持有"服务器广播下来的镜像"。所有权威判定（打不打得中、能不能交互、冷却好了没）只在服务器的entity上执行。

## 2. 同步范围判定：Entity级别

### 2.1 判定依据：只看战争迷雾可见性，不叠加Viewer距离过滤

结论（已拍板）：**一个entity要不要同步给某个玩家，只取决于它所在Sector当前是否在该玩家的可见Sector集合里，不额外叠加"距离摄像机远近"的二次过滤。**

原因：
- 若叠加Viewer距离过滤，边界判定会随摄像机拖动而每帧变化，导致entity在Bubble中频繁进出（churn），带来明显的表现层创建/销毁开销和视觉抖动（弹出感）。
- 战争迷雾边界的变化频率取决于"单位视野随移动而改变"，比摄像机拖动慢一个数量级以上，churn问题自然大幅减少。
- 10人规模下，单个玩家视野内的entity总量有上限，不叠加距离过滤也不会导致同步量失控。

> 备注（非本阶段实现，仅记录留待未来按需启用）：若未来实测带宽紧张，可以在**不改变Bubble成员判定**的前提下，单独对"距离玩家较远"的entity降低同步**更新频率**（而不是移除出Bubble），这是与本节结论正交、可随时叠加的独立优化手段。

### 2.2 数据依赖

- 战争迷雾系统需要产出：`每个玩家 -> 当前可见SectorID集合`，这是本框架的唯一外部依赖输入。
- Entity的Sector归属：复用现有寻路系统的`FSourcePosition`/`GridLocationToSectorId`能力，entity的位置换算成SectorID即可查表。

### 2.3 判定与广播流程（服务器每帧/每N帧执行一次）

```
对每个玩家 P：
  VisibleSectors = 战争迷雾系统.GetVisibleSectors(P)
  对每个需要同步的entity E：
    bShouldBeInBubble = E.CurrentSector ∈ VisibleSectors
    若 bShouldBeInBubble 且 E 不在 P 的Bubble里：
        加入Bubble（下一次FastArray同步会带上"新增"）
    若 !bShouldBeInBubble 且 E 在 P 的Bubble里：
        从Bubble移除前，先把E的"最后已知状态"标记打包发一次（战争迷雾记忆用）
        移出Bubble（FastArray同步"移除"事件）
    若 E 一直都在Bubble里且数据发生变化：
        更新Bubble里对应条目（FastArray同步"变化"事件）
```

## 3. 同步范围判定：Fragment级别

Entity本身不是同步的最小单位，真正同步的是"这个entity身上，哪些Fragment的哪些字段"。原则：

- **不是所有Fragment都需要同步**。只有影响客户端表现/UI的Fragment才需要，纯服务器内部计算用的Fragment（比如寻路用的`FMergedPathPlanHandle`、CostField相关的内部簿记）永远不同步。
- 每个需要同步的Fragment，对应一份**精简过的"复制代理"结构**（Replicated Agent），只包含客户端表现真正需要的字段，不是Fragment原始数据的镜像。

### 3.1 复制代理（Replicated Agent）设计

```cpp
// 基础字段，所有entity都需要
USTRUCT()
struct FAsceReplicatedAgentBase
{
    FMassNetworkID NetID;       // 服务器/客户端两套独立EntityHandle之间唯一的关联依据，不能用EntityHandle本身
    FVector2D WorldLocation;
    float FacingAngle;
};

// 需要显示血条的entity才带这个
USTRUCT()
struct FAsceReplicatedHealthInfo
{
    float CurrentHealthRatio; // 传比例而不是绝对值，减少客户端换算逻辑、也顺便隐藏部分数值信息
};

// 需要显示buff图标的entity才带这个
USTRUCT()
struct FAsceReplicatedBuffInfo
{
    TArray<FGameplayTag> ActiveBuffTags; // 只传Tag用于图标显示，不传具体数值/剩余时间等敏感信息
};
```

**设计要点**：
- 复制代理是**表现导向**的数据，不是Fragment的1:1镜像。哪些字段该暴露给客户端，需要按"防作弊/防开图"的原则做取舍（比如具体剩余CD数值、具体伤害计算参数不应该同步给非本人客户端）。
- 不同entity类型按需要组合不同的复制代理子结构（类似Fragment组合的思路延伸到网络层），而不是设计一个大而全的结构体塞给所有entity。

### 3.2 Fragment → 复制代理 的更新触发

不建议每帧对所有Fragment做全量diff（浪费CPU），应该采用**"写入时标记脏"**的模式：

- 关键Fragment的写入路径（比如`FAsceHealthFragment`被`UAsceDamageProcessor`修改后）主动调用一次"标记该entity的复制代理需要刷新"（往一个"脏Entity列表"里记一笔）。
- 同步Processor每帧/每N帧只处理"脏列表"里的entity，重新生成复制代理并检查是否需要推送给相关客户端的Bubble，而不是遍历全部entity做diff。
- Bubble成员关系变化（2.3节的加入/移出判定）和"字段内容变化"（本节）是两条独立的触发路径，都会导致对应客户端的FastArray产生一次更新。

## 4. 命令流：客户端 → 服务器

客户端不直接修改任何状态，所有操作走RPC + 服务器校验：

```
客户端：玩家操作（移动指令/交互/施放技能）
   → 发送轻量RPC（携带：发起者NetID(可能多个)、目标NetID或目标坐标、操作类型）

服务器：收到RPC
   → 按下方"NetID寻址"把RPC里的NetID换成服务器权威EntityHandle
   → 校验合法性（距离/冷却/权限/资源是否足够）
   → 校验通过：写入对应Fragment（如 FAsceInteractionOrderFragment / 技能Ability执行）
   → 校验不通过：直接丢弃（不需要显式告知客户端失败，或按需返回一个轻量的"操作被拒绝"反馈）
```

不做客户端预测/回滚。10人规模、非硬核动作类游戏，"操作后有一点延迟才生效"是可接受的，避免引入预测+服务器校正这套复杂机制。

### 4.1 RPC寻址：为什么不能用EntityHandle，必须用NetID

客户端和服务器的Mass EntityManager是两个独立实例（见第1节），**EntityHandle在两边不是同一套编号，RPC参数不能传EntityHandle**，必须传`FMassNetworkID`——这是两边唯一共享、含义一致的身份标识。

- **客户端侧获取NetID**：不需要额外请求，客户端entity身上本来就挂着`FMassNetworkIDFragment`（随复制数据一起同步下来的），玩家框选操作选中哪些entity，直接读它们的`FMassNetworkIDFragment.NetID`即可。如果框选逻辑操作的是表现Actor而非直接操作Mass entity，建议在生成表现Actor时把NetID缓存一份到Actor身上，避免每次都反查EntityManager。

- **服务器侧NetID→EntityHandle反查：需要自建registry，插件本身不提供**。已核实（`MassReplicationSubsystem.h`）：
  - `GetNetIDFromHandle(Handle)`只是正向查（EntityHandle→NetID），方向不对。
  - `EntityInfoMap`（NetID→EntityHandle反查表）在源码里明确包在`#if UE_REPLICATION_COMPILE_CLIENT_CODE`里，**只存在于客户端**，服务器编译时这个成员根本不存在，是给客户端"收到NetID后找本地哪个entity要更新"用的，不是给服务器用的。
  - `TClientBubbleHandlerBase`内部虽然在服务器侧也有一份`NetworkIDToAgentHandleMap`，但它是模板类的私有成员，每种Agent/Bubble类型各自一份，不是全局可查的通用注册表。
  - **结论**：需要自己在服务器维护一份全局的`TMap<FMassNetworkID, FMassEntityHandle> GlobalNetIDRegistry`，跟着entity的NetID分配Observer（`UMassNetworkIDFragmentInitializer`的Add事件）同步维护增删，供任意RPC按NetID查权威EntityHandle使用。

### 4.2 完整命令流示例：框选多个entity发起寻路

```
客户端：框选10个entity → 读取它们各自的 FMassNetworkIDFragment.NetID →
        RPC(TArray<FMassNetworkID> SelectedNetIDs, FVector2D GoalWorldLocation)

服务器：收到RPC
   → 对每个NetID查 GlobalNetIDRegistry 换成服务器权威EntityHandle
   → 取这些entity当前的世界坐标 EntityWorldLocations
   → RunMergeablePathRequest(GoalWorldLocation, EntityWorldLocations, ...) 得到 FMergedPathPlanHandle
   → AcquirePathPlan，并把Handle写入这批entity各自的 FAsceFlowFieldHandleFragment
   → 后续每帧由服务器的 TryGetFlowDirection 驱动这批entity移动（详见寻路系统设计）
```

## 5. 与已有系统的衔接关系

| 已有/前序设计 | 在本框架里的角色 |
|---|---|
| Sector/流场寻路 | 提供entity的`SectorID`归属查询，是视野可见性判定的基础依据 |
| 建筑Footprint + CostField写入 | 服务器权威逻辑，只在服务器的Mass EntityManager里执行；`OnCostFieldUpdate`触发的FlowFieldCache失效不需要同步给客户端——客户端那份entity不含寻路Fragment，也不跑寻路Processor |
| 交互系统（`FAsceInteractionOrderFragment`等） | 客户端只发起RPC请求交互，实际的"走到范围内"判定和交互执行都在服务器Processor里跑，客户端entity只收表现层的结果同步 |
| 精简版Ability/Effect系统 | 同上，Effect/Cooldown等数值全部服务器权威，客户端只收`FAsceReplicatedBuffInfo`这种脱敏后的Tag列表用于图标显示 |
| MassReplication插件 | 复用其Bubble Actor + FastArraySerializer增量同步机制；不复用其默认的距离LOD/Grid候选筛选逻辑（本框架用视野Sector判定替代） |

## 6. 后续需要确认/细化的点

1. 战争迷雾系统"每个玩家可见Sector集合"这份数据的具体产出方式和更新频率，决定2.3节判定Processor的执行时机。
2. 复制代理的具体字段清单需要按建筑/单位/物品分别过一遍，明确"哪些数据必须脱敏（如具体数值改传比例/Tag）"。
3. "脏Entity列表"的具体实现方式（每个写入Fragment的Processor手动打标记 vs 用Mass自带的Fragment变化检测机制）需要结合MassReplication现有API做一次可行性确认。
4. 战争迷雾记忆（entity离开可见范围后，客户端保留最后一次已知状态直到重新可见）需要在客户端Bubble Handler层单独实现，这不属于MassReplication自带能力。
5. ~~`GlobalNetIDRegistry`（服务器侧NetID→EntityHandle反查表）具体挂在哪个Subsystem/Manager上、增删时机具体挂在哪个Observer上~~ **已实现，见第7.2节**：`UAsceNetIDRegistrySubsystem` + Observer Processor。
6. 第2.3节"视野Sector判定替代默认距离LOD"**尚未实现**，当前Replicator仍走引擎默认的距离LOD计算（见7.3节末尾），是下一轮的工作。
7. `UMassReplicationTrait`（引擎自带的通用Trait，挂到EntityConfig上即可，见7.3节）是否需要额外补充战争迷雾相关的自定义参数，需要在做第6条时一并确认。

---

*本文档为架构基础文档，后续新增建筑/单位/技能类型时，只需要在"Fragment目录"和"复制代理清单"里做增量登记，不应修改第1~4节定义的核心同步机制。第7节记录的是"框架本身"的实现现状，框架有实质性改动（新增机制、调整分层）时才需要更新第7节；只是新增一个具体Fragment/Policy不算框架改动，不需要碰这里。*

## 7. 实现现状（代码落地记录，随实现推进持续更新）

> 本节记录"架构设计→实际代码"的对应关系和已知偏差/风险，不是重新设计。每次对本框架的代码有实质性改动（新增机制、调整分层、解决第6节的待确认项）时，都应该回来更新本节，而不是另开文档。
>
> 代码位置：`LyraStarterGame\Source\LyraGame\Mass\`（原在`ProjectMecury`工程下，已整体迁移到Lyra工程），子目录：`FlowField`（寻路，已验证可用，不在本节范围）/`Fragments`/`Networking`（本节新增）/`ScriptBridge`/`SpatialGrid`（已验证可用，不在本节范围）/`Traits`。**`Components`目录已删除**（见下方"迁移后发现的问题"最后一条）。
>
> 迁移后编译依赖：`LyraGame.Build.cs`原本不含任何Mass模块依赖，已补充`MassEntity`/`MassCommon`/`MassSpawner`/`MassMovement`/`MassReplication`到`PublicDependencyModuleNames`；`LyraStarterGame.uproject`的`Plugins`列表补充了`MassEntity`/`MassGameplay`两个插件条目（`MassReplication`等模块所在的插件）。这些改动**同样未经编译验证**，如果实际插件名称/模块归属跟引擎版本对不上，需要按报错调整。
>
> **迁移后发现的问题**：只搬了`Mass`目录，导致部分文件对"`Mass`目录之外、原`ProjectMecury`工程里的其他文件"的依赖断了，编译报"找不到类"：
> - `AsceRTSCommanderComponent`（原`Mass/Components/`）依赖了两个没搬过来的外部文件：`Character/AsceObserverComponent.h`（玩家输入/选中框选的Observer组件）、`AsceGameplayTags.h`（自定义GameplayTag集合，其中InitState_Spawned/DataAvailable/DataInitialized/GameplayReady四个跟Lyra自己`LyraGameplayTags.h`里的定义完全一样，是照抄Lyra Init State Pattern写的）。
> - 排查后确认：这个组件本身是"玩家侧输入→选中→下发指令"的表现层胶水代码，跟本框架的同步机制（Fragment/复制代理/Replicator）没有实质关联，只是消费方之一。**已拍板直接删除**`Mass/Components/AsceRTSCommanderComponent.h/.cpp`，不再迁移/存根它依赖的外部类。以后如果RTS选框/下达指令这块UI交互要重新做，会在Lyra自己的输入/Pawn体系里重新设计，不再照搬这个组件。
> - 教训：以后再从别的工程往这边搬代码，先在原工程里搜一遍"待搬目录之外的文件对它的引用"和"它对待搬目录之外文件的引用"，避免遗漏两头都要动的耦合点。

### 7.1 Fragment目录（`Mass/Fragments/`）

按"关注点"拆文件，不是一个大文件塞所有Fragment：

| 文件 | 内容 |
|---|---|
| `AsceCoreFragments.h` | `FAsceHealthFragment`（当前/最大血量）、`FAsceTeamFragment`（阵营）、`FAsceHeightFragment`（表现/视野用，不影响寻路）、`FAsceAliveTag`/`FAsceDeadTag` |
| `AsceOrderFragments.h` | `FAsceMoveOrderFragment`（骨架，寻路Handle字段留空，等寻路运行时Subsystem落地后再接）、`FAsceAttackOrderFragment`（用`FMassNetworkID TargetNetID`寻址目标，不用EntityHandle，呼应第4.1节结论） |
| `AsceInteractionFragments.h` | `FAsceInteractableFragment`/`FAsceInteractionOrderFragment`/`EAsceInteractionState`+两个交互Tag，骨架，Processor业务逻辑未接 |
| `AsceFootprintFragment.h` | `FAsceGridFootprintFragment`骨架（Origin/Width/Height），CostField写入逻辑留白 |

明确不放的东西：`bIsSelected`这类"谁选中了谁"的客户端本地UI状态不放进任何Fragment——它是每个玩家客户端自己的概念，不是entity自身属性，不应该被同步给所有人（呼应第3.1节"复制代理是表现导向，不是Fragment镜像"的原则）。

### 7.2 NetID Registry（对应第4.1节结论，`Mass/Networking/`）

- `UAsceNetIDRegistrySubsystem`（`UWorldSubsystem`，仅非客户端创建）：内部`TMap<FMassNetworkID, FMassEntityHandle>`，提供`RegisterEntity`/`UnregisterEntity`/`FindEntity`/`IsValidNetID`。
- `UAsceNetIDRegistryAddProcessor`/`UAsceNetIDRegistryRemoveProcessor`：Observer模式，`ExecuteAfter UMassNetworkIDFragmentInitializer`，entity的`FMassNetworkIDFragment`被Add/Remove时自动维护上面那张表，不需要业务Processor手动调用注册。
- RPC收到`FMassNetworkID`后，统一走这个Subsystem换成权威`FMassEntityHandle`，对应第4.2节示例里的`GlobalNetIDRegistry`一步。

### 7.3 MassReplication Bubble骨架（`Mass/Networking/`）

照抄引擎唯一一套具体范例`Engine/Plugins/AI/MassCrowd`的模式搭的骨架，对应第3.1/3.2/5节的"复制代理"机制：

| 文件 | 对应设计文档里的角色 |
|---|---|
| `AsceReplicatedAgent.h` | `FAsceReplicatedAgent`（继承`FReplicatedAgentBase`，即3.1节的`FAsceReplicatedAgentBase`）+ `FAsceFastArrayItem`（FastArray条目） |
| `AsceReplicationScalarHandlers.h` | 见下方"通用标量属性同步"，血量等"表现向标量数值"的同步走这里 |
| `AsceReplicationFieldPolicies.h` | 每个具体标量属性（目前只有血量）的Policy定义 |
| `AsceClientBubble.h/.cpp` | `FAsceClientBubbleHandler`（客户端收到FastArray增删改后真正Spawn/Destroy客户端entity，对应第1节"客户端Bubble Handler"）、`FAsceClientBubbleSerializer`（FastArraySerializer）、`AAsceClientBubbleInfo`（每个PlayerController一份的复制Actor） |
| `AsceReplicator.h/.cpp` | `UAsceReplicator`，服务器侧每帧决定Add/Modify/Remove，对应第2.3节"判定与广播流程"的落地位置——**但目前只是引擎默认的距离LOD在驱动这个循环，还没接入战争迷雾Sector可见性**（见下方"已知偏差"） |
| `AsceReplicationBootstrapSubsystem.h/.cpp` | `PostInitialize()`里调用`RegisterBubbleInfoClass`，插件要求这一步必须在客户端连接前手动做，不会自动发生 |

**接入方式（不需要写C++）**：引擎自带通用`UMassReplicationTrait`，直接挂到EntityConfig上，把`Params.BubbleInfoClass`设成`AAsceClientBubbleInfo`、`Params.ReplicatorClass`设成`UAsceReplicator`即可。

#### 通用标量属性同步（Policy模式）

第3.1节提到"复制代理按需组合子结构，不是大而全塞给所有entity"——落地到"单个0~1标量数值"这一类场景（血量比例、以后的护盾/蓝量比例等）时，做法是：

- 一个通用数据结构`FReplicatedScalarData`（就是一个float），所有标量属性复用，不是每个属性各写一个ReplicatedXXXData。
- 两个通用模板类`TMassClientBubbleScalarHandler<AgentArrayItem, FieldPolicy>`（客户端侧）/`TMassReplicationProcessorScalarHandler<FieldPolicy>`（服务器侧），只认`FieldPolicy`这个类型参数，不认具体是哪个属性。
- `FieldPolicy`只需要提供4样东西：`FragmentType`（读写哪个Fragment）、`ExtractFromFragment`（从Fragment提取要同步的值）、`ApplyToFragment`（写回Fragment）、`Tolerance`（变化超过多少才重新标脏，避免抖动导致高频同步）。血量的实现是`FAsceHealthRatioPolicy`，20行代码。

**以后新增一个标量属性（比如护盾比例），步骤**：
1. `FAsceReplicatedAgent`里加一个`FReplicatedScalarData`具名成员 + 一对accessor
2. 照抄`FAsceHealthRatioPolicy`写一个新Policy结构体
3. `FAsceClientBubbleHandler`里加一行`TMassClientBubbleScalarHandler<FAsceFastArrayItem, 新Policy>`成员
4. `UAsceReplicator`里加一行`TMassReplicationProcessorScalarHandler<新Policy>`

不需要新写Handler类，也不用碰同步管线代码——这个是本节唯一值得强调的"框架级"设计点，属于"新增具体标量属性不算框架改动"里的模式本身需要被记录下来的部分。

**已知的语言层限制（不是bug，是C++本身的约束）**：`TClientBubbleHandlerBase<T>`只`friend`了引擎自带的`TMassClientBubblePathHandler`/`TMassClientBubbleTransformHandler`两个模板类，不认任何自定义的Handler模板。所以"服务器侧真正写值+比较容差+标脏"这个动作（需要访问protected的`AgentLookupArray`/`Agents`/`Serializer`）不能放在通用的`TMassClientBubbleScalarHandler`模板里，只能放在具体子类`FAsceClientBubbleHandler`自己身上（写成一个模板成员函数`SetScalarFromFragment<FieldPolicy>`，所有标量属性共用同一份实现，不是每个属性单独一份）。

**已知偏差/待办**：
- LOD可见性判定：当前完全是引擎默认的"距离"，第2.3节要求的"视野Sector可见性判定"还没接入，见第6节第6条。
- 寻路Handle：`FAsceMoveOrderFragment`里的寻路Handle字段是空的，等寻路运行时Subsystem落地后再接（这块不影响同步框架本身）。
- 编译依赖风险：`MecuryGame.Build.cs`目前没有显式声明`MassReplication`模块依赖，如果编译报错找不到`MassClientBubbleHandler.h`等头文件，需要手动补上。
- 所有以上代码均**未经编译验证**，改动后需要在UE里编译并按报错情况修正。

### 7.4 移动/寻路/避障Pipeline（`AsceMass/Movement/`、`AsceMass/FlowField/`，本轮新增，已编译验证通过）

> 代码已迁移到当前工程`D:\Epic\Epic Games\Projects\Test\Source\Test\AsceMass\`（本节之前的7.1~7.3记录的是迁移前`LyraStarterGame`路径下的状态，两次是不同工程阶段，路径不再一致，仅供参考历史决策依据，不代表当前目录结构）。

**Pipeline全貌**：
1. `FAsceMovementSimStateFragment.bRunFullSimulation`（bool，非Tag，避免Archetype频繁搬迁）：是否要跑完整本地模拟（流场采样+避障）。战争迷雾系统后续接管这个字段的写入，目前占位默认`true`。
2. `UAsceMovementSteeringProcessor`（挂`Avoidance`组）：只处理`bRunFullSimulation==true`的entity，读`FAsceMoveOrderFragment`+`UAscePathFindingSubsystem::GetPathFinder()->TryGetFlowDirection`拿理想方向，叠加一个简化版排斥力避障（见下方"为什么不用引擎自带Avoidance"），写`FMassDesiredMovementFragment.DesiredVelocity`。
3. 引擎自带`UMassApplyForceProcessor`（`ApplyForces`组）/`UMassApplyMovementProcessor`（`Movement`组）：`DesiredVelocity→Velocity→位置积分`，不需要自己写。
4. `UAsceMovementOrientationProcessor`（`ExecuteAfter Movement`组，对所有entity无差别处理）：朝向=速度方向，瞬时赋值不平滑，不需要网络同步（Client/Server各自算，不要求bit-exact）。
5. 对`bRunFullSimulation==false`的entity：`UAsceMovementSteeringProcessor`直接跳过，`DesiredVelocity`维持上一次的值不变——这个值以后就是网络同步下来的速度，配合引擎自带的`ApplyMovementProcessor`每帧无脑做位置积分，天然实现Dead Reckoning外推，不需要额外写DR逻辑。

**为什么不用引擎自带的Avoidance（`Plugins/AI/MassAI/Source/MassNavigation`的`UMassMovingAvoidanceProcessor`/`UMassStandingAvoidanceProcessor`）**（已拍板，本轮暂不接入）：
- 它不在`MassGameplay`插件里，而是在`MassAI`插件的`MassNavigation`模块，项目当前`Test.Build.cs`没有这个依赖。
- 源码注释原文标注`Experimental: move using cumulative forces to avoid close agents`——不是真正的RVO/ORCA（速度障碍物法），是力叠加式避障，跟本项目自己写的简化排斥力版本本质上是同一类方法，只是参数更丰富（区分moving/standing、路径起止衰减、环境墙体分离力等）。
- 深度耦合MassAI自己的Steering/Navigation体系：依赖`FMassNavigationEdgesFragment`（MassAI自己的导航网格/障碍边缘数据）、`FMassMoveTargetFragment`（MassAI自己的移动目标类型）、`UMassNavigationSubsystem`（MassAI自己的障碍物空间网格），这套跟本项目"Flow Field算方向+`FAsceMoveOrderFragment`+`UAsceSpatialGridSubsystem`查邻居"的自研架构是两条平行、不互通的技术栈。接入它等于替换掉本项目的核心自研移动系统，成本远超收益。
- 结论：继续用自己的简化排斥力版本（按邻居距离加权叠加排斥力，非完整RVO/ORCA，未考虑相对速度/预测碰撞时间）。以后如果要提升避障质量，优先在自己这套上加强（比如引入预测碰撞时间），而不是切换到引擎那一套。

**WorldMapData网络同步**（对应7.3节遗留的"寻路Handle/CostField同步"缺口，本轮补完）：
- `FWorldMapData`本身早就预留了FastArraySerializer骨架（`CostFields`成员，`PostReplicatedChange`回调也早已实现，逻辑是把变化推入`PendingUpdateCostFields`走既有的时间片处理管线），但一直没有Owner真正把它接上网络：`SetCostField`没调`MarkItemDirty`，也没有任何UObject把它声明成`UPROPERTY(Replicated)`。
- 新增`AAsceWorldMapReplicatorActor`（`AInfo`，`bAlwaysRelevant=true`）作为权威持有者：`UPROPERTY(Replicated) FWorldMapData WorldMapData`；Server（含Standalone）由`UAscePathFindingSubsystem::OnWorldBeginPlay`生成唯一一份，Client端靠引擎正常的Actor复制流程接收对应实例；`BeginPlay`时双方都调`UAscePathFindingSubsystem::RegisterWorldMapReplicator(this)`，把`UPathFinder`指向这份数据。
- `UAscePathFindingSubsystem`不再自己持有`WorldMapData`，只在Actor就位前指向一份`FallbackEmptyWorldMapData`兜底（避免读到未初始化内存）。
- 遗留TODO：关卡静态障碍物/地形分析结果具体怎么灌入`WorldMapData`初始内容还没做，`AAsceWorldMapReplicatorActor::BeginPlay`里目前是占位`InitWorldMapData(4, 4)`。

### 7.5 Position/Velocity周期同步 + 客户端误差校正（本轮新增，已编译验证通过）

**背景**：7.3节的标量Policy模式只解决了"单个float"这类场景，Position（FVector）和Velocity需要各自独立判断容差、独立标脏——如果合并成一个payload，任何一个字段变化都会拖着另一个没变的字段一起发送，做不到"谁超容差谁单独触发同步"，所以拆成两个完全独立的Capability。

- `Networking/Core/AsceReplicationPositionHandlers.h`：`FReplicatedAgentPositionData`（纯FVector）、`TAscePositionClientBubbleHandler<AgentArrayItem>`（客户端通用模板）、`FAsceReplicationProcessorPositionHandler`（服务器Processor用）。
- `Networking/Core/AsceReplicationVelocityHandlers.h`：`FReplicatedAgentVelocityData`（`int16 VelocityX/Y/Z`量化）、`TAsceVelocityClientBubbleHandler<AgentArrayItem>`、`FAsceReplicationProcessorVelocityHandler`。
- `Networking/Capabilities/Transform/`（位置）、`Networking/Capabilities/Velocity/`（速度，本轮新建）：两个独立Capability，分别只关心`FTransformFragment`/`FMassDesiredMovementFragment`。已删除旧的合并版`AsceReplicationPositionVelocityHandlers.h`。
- **速度量化**：`VelocityQuantizeScale = 10.f`（精度0.1cm/s，范围±3276.7cm/s，相对`MaxSpeed`默认300cm/s留10倍余量防止以后加速/冲刺场景截断），`FVector`在UE5是double（8字节/分量），量化后单Agent速度部分从24字节降到6字节，省75%。速度同步保持朴素Snap不做平滑——速度突变不造成视觉跳变，由引擎`UMassApplyMovementProcessor`积分位置时自然体现，不需要额外处理。
- `AsceReplicationBootstrapSubsystem.cpp`已更新注册列表：`FAsceTransformReplicationCapability`+`FAsceVelocityReplicationCapability`+`FAsceHealthReplicationCapability`；`AAsceTransformClientBubbleInfo`+`AAsceVelocityClientBubbleInfo`+`AAsceHealthClientBubbleInfo`。

**客户端误差校正（三档处理）**：
- 新增`FAscePositionCorrectionFragment`（`Fragments/AsceMovementFragments.h`）：客户端专用，`FVector TargetPosition`+`bool bCorrectionPending`。`AsceRTSUnitTrait.cpp`的`bCanMove`分支挂载。
- 三档阈值在`AsceReplicationPositionHandlers.h::ApplyToFragment`：
  - 偏差≤`PositionCorrectionIgnoreThreshold`（5cm）：忽略不动，清掉旧`bCorrectionPending`。
  - 偏差≥`PositionCorrectionSnapThreshold`（200cm）：直接Snap（复活/传送/掉线重连场景）。
  - 中间量级：记录`TargetPosition`+`bCorrectionPending=true`，交给`UAsceMovementCorrectionProcessor`（`Movement/AsceMovementCorrectionProcessor.h/.cpp`，仅`Client`执行，`ExecuteAfter Movement`）用`FMath::VInterpConstantTo`恒定速度追赶，`CorrectionSpeed = 600.f cm/s`。

### 7.6 Order RPC通道（本轮已实现，代码未经编译验证）

**动态挂载方式已明确**：不需要自建`AGameModeBase::OnGameModePostLoginEvent`订阅逻辑挂组件，改用GameFeature挂载`UAsceOrderChannelComponent`，本节不涉及这部分。

**已落地的代码**：
- `Fragments/AsceOrderFragments.h`：`FAsceMoveOrderFragment`/`FAsceAttackOrderFragment`都加了`int32 OrderSequence`字段（客户端自行递增分配，服务器只透传，不做单调性/唯一性校验，纯粹是关联ID）；`FAsceMoveOrderFragment.AcceptanceRadius`改名`AcceptanceRadiusSquared`（默认`2500.f`=50²），存平方值省去到达判定里的重复`FMath::Square`。
- `Movement/AsceMoveOrderExecutor.h/.cpp`：`FAsceMoveOrderExecutor::ApplyMergedMoveOrder(...)`静态函数，把"合并寻路请求+写入Order Fragment"这个动作从`UAsceOrderChannelComponent`里抽出来，Server的RPC handler和Client的本地预测入口共用同一份实现，避免"两处各写一遍、以后改一边忘了改另一边"。
- `Movement/AsceMovementSteeringProcessor.cpp`：到达判定从`FVector::Dist`（3D）改成`FVector2D::DistSquared`（纯XY，省开方，配合上面的平方值一起省）——`TargetLocation.Z`目前固定写0，明确高度只影响表现（以后格子高度数据也只用于entity位置Z轴的表现，不参与任何寻路/玩法判定）。
- `Networking/AsceOrderChannelComponent.h/.cpp`：
  - `EAsceOrderRejectReason`：`TooManyUnits`/`InvalidTarget`/`EntityNotFound`/`MissingOrderFragment`。
  - `RequestMoveOrder(LocalSelectedHandles, GoalWorldLocation)`：**客户端本地入口（非RPC）**，是消费方应该调用的唯一入口，不要直接调`ServerRequestMoveOrder`。内部：①读每个entity的`FMassNetworkIDFragment.NetID`拼数组，调用`ServerRequestMoveOrder`；②如果`Owner->HasAuthority()`为false（真正的纯客户端），额外立刻在本地`EntityManager`+`PathFinder`上跑一遍`FAsceMoveOrderExecutor::ApplyMergedMoveOrder`做客户端预测，不等服务器确认。Standalone/ListenServer下Owner本身是Authority，RPC会同步直接跑到`_Implementation`，这里用`HasAuthority()`挡住，避免PathPlan被多Acquire一次导致引用计数泄漏。
  - `ServerRequestMoveOrder`/`ServerRequestAttackOrder`：`Server, Reliable, WithValidation`，`_Validate`只做硬性校验（数量上限`MaxOrderSelectionCount=250`、坐标有限值），业务失败（NetID查不到/没有Fragment）走`_Implementation`里回传拒绝，不触发作弊断线。
  - **拒绝分两个粒度**：`ClientRejectWholeOrder(OrderSequence, Reason)`（依赖的Subsystem缺失等，整批entity一个都没处理）+ `ClientRejectEntityOrder(OrderSequence, UnitNetID, Reason)`（批里个别entity失败，其它entity正常）。客户端收到后应该分别处理：整批拒绝要回滚这次下单涉及的所有本地预测，单个拒绝只处理那一个entity。
  - `PendingLocalOrders`（`TMap<int32, TArray<FMassEntityHandle>>`，容量上限`PendingLocalOrdersCapacity=64`，满了丢最旧的一条）：客户端专用，记录每次本地预测下单时用到的`LocalSelectedHandles`，供收到拒绝回执时反查该处理哪些本地entity——**不需要额外的NetID→EntityHandle反查表**，因为存的本来就是本地`FMassEntityHandle`，`ClientRejectEntityOrder`要匹配`UnitNetID`时，直接在这个小数组里挨个读`FMassNetworkIDFragment`比对即可（≤250个，线性扫描足够快），绕开了"Client侧没有全局NetID反查表"这个曾经卡住的问题（详见第4.1节）。
  - 回滚/处理时都额外校验`MoveOrder->OrderSequence == 收到的OrderSequence`，防止一个"迟到"的旧订单拒绝回执，误伤这个entity后来又被下达的新命令。
  - `ServerRequestAttackOrder`目前仍是单纯的"查表+写Fragment"，没有接入`FAsceMoveOrderExecutor`/本地预测——本质上攻击也要先寻路到目标附近，这部分留到后续专门讨论。

**已知的遗留问题（未解决，暂不处理）**：
- `UAsceNetIDRegistryAddProcessor`/`RemoveProcessor`目前`ExecutionFlags`仍不含`Client`，Client侧依然没有全局NetID反查表——但上面`PendingLocalOrders`的设计绕开了这个依赖（只解决"Order通道自己反查本地entity"这一个场景，不是"通用反查表"的替代品，以后如果有其它地方需要Client侧全局反查，还是要单独解决）。
- 代码均**未经编译验证**，尤其`UFUNCTION(Server/Client, Reliable, WithValidation)`手动声明`_Validate`/`_Implementation`这块，需要实际编译过一遍按报错调整。

### 7.7 WorldMapData同步范围讨论（已拍板：全量同步，不做视野裁剪）

**问题背景**：`FCostField`（`AsceFlowFieldCores.h:28-31`）目前只有一个`uint8 Cost`字段，没有区分"地形层"（永久不变）和"动态遮挡层"（建筑/单位造成的阻挡，会变化）。最初设想过"Client只同步视野内的CostField"（省带宽/配合战争迷雾），讨论后发现这个方向问题比预想的更严重，最终否决，改为维持全量同步。


**否决"视野裁剪同步"的原因（分两层）**：
1. **逐格数值层面**：视野外的格子该用什么默认值填充，两种选择都会导致Client本地流场/A*结果跟Server权威结果分裂——默认"可通行"会让Client算出穿实际被挡区域的路径，被Server纠正时表现为路径突然改道/Snap；默认"不可通行"会让Client绕开实际可走的区域，路径显著绕远。
2. **拓扑结构层面（更致命，是否决的主因）**：寻路真正依赖的不是逐格Cost值本身，而是从CostField聚合出的`Portal`/`Island`图结构（`ProcessSectors`/`ProcessPortals`/`ProcessIslandFields`建图流程）。哪怕只有一个格子的数据在Client端过时，都可能导致某条Portal的连通性判断整体反转（Server认为两个Sector已连通，Client因为没视野还认为堵着），造成的是**宏观路线选择完全不同**（走A区还是走B区），不是局部微调。这种级别的分歧会频繁触发7.5节设的200cm Snap阈值，让"纠正应为罕见小概率事件"这个假设失效，画面会经常性大幅跳变。
   - 曾考虑过折中方案："每个PlayerController一份认知镜像，只同步视野内格子，Server权威层保留一份完整图供自己寻路用"——但这个方案如果直接拿现有`FWorldMapData`类型给每个玩家建一份镜像，`ProcessSectors`/`ProcessPortals`等flood-fill建图流程会在Server上被迫跑N遍（N=玩家数），是不可接受的开销；即使解决了"镜像只存Cost值不建图、Client本地自己建图"这个实现细节，也没有解决上面第2点的拓扑分裂问题本身，只是没让Server多花CPU，Client端体验依然会因为拓扑分裂而频繁大幅纠正。综合考虑后，认为不值得为省带宽承担这个代价，直接否决。

**最终结论：全量同步，视野迷雾只管Unit可见性，不管寻路数据**：
- `AAsceWorldMapReplicatorActor`保持`bAlwaysRelevant=true`的单一共享Actor不变，所有Client永远拿到跟Server完全相同的CostField数据，Client/Server各自本地建图（`UPathFinder`本来就是独立实例，见7.3节）后，图结构必然完全一致，寻路结果的分歧只会来自帧级别的时序竞争，跟7.5节已有的小容差纠正机制吻合，不会有拓扑级别的大幅跳变。
- "看不看得见敌方单位/建筑"（渲染/战争迷雾）和"寻路数据是否已知"是两个独立概念，本项目选择让后者不受迷雾影响——玩家可能会发现"看不见那边有什么，但单位不肯走那条路"，这是走位层面的轻微信息泄露，代价可接受。
- 带宽评估：最坏情况（地图初始化一次性全量同步）按1000x1000个CostField（每个1Byte）估算约1MB，可接受；稳态下只有真正变化的格子会被`MarkItemDirty`增量推送，实际流量远低于这个上限。项目背景本身是≤10人小规模房间对战（见第0节），全量同步的带宽压力进一步不是问题。
- `FastArraySerializer`天然保证新Client连接时第一次复制即为全量（本地数组为空，所有item当增量推送），不需要额外写代码，现状已经满足以上结论，不需要改动。
