# Ether 系统设计（v2）

> 建筑网络中的"电信号/物质"传播系统。目标：物流（材料传输）+ 逻辑（组合门、时序器件、反馈），
> 借鉴数字电路（组合/时序、时钟沿）与红石（tick、反馈）。
>
> 本文是**目标架构**；`§9` 列出与当前代码（v1）的差异与迁移项。
>
> **v2.1 变更**：`SignalType` 改**有符号**（最高位 = 互补标志）；明确 **Empty ≠ 逻辑 0**、**无输入不输出**；门采用**份数不吞不吐 + 符号由 op 决定**；合流 = 同族异号**湮灭** / 异族**堵死**；**0 号 ether = 能量**（湮灭发电机产出，供时序器件）；分流引入 `SplitMode{Ordered,Balanced}` + **相对扫描序**。

---

## 0. 术语

| 术语 | 含义 |
|---|---|
| Ether | 网络里传输的"物质/电信号"；**不凭空产生**（无输入 ⇒ 不输出）；门可消耗输入产出结果（网络级守恒）。 |
| SignalType | 物质类型 id（**`int16`，有符号**）。最高位 = **互补标志**：`Complement(X) = X ^ 0x8000`；`+1 = 通用（电力/能源）`。 |
| Empty | **空**（无 ether）= "未定值"，**不是逻辑 0**；不激活任何东西、不产生任何东西。逻辑 0 必须由**互补 ether（负号态）**表示。 |
| 一族 family | 同一 `abs(SignalType)` 的正/互补两态（`+X` / `−X`）。一条线只承载一族；**异族 = 堵死**（§2.8）。 |
| 湮灭 | `+X` 与 `−X` 在合流点 **1:1 抵消**（多出者按符号留下；全抵 ⇒ 本 tick 输出空）。默认**纯消失**；送入**湮灭发电机**器件则转为 **0 号 ether（能量）**。 |
| 0 号 ether | `SignalType == 0` = **能量**（族 0），与普通 ether **同规则走线**；对逻辑**透明**（`And/Or` 不参与判定、原样透传，`Not` 后仍为 0）；供时序器件作能量输入；湮灭产物，数量 = `abs(X)`（**核数**）。 |
| 维度 Dim | 逻辑分层（实际是高度层）；同一格可有多条不同维度的线。 |
| 方向 Dir | `Left / Up / Right / Down`；**扫描序** = 以源 port 为基准、从它**左手边**起顺时针（§2.9）。 |
| **端口 Port** | **组合节点**：`(建筑, portIndex)`；数量由建筑配置决定（**不写死**）。 |
| **器件 Device** | **时序单元**（建筑级）：有明确的输入/输出端口，拍边界采样。 |
| **建筑 Building** | 容器：传输器=纯端口；器件=端口+内部时序。 |

---

## 1. 三个基本概念

### 1.1 端口（组合节点）
- 端口 = `(建筑, portIndex)`，属性：`Dir, Dim, CellOffsetX/Y`（对外位置/朝向/维度）、
  `Op`（**组合 op**：`Wire`(默认直通/路由) / `And` / `Or` / `Not` / …）、缓冲 `Ether/SignalType`、吞吐。
- 端口的**输入 = 所有指向它的边**（内部布线 + 外部进边）；**输出 = 它的出边**（内部布线 + 外部出边）。
- 组合 op 按段内拓扑序**当拍求值**（0 延迟）。
- op 取值语义见 §2.7：**无输入 ⇒ 不输出**；`Not` = 类型翻转（`+X ↔ −X`）；逻辑 0 = **负号态**（不是 Empty）。
- **入/出不是端口的属性**——端口只是端点，谁是输入/输出由**器件**决定。

### 1.2 器件（时序单元）
- 有**端口角色表**（哪些端口是输入、哪些是输出）；器件从输入端口取、写到输出端口。
- **内部状态**（配方进度 / 锁存值 / 计数器…）放 **fragment**（器件之间无共享 → **可并行**处理）。
- **拍边界**：采样输入端口 → 更新内部状态 → 写输出端口（1 tick 延迟）。
- 是组合图的**破环点**（组合环必须由器件破开）。
- **已实现的 op**：
  - `Facility`：按 `RecipeInputs` 扣料 → 产 `RecipeOutput`；
  - `Sink`：吃掉输入；
  - `Latch`（记忆单元）：**写入** = 入端口出现"非 0 号"的料 ⇒ 消耗掉它、覆盖**编号**（信息，不守恒）；**读取** = 用 `0 号 ether` 重铸编号 ⇒ 产出（`1 份 = abs(编号)` 份能量；**能量不足 ⇒ 本拍不产出且不消耗能量**，不累积）；
  - `Clock`（时钟）：**自驱动的记忆单元** —— 每 `DeviceParamA` tick 翻转一次编号，每拍用能量把它重铸出来（半周期 = `DeviceParamA`）；
  - `Counter`（计数器）：入端口每来 1 份"非 0 号"料 = 1 个脉冲（消耗掉；`0 号`不计数），累计满 `DeviceParamA` ⇒ 用能量重铸 1 份 `DeviceParamSignal`；
  - `Annihilator`（湮灭发电机）：`+X` + `−X` ⇒ `abs(X)` 份 `0 号 ether`（能量的唯一来源）；
  - `Register`（寄存器，**≤8 位** = 单格的维度数；一个寄存器占一格，**多位由玩家拼多个寄存器**）：
    角色表 `InputPorts = [bit0..bitN-1, enable]`、`OutputPorts = [bit0..bitN-1]`；
    **使能** = 最后一个入端口收到"正 ether"（非 0 号、非互补态）；**写** = 并行加载（有料覆盖、没料清空该位，消耗来料）；
    **读** = 每拍把有值的位用能量重铸到对应输出口（成本 `Σ abs(编号)` 份/拍）；
  - 未做：移位寄存器、比较器等。

### 1.3 建筑（容器）
- **传输器**：纯端口 + 端口 op/布线，无内部时序 → 纯组合。
- **器件**：端口（边界）+ 内部时序状态。
- 标记 fragment `FEtherBuildingFragment { FName BuildingDefId; }`（注册用 + 身份）；配置从 `UAsceEtherBuildingData` 解析。

---

## 2. 核心模型与约束

1. **Ether 不凭空产生**：端口有缓冲（`Ether`），上限 = 端口吞吐；无可用输入 ⇒ 本 tick 不输出。
   - **注**：线的身份是 `(格, 方向, 维度)` ⇒ **同一格同一方向只有 8 条线（每维度一条）**；位宽上限 = `格子数 × 8`（同方向），
     所以寄存器单格最多 8 位，16 位要靠 2 格（或玩家拼两个寄存器）。
2. **一线一维度一族**：同 (格,方向,维度) 一条线；一条线只承载**一族** SignalType（`+X`/`−X` 同族）。
3. **端口有类型**（`UAsceEtherPortType`）决定**每 tick 吞吐**；端口可自由选类型。
4. **外部连接**：出端口沿方向**正对**找**第一个建筑**（挡住，不穿透）；对方若有 (反方向, 同维度, 被器件/布线定义为入) 的端口 → 连接。
   - **传输器的"入端口" = 内部布线的源端口**（它从外部收料、向内转发；被指向的目标端口才是对外输出端）。
   - 外部连接**必须落在射线命中的那一格**（多格建筑在同一 `(方向,维度)` 上可能有多个端口，否则会接错）。
   - 大建筑端口相距远时，**用传输器接力**（正对放一个，再逐步分到目标格）。
   - 外部**1:1**（一条边一个邻居）。
5. **内部布线**：端口之间可任意连（**含同方向**，已去掉"同方向不能连"的限制）；**不能自连**。
6. **组合 / 时序**：
   - 组合（端口 op / 传输器）：**0 延迟**，tick 内 settle（红石式）；
   - 时序（器件）：**每级 1 tick**；
   - **组合子图（去掉器件后）必须无环**；布线成环且无器件破环 → **拒绝**。
7. **取值语义（Empty ≠ 0，绝不凭空）**：
   - **无输入 ⇒ 不输出**：端口/节点没有可用输入时本 tick 什么都不产生（Empty = 未定值，**不是逻辑 0**）。
   - 逻辑 0 由**互补 ether（负号态）**表示；即参与运算时线上**总有信号**（`+X` 或 `−X`）。
   - **份数不吞不吐**：`输出份数 = 输入可用份数`（上限 = 输出端口吞吐）；**符号由 op 决定且统一**（不符的那份**翻转**成结果符号）。
     - `Wire` = 直通；`Not` = 全翻转（`±X → ∓X`）；`And` = 全 `+` ⇒ 出 `+`，否则 `−`；`Or` = 有 `+` ⇒ 出 `+`，否则 `−`。
       - **实现（步骤③）**：`And/Or` 由"**入边**"求值 —— 上游推入时按入边记录本拍送来的符号（空入边 = 未定值，不计入；一条都没有 ⇒ 不输出）；
         求值只统一符号、**份数照收**（`And/Or` 目标**不做湮灭**）。`Not` 是推出时取互补态（单入边）。
     - 例：`And` 两路入 `+X, +X` ⇒ 出 `2 × +X`；入 `+X, −X` ⇒ 把那份 `+X` 翻成 `−X` ⇒ 出 `2 × −X`。
   - **吞吐不足 = 背压**（多出的留上游），**但这不是逻辑问题**：新 `SignalType`（含符号变化）到来时**清空该端口残留并直接填入**，不排队 ⇒ 值变化即时响应。
8. **合流（多入 → 一出）**：
   - 同族异号（`+X` 与 `−X`）→ **1:1 湮灭**：默认**纯消失**（不产副产物）；多出的按符号留下，全抵 ⇒ 本 tick 输出空。
     - 送入**湮灭发电机**器件 ⇒ 产出 **0 号 ether（能量）**，数量 = `|X|`（**核数**，见 §2.12）。
   - **异族 → 直接堵死**：本 tick 不消费、不输出。作用域 = 该合流节点；**纯当 tick 判定、不存状态**（异族消失即恢复）。
   - 好处：不静默丢料；错误立刻可见（上游背压→生产停摆）；强制一线一族；顺带让 `And/Or` 的**输出族由输入自动推导**（合流点上必然只有一族）。
   - **注意**：`And/Or` 目标**不湮灭**（它要按入边看符号）；湮灭只发生在 `Wire/Not` 目标上。
9. **分流（一入 → 多出）**：`SplitMode`（分流节点属性，默认 `Ordered`）
   - `Ordered`：按扫描序**灌满第一个再下一个**（到该端口缓冲上限）。
   - `Balanced`（平均器）：在每个**可接收目标**上按 `ceil(剩余 / 剩余目标数)` 分配（等价于 `base + 余数按扫描序前多后少`）；
     **份额顺延**：某端口收不下（满/被堵死）⇒ 份额自然落到后面的目标（不浪费、也不留上游）。例：3 出 / 4 ether → `2,1,1`。
   - **扫描序**：以源 port 为基准、从它**左手边**起顺时针，同方向内 `Dim` 低→高。
     例：`Dir=Left → 下→左→上→右`；`Dir=Up → 左→上→右→下`；`Dir=Right → 上→右→下→左`；`Dir=Down → 右→下→左→上`。
   - 候选集 = 该节点要送出的**输出端口**（源端口自身排除，不自连）。
   - 扇出是**带宽问题**：源每 tick 产出 ≥ 消费者数 ⇒ 人人有份；否则扫描序靠前的先吃。
   - 该顺序同时决定**背压优先级**（靠前的先满先堵）。
10. **背压**：目标满 → ether 留在上游端口；有料段保持活跃直到排空。
11. **服务器权威**；客户端只表现；操作走 RPC。
12. **0 号 ether（能量）**：
    - `SignalType == 0` = **能量**（族 0），不是物质；由**湮灭发电机**器件产出（`+X` + `−X` ⇒ `abs(X)` 份 0 号）。
    - **逻辑透明**：不参与 `And/Or` 判定（不构成"假"，照常通过），`Not` 后仍是 0。
    - **时序器件**的运行成本：记忆单元**读取**时消耗（`1 份产出 = abs(编号)` 份 0 号；不足则本拍不产出、不累积）。
    - **走线同规则**（不设专用能量线）：`0` 就是**族 0**；与非 0 混入同一合流点 ⇒ 同样**堵死**（§2.8 通用规则，无需特例）。
    - **守恒闭环**："信息"（记忆里的编号）不守恒；**任何物质产出都必须由能量买单** ⇒ 物质总量仍守恒。

---

## 3. 数据结构

### 3.1 静态数据（`UAsceEtherBuildingData` : `UDataAsset`，每建筑类型一份）

```cpp
UCLASS() class UAsceEtherPortType : public UDataAsset
{
    FName  TypeId;
    uint16 ThroughputPerTick = 1;   // 该类型端口的每 tick 吞吐上限
};

USTRUCT() struct FAsceEtherPortDef
{
    int32 CellOffsetX, CellOffsetY;      // 相对建筑原点的格偏移
    uint8 Dir, Dim;                      // 方向 / 维度（Dim < SupportedDimCount）
    TObjectPtr<UAsceEtherPortType> Type; // 端口类型（决定每 tick 吞吐，见上）
    EAsceEtherPortOp Op;                 // 组合 op（Wire/And/Or/Not/…，默认 Wire）
    EAsceEtherSplitMode SplitMode = EAsceEtherSplitMode::Ordered; // 该端口作分流源时的分配策略
};

USTRUCT() struct FAsceEtherRecipeItem { int16 SignalType; uint16 AmountPerTick; }; // SignalType 有符号，最高位 = 互补标志

USTRUCT() struct FAsceEtherLinkDef { int32 FromPort, ToPort; }; // 内部布线的一项（同建筑端口下标；不允许自连）

UCLASS() class UAsceEtherBuildingData : public UDataAsset
{
    bool   bIsDevice;              // 传输器=false；器件=true
    EAsceEtherDeviceOp DeviceOp;   // 器件 op（Facility/Latch/Counter/…；bIsDevice 时有效）
    int32  SizeX, SizeY;
    uint8  SupportedDimCount = 1;

    TArray<FAsceEtherPortDef> Ports;          // 端口清单（数量不写死）；端口**不配置"家族"**（族由输入推导，§2.8）
    TArray<FAsceEtherLinkDef> InternalLinks;  // 内部布线：同建筑端口间（可合流/分流；不允许自连）
    TArray<int32> InputPorts;                 // 器件：输入端口下标（端口角色表）
    TArray<int32> OutputPorts;                // 器件：输出端口下标

    TArray<FAsceEtherRecipeItem> RecipeInputs; // Facility 用
    FAsceEtherRecipeItem         RecipeOutput;
};
```
> **v2 现状（步骤①已落地）**：`PortType` 决定吞吐；`PortDef` 带 `Op/SplitMode`；`InternalLinks` 定义传输器内部布线；
> 建筑的 `BuildingDefId -> UAsceEtherBuildingData` 由 Trait 在 `BuildTemplate` 时登记进 Subsystem 的注册表。
>
> **规模板（步骤②已落地）**：器件 = `bIsDevice` + `DeviceOp` + **`InputPorts/OutputPorts` 角色表**（端口自身不再带"入/出"）；
> 器件挂 `FEtherDeviceStateFragment`（内部状态骨架，Latch/Counter/Clock 用）；传输器的入/出仍由 `InternalLinks` 推导。
> 现有实现行为：`Facility`（扣配方产出）/ `Sink`（吃掉输入）；`Latch/Counter/Clock` 为占位（待步骤④）。

### 3.2 Fragment

```cpp
// 标记：实体是 ether 建筑（注册/注销锚点 + 身份）
USTRUCT() struct FEtherBuildingFragment : public FMassFragment { GENERATED_BODY() FName BuildingDefId; };

// 器件内部状态（只有器件挂；公共骨架，具体 op 再细化/拆分）
USTRUCT() struct FEtherDeviceStateFragment : public FMassFragment
{
    GENERATED_BODY()
    int16  StoredType;      // 记忆单元的编号（信息，非物质）
    bool   bHasStored;      // 是否写过
    int16  StoredBits[8];   // 寄存器位（编号；0 = 空槽；上限 8 = 单格维度数）
    uint16 Progress;        // 计数进度（Counter）/ 时钟相位（Clock）
    uint8  Phase;           // 状态机相位（备用）
};
```
- 器件状态放 fragment → **器件采样可并行**（器件之间不共享端口）。
- 端口缓冲/布线**不**放 fragment（放 Subsystem，见 3.3）。

### 3.3 Subsystem（`UAsceEtherNetworkSubsystem`，服务器）

**变长运行态 + 图 + 调度**（不写死端口数）：

```cpp
// 每建筑（FNode）
struct FNode
{
    FMassEntityHandle Entity;
    FName BuildingDefId;
    bool  bAlive;
    // 从 DA 缓存的配置（RebuildGraph 时解析）
    EAsceEtherDeviceOp DeviceOp;
    bool  bDevice;                          // 器件（时序）；传输器 = false（纯组合）
    int32 PortStart, PortCount;             // 全局 PortId 区间
    TArray<int32> DeviceInputPorts;         // 角色表（全局 PortId）
    TArray<int32> DeviceOutputPorts;
    TArray<FAsceEtherRecipeItem> RecipeInputs; FAsceEtherRecipeItem RecipeOutput;
};

// 端口运行态（扁平、变长）
struct FPort
{
    int32 NodeId;
    uint8 Dir, Dim; int32 CellX, CellY;
    uint16 Throughput;
    uint16 Ether; int16 SignalType = 0;     // **Empty ≡ Ether == 0**；SignalType 有符号（族/互补态），仅在 Ether > 0 时有意义
    EAsceEtherPortOp Op;                    // 组合 op
    EAsceEtherSplitMode SplitMode;          // 分流策略（默认 Ordered）
    TArray<int32> LinkTargets;              // 内部布线目标（分流用；构建期算好）
    int32 OutEdge;                          // 外部出边（1:1，最多一条）
};
TArray<FPort> Ports;                        // 全局端口数组（按 node 分段）

// 布线：变长边表（支持合流/分流，不受 32 限制）
struct FPortLink { int32 FromPortId, ToPortId; };
TArray<FPortLink> Links;

// 图（端口级）
struct FPortEdge { int32 FromPortId, ToPortId; };   // 外部 1:1
TArray<FPortEdge> Edges;

// 段 / 顺序 / 调度（均按端口）
TArray<int32> ProcessOrder;              // 组合端口，按段连续
TArray<FAsceEtherSegment> Segments;      // {Start, Count, Stamp}
TArray<int32> PortSegId;                 // portId -> segId
TArray<TArray<int32>> DeviceAdjSegs;     // 器件 node -> 相邻段
TArray<int32> ActiveList; uint32 CurrentStamp;
```

---

## 4. 连接构建（`RebuildGraph`，建筑增删/布线变化时，服务器）

1. **占用图**：`FAsceGridFootprintFragment` → `格 → node`。
2. **端口运行态**：从 DA 分配（数量/布局/op/分流策略），填 `FNode.PortStart/PortCount` + 全局 `Ports`。
   - **器件**：按 `InputPorts/OutputPorts` 角色表标记入/出（越界报警）；
   - **传输器**：端口先全部当"出"，入由第 3 步的内部布线回填。
3. **组合边**：
   - **内部布线**：`InternalLinks` → `Links`（同建筑端口间；校验：不自连/不越界；重复去重）；
   - **外部连接**：每个非入端口沿方向正对找第一个建筑 → 对方对应 (反方向,同维度) 的**入**端口 → 建 `Edges`（1:1）。
4. **分段（端口级）**：组合端口（非器件端口）用组合边求连通分量 = 段；段内 Kahn 拓扑序 → `ProcessOrder`；**组合环（无器件破环）→ 报错**（兜底仍接上）。
5. **映射**：`PortSegId`、`DeviceAdjSegs`（器件相邻段）。

> 现状：整图重建（简单）；后续可脏区域增量。

---

## 5. 执行模型

- **组合 = 0 延迟**：单 tick 内沿端口段传播到位（红石式）。
- **器件 = 1 tick**：拍边界采样（写入自己的输入/输出端口缓冲）。
  - 采样结束后**立即把自己输出端口的料推出去**（否则料会卡在器件端口里；推不掉的留到下一拍继续）。
  - 组合 → 器件：上游组合端口按 1:1 边推进器件入端口 ✔ 无需特殊处理。
- **段**：端口级连通分量；段间由器件隔开 → **互不相连**：
  - 段内按 `ProcessOrder` 串行；**段间可并行**。
- **器件采样相位**：按器件 fragment **并行**（器件互不共享端口）。
- **活跃表（持久 + epoch 去重）**：
  - 加入：①器件采样有变化 → `DeviceAdjSegs`；②段仍有残留（背压）。
  - `AddActive(seg)`：`if (Stamp != CurrentStamp) { Stamp = CurrentStamp; ActiveList.Add(seg); }`
  - 段处理末尾**判空**：有料 → 留活跃 + `Stamp = CurrentStamp+1`；排空 → 移除。
  - 空闲段不跑、不扫；背压段保持活跃。

---

## 6. Tick 流水线（服务器，0.1s 固定步）

1. `CurrentStamp++`。
2. **器件采样（可并行）**：按器件 fragment，遍历器件：从**输入端口**取 ether（按器件 op 逻辑：配方/锁存/计数），更新内部状态，写到**输出端口**；有变化 → `AddActive(DeviceAdjSegs)`。
3. **组合传播（集中，端口级段）**：遍历 `ActiveList`，段内按 `ProcessOrder` 处理每个端口：
   - 求值端口 op（默认 `Wire`）：合流按 §2.8（同族异号→湮灭、异族→堵死）、分流按 §2.9（`SplitMode` + 扫描序）；**无可用输入 ⇒ 不输出**（Empty 不是 0）；
   - 沿外部边推送（受目标容量/吞吐/族限制；推不下留上游 = 背压）；
   - 判空决定段去留。
4. **表现/复制**：同步状态给客户端。

---

## 7. 规则速查

- **取值**：Empty ≠ 0；无输入 ⇒ 不输出；逻辑 0 = **负号态**（互补 ether）；`Not` = 类型翻转。
- **扫描序**：以源 port 为基准、从它**左手边**起顺时针；同方向内 Dim 低→高（§2.9）。
- **合流**：同族异号 → 1:1 湮灭；异族 → **堵死**（当 tick 判定，不存状态）；`And/Or` 输出族由输入推导。
- **分流**：`Ordered` 先填满一个再下一个；`Balanced` 均分 + 余数按扫描序前多后少（注水）。候选排除源端口自身。
- 外部：正对第一个建筑、1:1、方向+维度匹配、挡住不穿透；大器件靠传输器接力。
- 内部布线：任意端口间（**含同方向**）、不自连。
- 背压：目标满则留上游端口；有料段保持活跃；扫描序靠前的先满先堵。
- 环：允许，但**去器件后组合子图必须无环**。
- 器件拍边界采样；组合当拍 settle。
- 服务器权威；客户端只表现；RPC。

---

## 8. 与当前代码（v1）的差异 / 迁移项

| 项 | v1（当前代码） | v2（目标） |
|---|---|---|
| 图/段节点 | **建筑级** | **端口级** |
| 端口数 | 写死 `MaxPorts=32` | 变长（按 DA 分配） |
| 布线 | `PortLinks` uint32 位掩码 | 变长边表 `Links` |
| 运行态存放 | fragment（`FEtherPortStateFragment`） | **Subsystem**（端口）；器件状态才进 fragment |
| 组合 op | 只有路由 | 端口级 `Op`（Wire/And/Or/Not/…） |
| 入/出 | 端口 `bIsInput` | **器件端口角色表**（`InputPorts/OutputPorts`） |
| 同方向互连 | 禁止 | **允许** |
| 器件 | Producer/Consumer（简化） | 器件 op（Facility/Latch/Counter/…）+ fragment 状态 |
| 类型语义 | `uint16`；Empty 当作 0 | **`int16` 有符号** + 互补态；**Empty ≠ 0**；同族异号湮灭 / 异族堵死 |
| 分流 | 无（建筑级逐端口） | 端口级 `SplitMode{Ordered,Balanced}` + **相对扫描序** + 注水 |

**迁移进度**

1. ✅ **步骤①**：端口级图 + 运行态搬 Subsystem + 变长端口/布线（`Ports/Edges/Links/Segments/ProcessOrder` 全按 PortId；fragment 只剩标记）。
   - 附加：`InternalLinks`（DA）成为内部布线的真实数据源；分流顺序暂用 `(Dir,Dim)`。
2. ✅ **步骤②**：器件角色表（`InputPorts/OutputPorts` 取代端口 `bIsInput`）+ `EAsceEtherDeviceOp`（Facility/Sink 已实现；Latch/Counter/Clock 占位）+ `FEtherDeviceStateFragment` 骨架（Trait 对器件挂载）。
   - ⚠️ DA 需重新配置：`Kind` 已删除 → 改 `bIsDevice` + `DeviceOp` + 角色表；`FAsceEtherPortDef.bIsInput` 已删除。
3. ✅ **步骤③a**：`SignalType` 改 **`int16`**（`Complement/Family/IsComplement`）；**Empty ≡ `Ether == 0`**（`SignalType == 0` 现在是"0 号 = 能量"）；
   合流三态（异族 → **堵死** / 同族异号 → **1:1 湮灭** / 同族同号 → 累加）；端口 `Op` 的 `Wire`/`Not`（`Not` = 推出去时取互补态）；
   分流 `SplitMode`（`Ordered` / `Balanced` = `ceil(剩余/剩余目标数)` + 份额顺延）；**扫描序**改为"相对源端口左手边起顺时针"（同方向内 Dim 低→高）。
   - ⏳ **步骤③b（已完成）**：`And/Or` = **入边求值**（推入时按入边记符号；空入边不计入；份数照收、只统一符号；0 号逻辑透明）；`And/Or` 目标**不湮灭**。
4. ✅ **步骤④a/④b（已落地）**：`Latch`（记忆单元：写入覆盖编号 / 读取用 `0 号` 能量重铸，`1 份 = abs(编号)`，不足不产出不累积）；
   `Clock`（自翻转 + 每拍重铸 = 自驱动记忆单元）；`Counter`（脉冲计数，满阈值产 1 份）；`Register`（**≤8 位**并行加载 + 用能量重铸读取，多位靠拼多个）；
   `Annihilator`（湮灭发电机：`+X` + `−X` ⇒ `abs(X)` 份 `0 号`）；`FEtherDeviceStateFragment` 真正投入使用；器件参数走 DA 的 `DeviceParamSignal/DeviceParamA`。
   - ⏳ 待做：记忆单元的"能量不足"提示（UI 层）；更多器件 op（移位寄存器/比较器）。

---

## 9. 未决 / 待办

- 端口 op 的具体集合与参数（门函数、多输入语义）。
- **0 号 ether 细则**：时序器件的能量消耗速率；能量是否也走"清空重填"；0 号是否有独立 op（如"能量计"）。
- **湮灭发电机**参数：产出比例是否恒为 `|X|`、是否需要"点火"条件或最低流量。
- 器件 op 集合（Facility/Latch/Counter/Clock…）与各自状态 fragment；哪些器件需要能量输入。
- 客户端同步字段与 RPC 清单。
- 增量重建（脏区域）。
- 配方细节（输入端口↔配方项、多输出、比率）。
