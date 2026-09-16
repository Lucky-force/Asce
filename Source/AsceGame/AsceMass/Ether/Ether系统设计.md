# Ether 系统设计（v3）

> 建筑网络中的"电信号/物质"传播系统。目标：物流（材料传输）+ 逻辑（组合门、时序器件、反馈），
> 借鉴数字电路（组合/时序、时钟沿）与红石（tick、反馈）。
>
> **v3 变更（架构）**：把数据拆成**三层** ——
> ① **配置（共享）**：端口类型走**数据表**（`FAsceEtherPortTypeRow`），建筑布局仍是蓝图类 CDO（`UAsceEtherBuildingConfig`）；
> ② **实例（每建筑）**：Subsystem 里的 `FNode` + 端口实例数组 + 内部布线（玩家可改）；
> ③ **行为（可继承）**：`UAsceEtherProcessCore` 子类（`bInstance` 决定是否带状态；`OnInit/Tick` 自己处理自己的端口）。
> 建筑用 **slot（`FAsceEtherPortSlot`）** 声明"能挂端口的位置"，运行时 slot → 端口实例。
> 端口**不再自带入/出**：`TransportType` 由 core 在 `OnInit` 设定，**只在建图定向时用一次**，tick 推流完全不看。
> 旧 `UAsceEtherBuildingData / UAsceEtherPortType / FAsceEtherPortDef / EAsceEtherDeviceOp / FEtherDeviceStateFragment` 已删除。
>
> **v3.1 变更（数据表）**：**端口类型改成数据表** —— 全局查表系统（`UAsceDataTableSubsystem` + `UAsceDataTableSettings`）只查 **MDT**（`UCompositeDataTable`），
> 内容由各 GameFeature 用 `UGameFeatureAction_AddDataTableSources` 把 **SDT** 挂上去；端口类型表行结构 = `FAsceEtherPortTypeRow`
> （`Op` / `ThroughputPerTick` / `LinkRangeCells`，行名 = 端口类型 id）。于是 `UAsceEtherPortConfig`（蓝图类）被删除，
> slot / 端口实例里改成 `FName PortTypeId`（行名），建图时查表解析并缓存 `Op/Throughput/LinkRange`（运行时零查表）。
>
> 语义沿革：`SignalType` 为 **`int16`**（最高位 = 互补标志）；**Empty ≠ 逻辑 0**；`0 号 ether = 能量`；
> 合流 = 同族异号**湮灭** / 异族**堵死**（单入边普通节点为**符号直接翻转**）；分流 `SplitMode{Ordered,Balanced}` + **相对扫描序**。

---

## 0. 术语

| 术语 | 含义 |
|---|---|
| Ether | 网络里传输的"物质/电信号"；**不凭空产生**（无输入 ⇒ 不输出）；core 可消耗输入产出结果（网络级守恒）。 |
| SignalType | 物质类型 id（**`int16`，有符号**）。最高位 = **互补标志**：`Complement(X) = X ^ 0x8000`；`0 = 能量`，`1 = 通用材料`。 |
| Empty | **空**（`Ether == 0`）= "未定值"，**不是逻辑 0**；逻辑 0 由**互补 ether（负号态）**表示。 |
| 一族 family | 同一 `Family(X) = abs(SignalType)` 的正/互补两态（`+X` / `−X`）。一条线只承载一族；**异族 = 堵死**。 |
| 湮灭 | `+X` 与 `−X` 在**多入边合流点** 1:1 抵消（多余者留下；全抵 ⇒ 本 tick 输出空）。默认纯消失；送 **湮灭发电机** 则转成 `abs(X)` 份 **0 号 ether**。 |
| 0 号 ether | `SignalType == 0` = **能量**（族 0），与普通 ether **同规则走线**；对逻辑**透明**（`And/Or` 不参与判定、`Not` 后仍是 0）；湮灭产物；**记忆/时钟读取的"铸币"成本**。 |
| 维度 Dimension | 端口占用的逻辑层；线身份 = `(格, 方向, 维度)` ⇒ 同一格同一方向最多 8 条线。 |
| 方向 Direction | `Left / Up / Right / Down`；**扫描序** = 以源端口为基准、从它**左手边**起顺时针（§2.9）。 |
| **端口类型 PortType** | 端口类型表的**行名**（行结构 `FAsceEtherPortTypeRow`：`Op` / `ThroughputPerTick` / `LinkRangeCells`）。**玩家可换**（换行名 = 换端口行为）。 |
| **槽位 Slot** | `FAsceEtherPortSlot`：`Direction / Offset / Dimension / PortTypeId / SplitMode`（建筑的**默认端口**声明）。 |
| **端口实例 PortData** | 运行时端口：型号引用 + 实例配置（维度/方向/偏移/分流/**传输角色**）+ 缓冲 + 后继。 |
| **传输角色 TransportType** | `Input / Output / Any`。由 **core 在 `OnInit` 设定**（玩家不可改）；**只在建图定向用一次**（§2.4）。 |
| **行为 Core** | `UAsceEtherProcessCore` 子类：`bInstance / OnInit / Tick`；器件功能都写在 core 里。 |
| **建筑配置 BuildingConfig** | `UAsceEtherBuildingConfig` 蓝图子类：`DeviceType / ProcessCoreClass / 尺寸 / 支持维度 / PortSlots / 默认内布线`。 |

---

## 1. 三个基本概念

### 1.1 端口（组合节点）
- 端口 = **型号（共享）** + **实例（每建筑）**：
  - 端口类型：**端口类型表**的一行 —— `Op`（`Wire/And/Or/Not`）、`ThroughputPerTick`（= 缓冲上限）、`LinkRangeCells`（发射距离）；
  - 实例：`Dimension / Direction / Offset`（玩家可改）、`SplitMode`（玩家可改）、`TransportType`（core 设定）、`Ether / SignalType`（在制品）。
- **后继 = `LinkTargets`（内部布线）+ `OutTargetPort`（外部出边，1:1；反向索引 `InSourcePort`）**；每 tick 端口只做一件事：**把料往后继传**。
- 组合 op 按段内拓扑序**当拍求值**（0 延迟）；取值语义见 §2.7。
- **端口本身不分入/出**：`TransportType` 只是元数据（建图定向 + core 自校验 + 表现层）。

### 1.2 行为（`UAsceEtherProcessCore`）
- **`bInstance`**：`false` = 不实例化、共用 CDO 只跑函数（无状态 core）；`true` = Subsystem 为**每个建筑** `NewObject` 一个实例，
  状态就是 core 的成员变量（记忆/计数等）。**重建不会重置状态**；建筑销毁则随实例丢失。
- **`OnInit(Net, NodeId)`**：建筑生成/重建时调用一次 —— 设置自己端口的 `TransportType`（和需要的 `SplitMode`）。
  **不要在这里重置运行状态**。
- **`Tick(Net, NodeId)`**：时序器件每 tick 调用 —— 读入端口、更新自己的状态、写出端口。
- 通过 `UAsceEtherNetworkSubsystem` 的端口访问接口读写自己的端口：
  `GetPort / FindPortByTransport / GetPortEther / SetPort / ClearPort / MintFromEnergy` 等。
- **已实现的 core**：
  - `Transporter`（传输器）：无状态；`OnInit` 把所有端口设成 `Any`（由内部布线结构定向），不跑 `Tick`；
  - `Facility`（配方设施）：扣 `RecipeInputs` → 产 `RecipeOutput`；**空配方 = 根产源**（矿机/发电机：不扣料，仅受输出端口吞吐限制）；
  - `Sink`（汇）：吃掉输入；
  - `Latch`（记忆单元）：写入 = 入端口出现"非 0 号"料 ⇒ 消耗掉它、覆盖**编号**（信息不守恒）；读取 = 用 `0 号` 能量重铸编号（`1 份 = abs(编号)` 份能量；不足 ⇒ 不产出、不消耗、不累积）；
  - `Clock`（时钟）：自驱动记忆单元 —— 每 `HalfPeriodTicks` 翻转一次编号，每 tick 用能量重铸出去；
  - `Counter`（计数器）：入端口每来 1 份"非 0 号"料 = 1 个脉冲（消耗掉；`0 号`不计数），累计满 `Threshold` ⇒ 用能量重铸 1 份 `OutputSignalType`；
  - `Register`（寄存器，≤8 位）：并行加载 + 用能量重铸读取（约定见 §3.4）；
  - `Annihilator`（湮灭发电机）：`+X` + `−X` ⇒ `abs(X)` 份 `0 号`（能量的唯一来源）。

### 1.3 建筑（容器）
- **配置** `UAsceEtherBuildingConfig`（共享）：`DeviceType`（组合/时序）、`ProcessCoreClass`、`SizeX/SizeY`、`SupportedDimCount`、`PortSlots`、`DefaultInternalLinks`。
- **实例**（Subsystem）：`FNode`（身份/配置引用/端口区间/内部布线/`CachedDirection`）+ 端口实例数组。
- 标记 fragment `FEtherBuildingFragment { FName BuildingDefId; }`（注册/注销锚点 + 身份）；配置由 `BuildingDefId -> TSubclassOf<UAsceEtherBuildingConfig>` 注册表反查。

---

## 2. 核心模型与约束

1. **Ether 不凭空产生**：端口有缓冲（`Ether`），上限 = 端口吞吐；无可用输入 ⇒ 本 tick 不输出。
   - **注**：线身份 = `(格, 方向, 维度)` ⇒ **同一格同一方向只有 8 条线**；位宽上限 = `格子数 × 8`（同方向），
     所以单格寄存器最多 8 位，16 位靠 2 格（或玩家拼两个寄存器）。
2. **一线一维度一族**：一条线只承载**一族** `SignalType`（`+X`/`−X` 同族）。
3. **端口有类型**（端口类型表的行）：决定 `Op`、**每 tick 吞吐**、**发射距离**；端口的 `PortTypeId`（行名）**玩家可换**（换完标脏重建）。
4. **外部连接（定向只发生在这里）**：端口沿 `Direction` **正对**找**第一个建筑**（挡住不穿透）；
   **射线上限 = 发射端端口的 `LinkRangeCells`（只看发射端；`0 = 不能连线`；接收端不需要距离）**；
   命中格必须与对端端口所在格一致（多格建筑同一 `(方向,维度)` 可能有多个端口）；
   反方向、同维度、且对端**"收"** ⇒ 建立 `Edges`（`From` = 本端口，**1:1**）。
   - **谁能"发"/"收"**：`Output` = 只发；`Input` = 只收；`Any` = **由内部布线结构推**（有 `LinkTargets` 则收、否则发 —— 传输器就是 `Any`）。
   - 大建筑端口相距远时用**传输器接力**；外部 **1:1**。
5. **内部布线**：默认来自 `DefaultInternalLinks`（建筑内端口下标；**实例数据、玩家可改**，改完标脏）；任意端口间（含同方向）；**不能自连**。
6. **组合 / 时序**：
   - 组合（组合建筑/传输器）：**0 延迟**，tick 内 settle；
   - 时序（器件）：**每级 1 tick**；
   - **组合子图（去掉器件后）必须无环**；有环 ⇒ 报错，且**环上端口拒绝参与运算**。
7. **取值语义（Empty ≠ 0，绝不凭空）**：
   - **无输入 ⇒ 不输出**；逻辑 0 = **互补 ether（负号态）**；
   - **份数不吞不吐**：`输出份数 = 输入可用份数`（上限 = 输出端口吞吐）；**符号由 op 决定且统一**：
     `Wire` 直通；`Not` = 推出时取互补态；`And` = 全 `+` 才出 `+`，否则 `−`；`Or` = 有 `+` 则 `+`，否则 `−`。
     - `And/Or` 具体实现：**入边求值** —— 上游推入时按**入边**记录本拍送来的符号（空入边 = 未定值，不计入；一条都没有 ⇒ 不输出）；
       求值只**统一符号**、**份数照收**（`And/Or` 目标**不做湮灭**）。
     - 例：`And` 两路入 `+X, +X` ⇒ 出 `2 × +X`；入 `+X, −X` ⇒ 把那 2 份统一成 `−X` 出。
8. **合流（多条入边 → 一出）**：
   - **判定依据是端口结构**：目标端口有 **≥2 条入边**时应用 **1:1 湮灭**；只有 **1 条入边**的普通节点则**符号直接翻转**（清空残留、采用来料）。
   - **异族 → 堵死**：本 tick 不消费、不输出（纯当 tick 判定，不存状态）。
   - `And/Or` 目标**不湮灭**（它要按入边看符号）。
9. **分流（一入 → 多出）**：`SplitMode`（端口实例属性，**玩家可改**，默认 `Ordered`）
   - `Ordered`：按扫描序**灌满第一个再下一个**；
   - `Balanced`（平均器）：在"剩余可接收目标"间按 `ceil(剩余 / 剩余目标数)` 均分（余数靠前多分；收不下的份额自动顺延）；
   - **扫描序**：以源端口为基准、从它**左手边**起顺时针，同方向内 `Dim` 低→高；
     例：`Dir=Left → 下→左→上→右`；`Up → 左→上→右→下`；`Right → 上→右→下→左`；`Down → 右→下→左→上`。
   - 候选 = 后继（`LinkTargets`）；该顺序同时决定**背压优先级**。
10. **背压**：目标满 → 料留上游端口；有料段保持活跃直到排空。
11. **服务器权威**；客户端只表现；操作走 RPC。
12. **0 号 ether（能量）**：
    - 由**湮灭发电机**产出；记忆/时钟/计数器**读取时**消耗（`1 份产出 = abs(编号)` 份 0 号）；
    - **逻辑透明**、**同规则走线**（与非 0 混入合流点同样**堵死**）；
    - **守恒闭环**："信息"（记忆里的编号）不守恒；**任何物质产出都必须由能量买单**。

---

## 3. 数据结构

### 3.1 配置层（共享：蓝图子类 CDO 当数据，不实例化）

```cpp
// ── 端口类型表（数据表；全局查表系统只查 MDT） ──
USTRUCT(BlueprintType) struct FAsceEtherPortTypeRow : public FTableRowBase   // 行名 = 端口类型 id
{
    EAsceEtherPortOp Op = Wire;        // 组合 op
    int32 ThroughputPerTick = 1;       // 轴 1：每 tick 吞吐 = 缓冲上限
    int32 LinkRangeCells = 256;        // 轴 2：发射距离（只看发射端；0 = 不能连线）
};
// MDT 在 UAsceDataTableSettings::AsceEtherPortTypeRow 指定（变量名给人看，运行时按行结构类型解析）；
// SDT 由 UGameFeatureAction_AddDataTableSources 挂/卸；C++ 查表：
//   FAsceEtherPortTypeRow Row;
//   UAsceDataTableSubsystem::GetDefinition<FAsceEtherPortTypeRow>(TEXT("BulkPort"), Row);

USTRUCT(BlueprintType)
struct FAsceEtherPortSlot                             // 建筑的"默认端口"声明
{
    uint8 Direction = 0;                              // AsceEther::EDir（建筑默认朝向）
    uint8 Offset = 0;                                 // 该方向边上、从左手侧起顺时针第几格
    uint8 Dimension = 0;                              // 占用的维度（< SupportedDimCount）
    FName PortTypeId;                                 // 端口类型行名（玩家可换）
    EAsceEtherSplitMode SplitMode = Ordered;          // 分流策略默认值（玩家可改）
};

USTRUCT(BlueprintType)
struct FAsceEtherLinkDef { int32 FromPort = 0; int32 ToPort = 0; };   // 内部布线（建筑内端口下标；不允许自连）

UCLASS(Abstract, BlueprintType, Blueprintable)
class UAsceEtherBuildingConfig : public UObject       // 建筑布局
{
    EAsceEtherDeviceType DeviceType = Combinational;  // 组合（传输器）/ 时序（器件）
    TSubclassOf<UAsceEtherProcessCore> ProcessCoreClass;
    int32 SizeX = 1, SizeY = 1;
    uint8 SupportedDimCount = 1;
    TArray<FAsceEtherPortSlot> PortSlots;             // 端口槽位（数量不写死 = 端口数）
    TArray<FAsceEtherLinkDef> DefaultInternalLinks;   // 内部布线默认值（实例可改）
};
```

### 3.2 行为层（`UAsceEtherProcessCore`）

```cpp
UCLASS(Abstract, BlueprintType, Blueprintable)
class UAsceEtherProcessCore : public UObject
{
    bool bInstance = false;                           // true = 每建筑一个实例（可带状态）
    virtual void OnInit(UAsceEtherNetworkSubsystem& Net, int32 NodeId);  // 设端口默认角色（不重置状态！）
    virtual void Tick  (UAsceEtherNetworkSubsystem& Net, int32 NodeId);  // 时序器件每 tick
    virtual FString GetDebugString() const;                              // 调试显示（可选）
};

// 具体 core（参数都是自己的 UPROPERTY；状态是成员变量）
UAsceEtherTransporterCore : bInstance=false                        // OnInit：所有端口 = Any
UAsceEtherFacilityCore    : bInstance=false, RecipeInputs/RecipeOutput
UAsceEtherSinkCore        : bInstance=false
UAsceEtherLatchCore       : bInstance=true,  StoredType / bHasStored
UAsceEtherClockCore       : bInstance=true,  OutputSignalType / HalfPeriodTicks / CurrentType / Phase
UAsceEtherCounterCore     : bInstance=true,  OutputSignalType / Threshold / Progress
UAsceEtherRegisterCore    : bInstance=true,  BitCount / StoredBits[8]（非反射） / bHasStored
UAsceEtherAnnihilatorCore : bInstance=false
```

**端口约定**（core 依赖 slot 顺序）：

| core | slot 顺序约定 |
|---|---|
| `Transporter` | 全部 `Any`（建图按内部布线结构定向，见 §2.4） |
| `Facility` / `Sink` / `Latch` / `Clock` / `Counter` / `Annihilator` | `slot0 = 输出`，其余 = 输入 |
| `Register` | `[数据入 ×BitCount, 数据出 ×BitCount, enable]`；enable = `Any`，写使能 = enable 收到"正 ether" |

### 3.3 实例层（Subsystem）

```cpp
// 建筑实例（nodeId 身份）
struct FNode
{
    FMassEntityHandle Entity;
    FName BuildingId;
    bool  bAlive;
    UAsceEtherBuildingConfig* Config;       // CDO（注册表 UPROPERTY 保活）
    bool  bDevice;                          // DeviceType == Sequential
    uint8 CachedDirection;                  // 建筑旋转（预留：暂恒 0）
    TArray<int32> SlotPortIds;              // slot 序号 -> 全局 PortId（**持久化：按相对几何身份复用**）
    TArray<FAsceEtherLinkDef> InternalLinks;// 实例内布线（玩家可改）
};

// 端口实例（**持久存在**的全局扁平数组 + 空闲池；空闲槽位 bActive = false）
struct FAsceEtherPortData
{
    // 端口类型（建图/换类型时查表解析成缓存）
    FName PortTypeId;  EAsceEtherPortOp Op;  uint16 Throughput;  int32 LinkRange;
    // 实例配置
    EAsceEtherSplitMode SplitMode;  EAsceEtherTransportType TransportType;  uint8 Dimension, Direction, Offset;
    // 运行态
    int32 NodeId, CellX, CellY;  bool bActive;  uint16 Ether;  int16 SignalType;
    // 图（外部边 1:1 ⇒ 直接存对端，不再需要全局边表；反向索引用于 O(1) 撤销/重扫）
    TArray<int32> LinkTargets;  int32 OutTargetPort;  int32 InSourcePort;
    TArray<int32> InSources;  TArray<int16> InSigns;  TArray<uint32> InSignTick;   // And/Or 入边符号记录
};

// 组合段（段池；段自带拓扑序）
struct FAsceEtherSegment { TArray<int32> Order; uint32 Stamp; bool bActive; };

// Subsystem 其它成员
UPROPERTY(Transient) TMap<FName, TObjectPtr<UAsceEtherBuildingConfig>> BuildingConfigRegistry;  // 保活
TArray<FAsceEtherPortData> Ports; TArray<int32> FreePortIds;          // 端口池
TArray<FAsceEtherSegment> Segments; TArray<int32> FreeSegIds;         // 段池
TArray<int32> PortSegId;                       // portId -> segId
TArray<TArray<int32>> DeviceAdjSegs;           // 器件 node -> 相邻段
TArray<int32> DeviceNodes;                     // 器件 nodeId（升序；Pass 1/2 只遍历它）
TArray<int32> GridToNode; int32 GridW, GridH;  // **扁平占用网格**（cell -> nodeId；射线 O(1) 查格）
TArray<int32> ActiveList; TArray<TArray<int32>> ActiveShards;          // 活跃段（分片）
uint32 CurrentStamp; TArray<FInstancedStruct> GraphCommands;           // 命令队列
UPROPERTY(Transient) TArray<TObjectPtr<UAsceEtherProcessCore>> NodeCores;   // 按 nodeId（CDO 或实例）
```

**对外接口（core / 玩家逻辑用）**：`GetPort / GetNodePortId / FindPortByTransport / GetPortEther / GetPortSignalType / SetPort / ClearPort /
MintFromEnergy / GetInternalLinks / SetInternalLinks / GetProcessCore / SetBuildingDirty`；结构性修改一律走 `PushAddNode / PushRemoveNode /
PushPatchNode / PushSetNodeLinks / PushPatchPort / PushRefreshData / PushFullRebuild`；调试用 `GetPortView / GetPortLinkTargets / GetPortNum / GetNodeNum`。

### 3.4 Fragment

```cpp
USTRUCT() struct FEtherBuildingFragment : public FMassFragment { FName BuildingDefId; };  // 只有标记
```
- 端口/布线/在制品在 **Subsystem**；器件状态在 **core 实例**；**fragment 不再承载任何状态**。

---

## 4. 连接构建（**命令驱动 + 局部更新**，服务器）

> 结构性修改（建筑增删 / 改配置 / 改布线 / 换端口类型 / 表变化）都以**命令**入队（见 §3.3 / §5.0），tick 开头统一应用 ⇒ **本 tick 内图最新且冻结**。

**两条路径**：

- **A. 局部更新（常态）**：只动受影响的建筑/端口/段 —— 端口**持久化**（按相对几何身份复用，id 不漂、在制品不动）；
  内布线只重建该建筑（`RebuildNodeInternalTopology`）；外部边只重扫**受影响的线路**（`RescanExternalEdgesForNode`：本建筑的出边 + 沿每个占用格反方向找到的"最近建筑"上朝本建筑发的那些端口）；
  段只重算"受影响端口所在旧段 ∪ 受影响端口"范围内的连通分量与拓扑序（`RecomputeSegmentsForPorts`，切段/合段自然处理）；
  最后把"仍有料"的段重新入列（`RefreshInitialActiveSegments`）。
- **B. 全量重建（兜底/批量）**：`RebuildGraph()` —— 保存/恢复不再需要（端口持久化），流程为：
  1. `EnsureGridIndex()`（扁平占用网格，尺寸取地图）；
  2. **端口实例**：`BuildNodePorts`（slot → 端口池，按相对几何身份复用；换建筑类型才重建整组端口）；
  3. **内部布线 → `LinkTargets`**（校验不自连/不越界；按 §2.9 扫描序排序）；
  4. **core 创建/复用 + `OnInit`**（`EnsureNodeCore`；设定各端口 `TransportType`，这是"定向"的依据）；
  5. **定向 + 外部边**：`RayScanPort`（等效发的端口 → 正对第一个建筑 → 命中格 + 反方向 + 同维度 + 对端"等效收"）⇒ 写 `OutTargetPort/InSourcePort`；
  6. **入边源表 + And/Or 记录数组**；
  7. **分段（端口级）**：组合端口求连通分量 = 段；段内 Kahn 拓扑序 → `Seg.Order`；**有环 ⇒ 报错，环上端口不参与运算**；
  8. **器件相邻段** + **初始活跃段**。

> 局部路径的正确性由开发期开关 `bValidateIncremental` 兜住：局部更新后跑一次全量重建并 diff 摘要（端口出/入目标、内部目标、段与器件邻接的端口集合），不一致就报错并以全量结果为准。
9. **映射**：`PortSegId`、`DeviceAdjSegs`（器件相邻段）。
10. **初始活跃段**：重建后仍**有残留**的段直接进活跃表（不依赖器件先采样）。

> 现状：整图重建（简单）；后续可做脏区域增量。

---

## 5. 执行模型

- **组合 = 0 延迟**：单 tick 内沿端口段传播到位（红石式）。
- **器件 = 1 tick**：`Core->Tick` 里"读入端口 → 更新状态 → 写自己的端口缓冲"。
- **tick 分三遍**（前面还有一步"命令 drain"）：
  0. **命令 drain（主线程，最前面）**：结构性修改**只能**通过命令队列进来，否则运行态（在制品）会被结构改动撕裂。
     命令集是**设施无关的最小集**（新增设施/器件都不加命令）：`AddNode` / `RemoveNode` / `PatchNode`（换类型、移动旋转）/ `SetNodeLinks` / `PatchPort`（端口实例字段补丁，掩码）/ `RefreshData`（表内容变化）/ `FullRebuild`（兜底、批量）。
     键用 `FMassEntityHandle`（对外身份）；端口身份用 `FAsceEtherSlotKey`（**相对几何** `Direction/Offset/Dimension`，移动/旋转/重排不改身份）。
     应用完 ⇒ **本 tick 内图最新且冻结**（这也是三个 pass 能并行、且 core 只碰运行态的前提）。
  1. **Pass 1（**并行**：按器件）**：全体器件 `Core->Tick`（只写自己的端口缓冲，不推）；有料则把相邻段**标进线程本地分片**；
  2. **Pass 2（**并行**：按器件）**：全体器件把自己的端口**推出去**（含上拍残留）⇒ 保证"器件 → 器件"外部边也是 **1 tick/级**，与 nodeId 顺序无关；随后主线程 `MergeActiveShards()` 合并分片；
  3. **Pass 3（**并行**：按段）**：各活跃段内按 `Seg.Order`（拓扑序）处理端口（`EvaluateOp` → 沿后继推）；并行结果（是否还有料）记到 `Survived[]`，主线程**串行收尾**：存活段留到下一拍（`Stamp = CurrentStamp + 1`），其余移除。

**增量的落地（P1）**
- **端口持久化 + 身份复用**：端口对象常驻（端口池 + `bActive`），重建/改动都按**相对几何身份**复用 ⇒ id 稳定、在制品原地不动（不再需要"保存/恢复"）；
- **外部边不建表**：1:1 ⇒ 端口上直接存 `OutTargetPort/InSourcePort`，撤销/重扫 O(1)；
- **段池 + 段自带拓扑序**：`FAsceEtherSegment.Order`（丢掉全局 `ProcessOrder`）；
- **局部更新**：`SetNodeLinks / PatchPort / AddNode / RemoveNode` 已走局部路径（`RebuildNodeInternalTopology` + `RescanExternalEdgesForNode` + `RecomputeSegmentsForPorts` + `RefreshInitialActiveSegments`）；只有 `PatchNode`（摆放来自建筑系统的 footprint）暂落 `FullRebuild`；
- **校验**：`bValidateIncremental`（开发期）——局部更新后跑全量重建并 diff 摘要，不一致报错且以全量结果兜底。

**并行的前提（必须守住）**
- 活跃表：段 `Stamp` 用**原子交换**去重（`InterlockedExchange`）；激活写**线程本地分片**，主线程合并 ⇒ 不竞争；
- **core 并行契约**：`Tick` 会被并行调用 ⇒ `bInstance == false` 的 core（共享 CDO）**禁止写成员/共享状态**，只能碰"自己建筑的端口"；要状态就 `bInstance = true`；
- **建图/增量断言**：① 一个端口**最多一条外部入边**（射线"遇第一个建筑就停"+ 目标朝向固定）；② 内部布线**两端同建筑** ⇒ 同目标的多次推入必在同一线程（Pass 2 同一器件 / Pass 3 同一段）内**按序**发生（湮灭、`And/Or` 符号记录都依赖顺序）。
- **推流不看端口类型**：端口只沿 `LinkTargets + OutTargetPort` 推；`Input` 端口在定向时就没建出边，所以不会"推回原处"。
- **段**：端口级连通分量；段间由器件隔开 ⇒ 互不相连；段内串行、段间可并行。
- **活跃表（持久 + epoch 去重）**：器件有料 → 激活相邻段；段处理末尾判空：有料 ⇒ 留活跃（`Stamp = CurrentStamp+1`），排空 ⇒ 移除；空闲段不跑。

---

## 6. Tick 流水线（服务器，0.1s 固定步）

1. `CurrentStamp++` ⇒ 命令队列非空则 `ApplyGraphCommands()`（结构性修改的唯一入口；全量兜底/局部更新见 §4）⇒ 本 tick 内图**最新且冻结**。
2. Pass 1：器件 `Core->Tick`（配方/锁存/计数/寄存器…按自己的逻辑读写端口）→ 有料则激活 `DeviceAdjSegs`（写线程本地分片）。
3. Pass 2：器件端口推出（`PushPort`）。
4. Pass 3：活跃段 `ProcessPort`（`EvaluateOp` 处理 `And/Or` 符号 → `PushPort` 分流/外部推送 → 判空）。
5. 表现/复制：同步状态给客户端（待做）。

---

## 7. 规则速查

- **配置/实例/行为三分**：型号与建筑配置是**共享蓝图 CDO**；端口实例/布线在 **Subsystem**；功能与状态在 **core**。
- **端口类型只在建图用一次**：`Output` 只发、`Input` 只收、`Any` 由内部布线结构推；tick 推流不看。
- **取值**：Empty ≠ 0；无输入 ⇒ 不输出；逻辑 0 = 负号态；`Not` = 推出时取互补态。
- **扫描序**：源端口左手边起顺时针；同方向内 `Dim` 低→高（§2.9）。
- **合流**：≥2 入边且同族异号 → 1:1 湮灭；单入边 → 符号直接翻转；异族 → 堵死；`And/Or` 目标不湮灭。
- **分流**：`Ordered` / `Balanced`（余数靠前多分 + 份额顺延）。
- 外部：正对第一个建筑、命中格匹配、反方向同维度、1:1、挡住不穿透；**发射距离 = 源端口型号的 `LinkRangeCells`（0 = 不能连）**。
- 内部布线：任意端口间（含同方向）、不自连、实例可改。
- 背压：目标满则留上游；有料段保持活跃。
- 环：允许，但**去器件后组合子图必须无环**（有环 → 报错 + 环上端口拒绝运算）。
- 服务器权威；客户端只表现；RPC。

---

## 8. 迁移 / 进度

**结构对照（v2 → v3）**

| 项 | v2 | v3 |
|---|---|---|
| 端口"类型" | `UAsceEtherPortType`（DataAsset，只有吞吐） | 数据表行 `FAsceEtherPortTypeRow`（`Op` + 吞吐 + 发射距离） |
| 建筑配置 | `UAsceEtherBuildingData`（DataAsset） | `UAsceEtherBuildingConfig`（蓝图类：布局 + slots） |
| 端口摆放 | `FAsceEtherPortDef`（格偏移 + 方向 + 维度） | `FAsceEtherPortSlot`（方向 + 边上第几格 + 维度 + 型号） |
| 器件行为 | `EAsceEtherDeviceOp` + Subsystem 里的 switch | `UAsceEtherProcessCore` 子类（`OnInit/Tick`） |
| 器件状态 | `FEtherDeviceStateFragment` | **core 实例成员**（`bInstance = true`） |
| 入/出 | 端口 `bIsInput` / 器件角色表 | `TransportType`（core 设定，仅建图定向用） |
| 内部布线 | `InternalLinks`（配置） | `DefaultInternalLinks`（默认）+ 实例可改 |
| 端口运行态 | Subsystem 全局扁平数组 | 同（按 node 分段），字段改名 `FAsceEtherPortData` |

**迁移进度**

1. ✅ **步骤①~⑤（v2 语义与图）**：端口级图 + 变长端口/布线；`int16` 互补态 + `Empty ≠ 0`；`Op`（`Wire/Not/And/Or`）；
   合流湮灭/堵死（单入边翻转）+ 分流 `SplitMode` + 相对扫描序；器件 op（Facility/Sink/Latch/Clock/Counter/Register/Annihilator）+ 能量闭环（0 号铸币）。
2. ✅ **步骤⑥（本次，v3 架构）**：Config（共享蓝图类）/ Runtime（Subsystem 实例）/ Core（行为 + 状态）三层拆分；
   建筑用 **slot** 声明端口；`TransportType` 取代 `bIsInput` 与角色表；8 个 core 取代 `DeviceOp` switch；
   删除 `UAsceEtherBuildingData / UAsceEtherPortType / FAsceEtherPortDef / EAsceEtherDeviceOp / FEtherDeviceStateFragment`。
   - ⚠️ **资产侧需要重建**：`UAsceEtherBuildingConfig` 的蓝图子类（`ProcessCoreClass` 选对应 core、按 §3.2 摆 slot）；**端口类型改成数据表**（建 `DT_EtherPortTypes` MDT + 各 GameFeature 的 SDT，见步骤⑦）。
3. ✅ **步骤⑦（本次）**：**全局查表系统**（`AsceMass/Data/`）—— `UAsceDataTableSettings`（别名 = 变量名 → MDT）+ `UAsceDataTableSubsystem`（GameInstance 子系统；按**行结构类型**建 `RowStruct → MDT` 映射；`GetDefinition<T>/GetDefinitions<T>/GetDefinitionMap<T>` 静态模板；蓝图 `BP_GetMasterTable` + `CustomThunk` 泛型 `GetDefinitionByStruct`；`AddSourceTable/RemoveSourceTable` 挂卸 SDT）+ `UGameFeatureAction_AddDataTableSources`（GameFeature 激活挂、卸载摘；`AssetBundles=Server`；`IsDataValid` 校验）。
   **端口类型表** `FAsceEtherPortTypeRow` 是第一个使用者；`UAsceEtherPortConfig` 蓝图类已删除，slot/端口实例改用 `FName PortTypeId`。
   - ⏳ 待做：**脚本层(AS)接入**（模板/`CustomThunk` 脚本都用不了 ⇒ 需要每表手写的具体类型 `UFUNCTION`，等环境完整再做）；表内容热更后不主动重建图（任何结构变化会自愈）。
4. ✅ **步骤⑧（本次）**：**三个 pass 并行**（`ParallelFor`；数量 < 8 退回串行）—— 基础设施：段 `Stamp` 原子交换去重 + 活跃段**线程本地分片**（主线程 `MergeActiveShards` 合并）+ `DeviceNodes` 缓存（Pass 1/2 只遍历器件）+ 建图期并行前提断言（外部入边 ≤1 / 内布线同建筑）+ core 并行契约（§5 已同步）。
5. ✅ **步骤⑨（本次）**：**结构性修改改走命令队列**（`AsceEtherGraphCommand.h`：7 条设施无关命令，`FInstancedStruct` 队列），tick 开头 drain 应用；注册表改 `UPROPERTY TObjectPtr`（删掉 keepalive）、实例快照 key 改成**端口相对几何身份**、`LinkTargets` 只在 >1 时排序。未局部化的命令（`PatchNode` 摆放、`PatchPort` 部分字段）暂落 `FullRebuild` 兜底。
   - ⏳ 下一步（P1）：把 `PatchPort / SetNodeLinks / RemoveNode` 等逐条改成**局部应用**（需先补：稳定端口块/free-id 池、反向索引、扁平格索引），并用"增量结果 vs 全量重建 diff"做开发期校验。
6. ✅ **步骤⑩（本次，P1 起步）**：
   - **端口持久化 + 身份稳定**：端口池（`AllocPortId/FreePortId` + `bActive`）、`FNode.SlotPortIds`、按**相对几何身份**原地复用（id 不漂、在制品不动）⇒ 删掉重建前的快照/恢复；
   - **外部边去表**：`Edges` 全局数组 → 端口上的 `OutTargetPort`/`InSourcePort`（1:1 直接存对端 + 反向索引，撤销/重扫 O(1)）；
   - **段池 + 段自带拓扑序**：`FAsceEtherSegment{ TArray<int32> Order; Stamp; bActive }`（丢掉全局 `ProcessOrder`）；
   - **局部更新**（已接入）：`RecomputeSegmentsForPorts`（只在"受影响旧段 ∪ 受影响端口"范围内重跑连通分量 + 拓扑序，合并/拆分自然处理）、`RebuildNodeInternalTopology`、`RecomputeDeviceAdjSegsForNodes`、`RefreshInitialActiveSegments`；`SetNodeLinks` 与 `PatchPort`(分流/类型未改射程) 已走**局部**路径，其余暂落 `FullRebuild`。
   - ⏳ 待做：射线"按线路重扫"（`AddNode/RemoveNode/改维度/改射程/移动旋转`要局部化）、扁平 `cell → node` 索引、`bValidateIncremental`（增量 vs 全量 diff 校验）。
7. ✅ **步骤⑪（本次，P1 完成）**：
   - **扁平占用网格**（`GridToNode/GridW/GridH`，射线 O(1) 查格；`MarkNodeCells` 局部增删）；
   - **按线路重扫**（`RayScanPort` + `RescanExternalEdgesForNode`：本建筑出边 + 沿占用格反方向找到的"最近建筑"上朝本建筑发的端口）；
   - **命令全部局部化**：`AddNode` / `RemoveNode`（先清占用格→重扫→带旧段范围重算段→再回收）/ `SetNodeLinks` / `PatchPort`（含维度/射程变化）都走局部路径；只剩 `PatchNode`（摆放）落兜底；
   - **开发期校验**：`bValidateIncremental` + `BuildGraphDigest`（局部 vs 全量 diff，按端口/段端口集合/器件邻接签名比较），不一致报错并以全量兜底。
   - ⏳ 仍待做：`PatchNode`（摆放，依赖建筑系统的 footprint 改动）、建筑旋转（`CachedDirection`）、`RefreshData` 的精准失效（目前整表刷新走全量）、局部更新的单元/回归测试。

---

## 9. 未决 / 待办

- **建筑旋转**：`CachedDirection` 目前恒 0；`SlotToCell` 与"反方向匹配"都已留好位置，接上 entity 旋转即可（slot 的 `Offset` 已按"顺时针第几格"定义，天然随转）。
- **玩家改端口**：换型号 / 改维度 / 改分流 / 改内部布线的 UI 与 RPC（接口已就绪：改完 `SetBuildingDirty`）。
- **客户端同步**：需要同步哪些字段（端口在制品、core 状态）+ RPC 清单。
- **增量重建**（脏区域）取代整图重建。
- **更多 core**：移位寄存器/比较器（比较"编号核数"？寄存器值？需定值语义）、能量计。
- **能量不足提示**：现在静默不产出，UI 层要给"需要更快的能量线"提示。
- **根产源（空配方 Facility）的门槛/成本**：目前自由产出，后续是否加资源/建造限制。
- **`And/Or` 细节**：入边数为 0/部分为空时的判定已定（未定值不计入），是否要"缺边即假"另说。
- **小整**：`EEtherPortOp` 是否要加 `Xor`/`Nand` 等；端口 `ThroughputPerTick` 上限（目前 65535）。
