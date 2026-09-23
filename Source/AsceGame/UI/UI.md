# 实体 Fragment 字段采样与通知（Sampler）

> 基本单位 = `FEntityFragmentSampler`：监听**一个 entity 的一个 fragment**，key = `EntityHandle + UStruct*`。
> 采样是**拉模型 + 值 diff**：定期采 → 和影子副本比 → 只有变了才广播；
> 广播带 `(EntityNumber, FieldName)`，**sampler 不返回真实值**——绑定者拿到通知后自己用 EntityNumber 去 entity 上取值
> （例：监听"生命"变化时要算百分比，就同时监听"生命/最大生命"，任一变化都直接重读两个字段再算）。
> AS 侧不想手写绑定 ⇒ 用 `AsceBind` 标识 + `UAsceFieldBindingWidget` 基类自动绑/解（见 §6）。
> 伪代码风格与原草图一致；带 ← 的注释是本次评审后的修正点。

## 0. 关键约定（评审结论）

1. **字段必须 trivially copyable**：sampler 创建时校验 `EnumHasAnyFlags(FragmentType->StructFlags, STRUCT_IsPlainOldData)`；不合规**拒绝注册 + Log Warning**（不做非 POD / `Identical` 分支）。
2. **BitNumber = 属性在 fragment 里的枚举序号**（`TFieldIterator<FProperty>` 顺序），`FieldArray` / `BitNumberArray` 与它同序对齐；仅**进程内**稳定 ⇒ 配置/UI 引用字段一律用**名字**，绑定时解析。
3. **绑定即广播**：字段第一次有绑定者时先 `CaptureInit`（建影子），再 `Broadcast` 一次 ⇒ 订阅者当场拿到初值（和 MVVM 一致）。
4. **不 memset**：影子首次用 `Memcpy` 从实时值建立（连 padding 一起拷），避免垃圾/对齐差异导致的假变化。
5. **采样只在主线程**，且挂在 sim 阶段链**最后的独立阶段**（本帧数据都更新完之后）。

## 1. 数据结构

```cpp
using FFragmentFieldValueChangedDelegate =
    TDelegate<void(int64 EntityNumber, FName FieldName), FNotThreadSafeNotCheckedDelegateUserPolicy>;
    // ← 第 1 个参数 = FMassEntityHandle::AsNumber()（用 int64 是项目 AS/BP 的既有约定，
    //   FMassEntityHandle 不是 BlueprintType；C++ 侧 FromNumber 还原）。
    //   原草图从 MVVM 抄来的 UObject* 是"字段拥有者(VM)"——我们值在 fragment 里、没有这个对象。
    //   第 2 个参数只给字段名（BitNumber 是内部概念，不往外传）；绑定者身份由 delegate 自身 / Handle 标识。

struct FFragmentFieldId
{
    FName FragmentFieldName;   // 只用于诊断；比较/排序只看名字
    int32 BitNumber;           // = FieldArray / BitNumberArray 下标 = 属性枚举序号

    operator==(FFragmentFieldId& Other):
        return Other.FragmentFieldName == FragmentFieldName
}

struct FCacheField             // 每字段一份影子（diff 基准）
{
    FProperty* Property
    void* Buffer               // 上一帧采到的值
}

struct BindElement
{
    FFragmentFieldId FieldId
    FFragmentFieldValueChangedDelegate Delegate
}

struct FEntityFragmentSampler
{
    FMassEntityHandle EntityHandle
    UStruct* FragmentType
    TBitArray BitNumberArray          // 位 = 该字段当前是否有绑定者（立即置位/清位）
    TArray<FCacheField> FieldArray    // index == BitNumber
    TArray<BindElement> BindElements  // [0, AddedEmplaceAt) 有序区；[AddedEmplaceAt, N) 广播期暂存区
    int32 BindElementsLock            // 广播锁（计数，支持嵌套广播）
    int32 AddedEmplaceAt              // 暂存区起点；← 原草图里的 BindElementEnd 与它是同一个东西，统一用这个
    int32 CompactionCount             // 待压实计数（广播期间解绑的元素先留着）
}
// ← 原草图的 BindingDelegates / bMarkedForRemoval 都不需要：
//   - 广播期间新增直接追加到 BindElements 尾部（不是另一个数组）；
//   - 是否可回收看 BitNumberArray.CountSetBits() == 0，不用标记。
```

## 2. 流程

### Init（创建 sampler 时一次；含 P0-1 的校验）

```
if (!EnumHasAnyFlags(FragmentType->StructFlags, STRUCT_IsPlainOldData)): 拒绝创建 + Log Warning

int32 NumProperties = 0
for (TFieldIterator<FProperty> It(FragmentType); It; ++It):
    FCacheField CacheField
    CacheField.Property = *It
    CacheField.Buffer = FMemory::Malloc(It->GetSize(), It->GetMinAlignment())   // 不 memset，垃圾内容由 CaptureInit 覆盖
    FieldArray.Emplace(CacheField)
    ++NumProperties
BitNumberArray.Resize(NumProperties)      // 全 false
```

### CaptureInit（字段第一次有绑定者：只建影子，不比较、不广播）

```
FStructView View = FMassEntityView(EntityManager, GetEntity(EntityHandle)).GetFragmentDataStruct(FragmentType)
if (!View.IsValid()): 标记可回收（见 §4）; return          // 实体没了 / fragment 没了

FCacheField& CacheField = FieldArray[FieldId.BitNumber]
FMemory::Memcpy(
    CacheField.Buffer,
    CacheField.Property->ContainerPtrToValuePtr<void>(View.GetMemory()),
    CacheField.Property->GetSize())
```

### Capture（采一个字段；变了就广播）

```
FStructView View = FMassEntityView(EntityManager, GetEntity(EntityHandle)).GetFragmentDataStruct(FragmentType)
if (!View.IsValid()): 标记可回收; return

FCacheField& CacheField = FieldArray[FieldId.BitNumber]
void* FieldPtr = CacheField.Property->ContainerPtrToValuePtr<void>(View.GetMemory())
const int32 Size = CacheField.Property->GetSize()

if (FMemory::Memcmp(FieldPtr, CacheField.Buffer, Size) == 0):
    return                                                  // 没变，啥也不干

FMemory::Memcpy(CacheField.Buffer, FieldPtr, Size)          // ← 方向：影子 = 实时（原草图是反的，会把旧影子写回 fragment）
Broadcast(FieldId)                                          // ← 广播后不再使用 View / FieldPtr（回调里可能 demolish 实体）
```

### CaptureAll（周期采样入口：调度器轮到这个 sampler 时调）

```
for (int32 Bit = 0; Bit < BitNumberArray.Num(); ++Bit):
    if (!BitNumberArray[Bit]): continue                      // 该字段没人绑，跳过
    FFragmentFieldId FieldId
    FieldId.FragmentFieldName = FieldArray[Bit].Property->GetFName()
    FieldId.BitNumber = Bit
    Capture(FieldId)                                         // ← 每字段各自重新取一次 View（不缓存 fragment 指针）
    if (BitNumberArray.CountSetBits() == 0): return           // 广播里可能把自己清空/解绑
```

### AddDelegate

```
// 插入：锁内追加到尾部（暂存区），锁外二分插入有序区
if (BindElementsLock > 0):
    const int32 Index = BindElements.Emplace({FieldId, Delegate})
    AddedEmplaceAt = Min(AddedEmplaceAt, Index)
else:
    BindElements.EmplaceAt(UpperBound(FieldId), {FieldId, Delegate})

// 该字段第一次有绑定者 ⇒ 建影子
if (!BitNumberArray[FieldId.BitNumber]):
    BitNumberArray[FieldId.BitNumber] = true
    CaptureInit(FieldId)

Broadcast(FieldId)        // ← 绑定即广播：委托已在表里 ⇒ 新订阅者当场拿到初值
```

### RemoveDelegate

```
// 在有序区 + 暂存区两处找该 FieldId 的 BindElement
if (BindElementsLock > 0):
    Element.Delegate.Unbind()
    ++CompactionCount                       // 元素留在数组里，等解锁后压实
else:
    BindElements.RemoveAt(Index)

// 两区都确认没有该 FieldId 的"已绑定"委托之后，才清位
if (该 FieldId 在有序区和暂存区都没有 IsBound 的委托):
    BitNumberArray[FieldId.BitNumber] = false
```

### Broadcast（照引擎：有序区 + 暂存区两段）

```
++BindElementsLock

// ① 有序区：从 UpperBound(FieldId)-1 往前扫，遇到不同 FieldId 就 break
//    （注意二分只搜 [0, AddedEmplaceAt) 这段）
// ② 暂存区（广播期间新增的也收到本次）：从 BindElements.Num()-1 倒序扫到 AddedEmplaceAt

--BindElementsLock
ExecuteLockOperations()
```

### ExecuteLockOperations（Lock == 0 才做）

```
if (CompactionCount > 2):
    压实：摘掉已 Unbind 的元素；每删掉一个位于 AddedEmplaceAt 之前的元素就 --AddedEmplaceAt
    CompactionCount = 0

for (; AddedEmplaceAt < BindElements.Num(); ):
    if (BindElements[AddedEmplaceAt].Delegate.IsBound()):
        FoundIndex = UpperBound(该元素的键)
        if (FoundIndex != AddedEmplaceAt):                   // 需要挪才挪
            Element = MoveTemp(BindElements[AddedEmplaceAt])
            BindElements.RemoveAtSwap(AddedEmplaceAt)
            BindElements.EmplaceAt(FoundIndex, MoveTemp(Element))
        ++AddedEmplaceAt
    else:
        BindElements.RemoveAtSwap(AddedEmplaceAt)            // 已解绑，直接丢

AddedEmplaceAt = MAX_int32
```

### 显式广播（外部数据变了、fragment 值没变）

```
BroadcastField(Entity, FragmentType, FieldName)   // 下推单个字段
BroadcastAllFields(Entity, FragmentType)          // 下推该 (实体, fragment) 上所有"有绑定者"的字段
```

- 用途：数据在**外部系统**（例：Ether 网络持有节点实际状态，`FEtherBuildingFragment` 只存 NodeId）⇒
  外部数据变了要**主动推一次**，订阅者收到后重新去外部取；
- **不管值变没变**都推；推之前先把影子对齐实时（避免下一轮采样又因"值变了"重复广播一次）；
- 没有 sampler（没人订阅）或该字段没绑定者 ⇒ **空操作**（不建影子、不白跑）。

### 取值（**不经过 sampler**）

```
// 广播带 (EntityHandle, FieldId)；绑定者收到通知后自己去 entity 上取值：
//   const FMyFragment* F = EntityManager.GetFragmentDataPtr<FMyFragment>(Entity);
//   （需要多个字段一起算就都绑上：任一变化都重读全部并用当前值重算）
// sampler 只负责"通知哪个实体、哪个字段变了"，不返回、也不缓存"给绑定者的值"。
```

### Release（销毁 sampler）

```
for (int32 i = 0; i < FieldArray.Num(); ++i):
    FMemory::Free(FieldArray[i].Buffer)         // 不 DestroyValue：只支持 POD 字段
    FieldArray[i].Property = nullptr
    FieldArray[i].Buffer = nullptr
FieldArray.Empty()
BindElements.Empty()
BitNumberArray.Empty()
AddedEmplaceAt = MAX_int32
BindElementsLock = 0
CompactionCount = 0
```

## 3. 调度器 / 注册表（另需实现，不在上面伪代码内）

- **唯一真相 = 一个有序数组**，按 `(EntityHandle, UStruct* FragmentType)` 排序：
  - 二级键用**指针地址**即可（目的只是把同一个 key 的 sampler 放一起，不要求跨运行稳定）；
  - 查找用 `Algo::LowerBoundBy`；实体销毁时 `lower_bound(Handle, 最小 Type)` 起顺序扫、批量删。
  - 没有字典（不需要第二份索引）。
- **游标（键式）**：`{EntityHandle, UStruct*}` + "从头"标志；每帧从 `LowerBound(游标)` 起处理**常量预算**（默认 64~128，可配）个 sampler；到数组末尾 ⇒ 游标置无效 + 做 sweep。
- **sweep**：回绕时、锁外、**整遍扫**，删掉 `BitNumberArray.CountSetBits() == 0` 的 sampler（不做 mark、不做复活）。
- 遍历期间用**下标**访问、不跨回调持有元素引用（插入可能触发数组重分配）。
- 所有入口 `check(IsInGameThread())`；挂 `UAsceSimulationSubsystem` 的最后一个阶段。
- 待定：`pendingAdd`（遍历中插入的暂存）**是否还需要**——键式游标 + 只在 sweep 删除的情况下，插入不会导致跳过（初值已由绑定即广播给出），我认为可以不要；实现时如果发现遍历期间插入会破坏 sweep 的前置条件，再补。

## 4. 字段目录 / 布局约定

- **不做字段目录/缓存**：绑定时遍历 `TFieldIterator<FProperty>(FragmentType)` 比对 `GetFName() == FieldName`，命中项的序号就是 BitNumber、`*It` 就是 `FProperty*`，直接组成 `FFieldId`；未命中 ⇒ 报错返回。
- **两处遍历必须同规则**（`Init()` 建 `FieldArray` 与绑定解析都用 `TFieldIterator<FProperty>`、同样的 iterator flags/过滤），否则 BitNumber 会错位。
- 目录键是 `FragmentType`（每个 sampler 自带），所以同名字段跨 fragment 互不影响。

## 5. 评审结论清单（逐条）

| # | 结论 |
|---|---|
| P0-1 | 只支持 trivially copyable：按 fragment 的 `STRUCT_IsPlainOldData` 校验，不合规拒绝注册 + Warning；不 memset，首次 `memcpy` 建影子；绑定即广播 |
| P0-2 | 无设计改动；约束：`CaptureAll` 逐字段调 `Capture`（每字段各自取 View），广播后不再用该 View |
| P0-3 | 不做 mark/复活；回收判据 = `BitNumberArray.CountSetBits() == 0`；sweep 在回绕点、锁外、整遍扫；解绑清位保持立即 |
| P1-4 | 键式游标 `{EntityHandle, UStruct*}` + "从头"标志；扫到末尾置无效 + 触发 sweep；删除"插入在游标前立即采样"特例 |
| P1-5 | 作废（没有 mark 就没有"跳过 marked"这件事） |
| P1-6 | 挂 sim 调度器最后阶段；入口 `check(IsInGameThread())` |
| P2-7 | 不做 tier / 自适应 / RequestSampleNow（后补）；同频 + 常量预算 |
| P2-8 | 不做字段目录；绑定时遍历解析（BitNumber = 枚举序号） |
| P2-9 | 只有有序数组一份（无字典）；二级键用指针地址 |
| P3-10 | 影子是纯值快照（不进 GC、不解引用）；接受 ABA 漏一次广播；指针值由订阅者重新取（fragment 里不存指针，存 id 更稳） |
| P3-11 | `AddedEmplaceAt` 统一一个变量；"是否还有其它绑定者"要两区都扫 |
| 签名 | 通知 = `void(int64 EntityNumber, FName FieldName)`（多实体订阅靠 EntityNumber 区分）；**sampler 不返回真实值**，绑定者收到通知后自己从 entity 取值（需要多字段就都绑上）；绑定者身份由 delegate 自身 / `BindField` 返回的 Handle 标识（不额外存 owner） |
| 显式广播 | `BroadcastField` / `BroadcastAllFields`：外部系统改了关联数据时主动下推（**不管值变没变**，先对齐影子）；没人订阅 / 字段没绑定者 ⇒ 空操作（**兜底**；主路径见 §7 版本字段） |
| 版本字段 | 外部数据用 `FEtherBuildingFragment::Revision`（放在委托标志 fragment 里）：只跟**结构/配置**变化自增（`ApplyGraphCommands` 末尾，含受影响的邻居），料流不 bump、面板自轮询读；`GetNodeIdByEntity` 供 UI 定位节点；完工前 fragment 不存在 ⇒ 不补绑（约定 B） |

## 6. AS 侧：标识自动绑定（`UAsceFieldBindingWidget`）

不想手写绑定/解绑 ⇒ 面板继承 `UAsceFieldBindingWidget`（它继承 `UAsceActivatableWidget`），给 handler 打标识：

```angelscript
UFUNCTION(Meta = (AsceBind = "FAsceHealthFragment:CurrentHealth,FAsceHealthFragment:MaxHealth"))
void OnHealthFieldsChanged(int64 EntityNumber, FName FieldName)
{
    // 任一变化：自己用 EntityNumber 去 entity 上重读"生命/最大生命"，重算百分比
}
```

机制（都在 C++ 基类里，AS 只写 handler + 标识）：
- `NativeConstruct` ⇒ `RefreshFieldBindings()`：遍历本类（含父类）所有 `UFUNCTION` → 读 meta `AsceBind`
  → 解析 `FragmentStructName:FieldName`（逗号分隔）→ `FindFragmentTypeByName` 解析出 `UScriptStruct*`
  → 对 `BindTargetNumbers` 里每个实体 `BindField(..., Delegate::CreateUFunction(this, 函数名))`，句柄存数组；
- `NativeDestruct` ⇒ `ClearFieldBindings()`：全部解绑；
- 目标实体：`UPROPERTY(BlueprintReadWrite) TArray<int64> BindTargetNumbers`（列表行设 1 个、多选面板设多个），改完调 `RefreshFieldBindings()`；
- 校验：handler 签名必须是 `(int64, FName)`、类型名/字段名必须能解析 —— 否则**打 Error 日志并跳过**（绝不静默）。

**已知不足**：meta 是字符串，AS 里没有补全、没有编译期检查 ⇒ 拼错只能靠 Construct 时的 Error 日志暴露。
后续可做（见"待办"）：编辑器校验工具 + "可用 `类型:字段` 清单"生成（至少解决可发现性，补全做不到）。

## 7. 外部数据 ⇒ 版本字段（Revision）

"真数据在外部系统、fragment 只是句柄/标记"的场景，用**修订号**当失效信号，比逐字段广播更省：

- **版本字段放在"委托标志 fragment"里**（谁把数据委托出去，谁在自己的 fragment 上带修订号）：
  当前就是 `FEtherBuildingFragment::Revision`（`uint8`，只增，回绕无害，订阅方只判断"不等"）；
  需要分方面（拓扑 / 料流…）就在这个 fragment 里加字段，粒度由该系统自己定。
- **只管结构/配置，不管料流**：
  - Ether 在 `ApplyGraphCommands` 末尾，对"本拍受影响节点"（目标节点 + 受影响端口所属节点，含被接上的邻居；全量重建则所有节点）统一 `++Revision`；
  - `Ether`/信号值这类**逐帧在动**的数据**不 bump**（否则通知机制等于每帧喊一次）：面板可见时自己按节拍用
    `GetPortEther` / `GetPortSignalType` / `GetPortView` 读；
  - 画布/UI 拿到 `EntityNumber` 后要定位节点 ⇒ `UAsceEtherNetworkSubsystem::GetNodeIdByEntity()`（已提供）。
- **订阅**：`AsceBind = "FEtherBuildingFragment:Revision"` ⇒ 收到通知后自己去 Ether 取需要的结构数据。
- **时序约定（选 B）**：`FEtherBuildingFragment` 是**完工时**才加上的 ⇒ "建造中就绑 Ether 字段"的场景**不做补绑**，
  约定只有已完工建筑才显示 Ether 数据（面板不重试、不特判）。
- `BroadcastField` / `BroadcastAllFields`（§2）保留为**兜底**：给"没有版本字段 / 一次性事件"的场景。

## 8. 待办

- 编辑器校验：写一个 `UEditorValidatorBase`（项目 `AsceEditor` 已有 `EditorValidator` 体系）/ 或 console command，
  扫描所有 AS widget 类的 `AsceBind` 标识：类型名能否解析、字段是否存在、handler 签名是否正确（不进 PIE 就能发现问题）。
- 可发现性：dump 一份"所有 fragment 及其字段名"的清单（`类型:字段` 形式），写 meta 时对照；也可作为给 AI 的上下文。
- `RequestSampleNow`（热字段不等一圈）、预算自适应、tier 数组（P2-7 的后补项）。
- 超出预算的"同频"限制：如果将来出现"一次绑几百个实体"的面板，再按 P2-7 的 tier/预算方案处理。
