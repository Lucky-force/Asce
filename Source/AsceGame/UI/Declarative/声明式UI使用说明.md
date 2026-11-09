# 声明式 UI（AS 写布局，C++ 建控件）

> 代码：`Source/AsceGame/UI/Declarative/`
> 目标：**布局写在 AS 代码里**（可 diff、可让 AI 改），C++ 负责校验并即时创建 UMG 控件；
> 视觉全部配在控件资产（WBP 子类 / C++ 类默认值）里，**样式表只负责"这个位置用哪个控件类"**。

---

## 1. 文件与职责

| 文件 | 内容 |
|---|---|
| `AsceUIDeclarativeTypes.h` | `EAsceUISpace` + 5 个样式 token 枚举（Panel/Image/Text/Button/Bar） |
| `AsceUIThemeSettings.h/.cpp` | `UAsceUIStyleSet`（Project Settings → Asce → **UI Styles**）：5 张"token → 控件类"表 + `SpaceScale` |
| `AsceUIBuilder.h/.cpp` | `UAsceUIBuilder`：**调用即创建控件**（作用域栈 + 校验 + id 映射） |
| `AsceDeclarativeWidget.h/.cpp` | `UAsceDeclarativeWidget`：AS 继承它，实现 `BuildUI`；提供 `FindWidget/FindPanel` |

数据流：

```
AS: BuildUI(Builder)  →  Builder.VerticalBox(...)  →  直接 NewObject/ConstructWidget
                                   ├─ 样式：按 token 从 UAsceUIStyleSet 取控件类
                                   ├─ 挂载：挂到当前作用域栈顶
                                   └─ 记录：Id → UWidget（供 FindWidget）
```

---

## 2. 快速开始

### 2.1 配置样式表

Project Settings → **Asce → UI Styles**，填 5 张表（**每个 token 一个控件类**）：

| 表 | 控件类要求 | 建议做法 |
|---|---|---|
| `PanelStyles` | `UBorder` 派生 | WBP 子类，背景/圆角/边距在 WBP 里配 |
| `ImageStyles` | `UImage` 派生 | WBP 或直接用 `UImage` |
| `TextStyles` | `UTextBlock` 派生 | `UCommonTextBlock` 或 WBP 子类，字体/颜色在里面配 |
| `ButtonStyles` | `UButton` 或 `UCommonButtonBase` 派生 | 见 §6 |
| `BarStyles` | `UProgressBar` 派生 | 填充色/背景在控件里配 |

没配的 token → 用引擎默认类（`UBorder/UImage/UTextBlock/UButton/UProgressBar`），不会崩。

### 2.2 写 AS 面板类

```angelscript
class UUnitPanel : UAsceDeclarativeWidget
{
    UFUNCTION(BlueprintOverride)
    void BuildUI(UAsceUIBuilder B)
    {
        B.VerticalBox(n"Root", EAsceUISpace::M);                       // 竖排 + 子节点间隔 M
            B.Text(n"Title", "单位信息", EAsceUITextStyleToken::Title)
                .Padding(EAsceUISpace::S);

            B.HorizontalBox(n"HpRow", EAsceUISpace::S);
                B.Text(n"HpText", "100/100", EAsceUITextStyleToken::Body);
                B.ProgressBar(n"HpBar", 1.0, EAsceUIBarStyleToken::Health);
            B.End();

            B.Button(n"BtnMove", "移动", EAsceUIButtonStyleToken::Primary);
        B.End();
    }

    UFUNCTION(BlueprintOverride)
    void Construct()
    {
        if (UButton Btn = Cast<UButton>(FindWidget(n"BtnMove")))
        {
            Btn.OnClicked.AddUFunction(this, n"OnMoveClicked");
        }
    }

    UFUNCTION()
    void OnMoveClicked()
    {
        // …下指令
    }
}
```

### 2.3 挂到现有 UI 栈

`UAsceDeclarativeWidget` 继承自 `UAsceActivatableWidget`，可以直接被 CommonUI 栈推入：

```cpp
RootLayout->PushWidgetToLayerStack<UAsceDeclarativeWidget>(TAG_UI_LAYER_MENU, UUnitPanel::StaticClass());
```

（按 tag 注册/弹出这套还没做，见 §10。）

---

## 3. Builder API

### 3.1 容器（进入子作用域，**必须配一个 `End()`**）

| 方法 | 控件 | 子节点 |
|---|---|---|
| `VerticalBox(Id, Spacing)` | `UVerticalBox` | 多个 |
| `HorizontalBox(Id, Spacing)` | `UHorizontalBox` | 多个 |
| `Overlay(Id)` | `UOverlay` | 多个 |
| `ScrollBox(Id)` | `UScrollBox` | 多个 |
| `CanvasPanel(Id)` | `UCanvasPanel` | 多个（子节点可用 Anchors/Offsets） |
| `Border(Id, PanelStyleToken)` | 样式类（`UBorder` 派生） | **1 个** |
| `SizeBox(Id)` | `USizeBox` | **1 个**（配 `MinSize/MaxSize`） |

### 3.2 叶子（不能挂子节点）

| 方法 | 控件 | builder 设置的内容 |
|---|---|---|
| `Text(Id, FText, TextStyleToken)` | 样式类（`UTextBlock` 派生） | 文字 |
| `Image(Id, Texture, ImageStyleToken)` | 样式类（`UImage` 派生） | 贴图（给了才设） |
| `Button(Id, FText, ButtonStyleToken)` | 样式类（`UButton`/CommonButton 派生） | 文字（见 §6） |
| `ProgressBar(Id, Percent, BarStyleToken)` | 样式类（`UProgressBar` 派生） | 百分比 |
| `Spacer(Id)` | `USpacer` | — |

### 3.3 布局参数（作用于"最近创建的控件"）

| 方法 | 作用 |
|---|---|
| `Padding(Space)` | 槽位内边距（根节点无效，会记警告） |
| `Align(H, V)` | 槽位对齐（Box/Overlay/Scroll/Border/SizeBox/Button 槽位都支持） |
| `MinSize(Vector2D)` / `MaxSize(Vector2D)` | 仅 `SizeBox` |
| `SetPercent(float)` | 仅 `ProgressBar` |
| `Anchors(FAnchors)` / `Offsets(FMargin)` | 仅 **CanvasPanel 的子节点** |
| `Select(Id)` | 切"当前操作目标"，**不改挂载作用域** |

### 3.4 结果 / 查询

| 方法 | 说明 |
|---|---|
| `End()` | 结束当前容器作用域 |
| `FindWidget(Id)` / `FindPanel(Id)` | 控件 / `UPanelWidget`（构建后） |
| `GetRootWidget()` | 根控件 |
| `GetErrors()` / `GetOpenScopeCount()` | 构建期错误 / 未 `End()` 的容器数 |

---

## 4. 作用域与"当前控件"规则（**最容易搞错的地方**）

两件事是分开的：

| 操作 | 依据 |
|---|---|
| **新建控件挂到哪** | 作用域栈顶（栈空 = 根节点） |
| **属性改谁**（Padding/MinSize/Anchors…） | `LastWidget`（最近创建 / 最近 `End` 回到 / 最近 `Select` 的控件） |

| 调用 | 挂载 | LastWidget | 栈 |
|---|---|---|---|
| `Text(...)` | 栈顶容器 | = 新 Text | 不变（叶子不压栈） |
| `Overlay(...)` | 栈顶容器 | = 新 Overlay | **压栈** |
| `End()` | — | = 刚关闭的容器 | 弹栈 |
| `Padding(...)` / `Align(...)` / … | — | 不变 | 不变 |
| `Select(Id)` | — | = 指定控件 | 不变 |

**推荐写法**：容器自己的属性紧跟创建时设；每个子控件创建后立刻链式设属性。

```angelscript
B.Overlay(n"Root");
    B.Text(n"A", "A").Padding(EAsceUISpace::S).Align(EHorizontalAlignment::Center, EVerticalAlignment::Center);
    B.Text(n"B", "B").Padding(EAsceUISpace::S);
B.End();

B.Select(n"Root").Padding(EAsceUISpace::L);   // 事后改容器（可选）
```

嵌套示例（合法、栈是平衡的）：

```angelscript
B.Overlay(n"Root");
    B.Overlay(n"Lay1").Padding(EAsceUISpace::M);
        B.Text(n"A", "A").Padding(EAsceUISpace::S);
        B.CanvasPanel(n"Lay2");                       // 要锚点用 CanvasPanel
            B.ProgressBar(n"C", 0.5)
                .Anchors(FAnchors(0.0, 1.0)).Offsets(FMargin(-80.0, -40.0, 160.0, 24.0));
        B.End();
    B.End();
B.End();
```

---

## 5. 样式（一个 token = 一个控件类）

- **代码里没有任何样式数值**：颜色/字体/画刷都存在控件资产（WBP 子类）里；
- 换皮 = 改 WBP 资产，**不用动 AS、不用动代码**；
- 特殊外观/特殊控件 = 样式表里加一个 token + 一个控件类；
- token 为 `None` 或未配置 → 引擎默认类；
- 类型不符（例如 Text 样式配了 `UBorder`）→ 回退默认类 + 一条构建错误。

间距：`EAsceUISpace` 在 `SpaceScale` 里换算（默认 `None 0 / XS 2 / S 4 / M 8 / L 12 / XL 20`）。

---

## 6. 按钮的两种类

| 样式类 | builder 行为 |
|---|---|
| `UButton` 派生（含 WBP 子类） | 自动加一个居中的 `UTextBlock` 子节点（名字 `Label`）作为文字 |
| `UCommonButtonBase` 派生 | 调用它的 **`UFUNCTION SetLabel(FText)`**；没有这个函数会记构建错误 |

> CommonButton 的点击事件在 AS 里是 `Btn.OnClicked().AddUFunction(this, n"OnClicked")`（注意是函数调用写法）；
> 普通 `UButton` 是 `Btn.OnClicked.AddUFunction(...)`。

如果想让 WBP 按钮既能用 CommonUI 样式、又能被 builder 设文字，就在 WBP 的父类里实现
`UFUNCTION SetLabel(FText Label)`（写进里面那个文本控件）。

---

## 7. 动态内容（技能格数量不固定等）

布局只负责"骨架"，动态项按 id 取容器后自己加：

```angelscript
UFUNCTION(BlueprintOverride)
void Construct()
{
    if (UPanelWidget SkillBar = FindPanel(n"SkillBar"))
    {
        for (int i = 0; i < SkillCount; ++i)
        {
            UUserWidget Entry = CreateWidget(this, USkillSlotWidget);
            SkillBar.AddChild(Entry);
        }
    }
}
```

（`UListView` 不在 v1 范围内，列表项先用 `ScrollBox/HorizontalBox` + 池化。）

---

## 8. 错误处理

builder 的所有校验都**只记录不崩**，构建继续（必要时退化到默认类）：

| 情况 | 行为 |
|---|---|
| 样式类类型不符 | 回退默认类 + 错误 |
| 单子节点容器（Border/SizeBox）放了第二个孩子 | 忽略 + 错误 |
| 重复 Id | 保留第一个 + 错误 |
| 多个根节点 | 忽略多余的 + 错误 |
| 多余的 `End()` | 错误 |
| 缺少 `End()` | `BuildErrors` 报"有 N 个容器没有 End()" |
| 根节点 `Padding` | 警告（没有父槽位） |
| `Anchors/Offsets` 不在 CanvasPanel 下 | 错误 |
| `MinSize/MaxSize` 不是 SizeBox | 错误 |

查看方式：
- `UAsceDeclarativeWidget::GetBuildErrors()`；
- Output Log 里搜 `[UI]`。

---

## 9. 与 CommonUI / UI 栈的关系

- `UAsceDeclarativeWidget` 继承 `UAsceActivatableWidget`，因此**能被现有 CommonUI 栈推入/弹出**；
- 层（`UI.Layer.Game/HUD/Menu/Modal`）、z-order、输入模式、焦点仍由 `UPrimaryGameLayout` + 层 Stack 管；
- 声明式部分只管"这个界面长什么样"，不碰层级。

---

## 10. 限制 / 待做

1. **不做运行期热重载**：改布局要重启 PIE（重新 `CreateWidget` 才重建）；
2. **单遍构建**：`BuildUI` 里必须把容器写完整（漏 `End()` 会报错）；不能在别处"回到"某个容器加子节点（动态内容用 `FindPanel`）；
3. **没有可视化预览**：调试靠 id 命名 + Output Log；
4. **按 tag 注册/弹出屏幕**（`PushScreenByTag` 等）还没做：目前直接用 `PushWidgetToLayerStack` 推类；
5. 列表虚拟化、复杂控件（小地图/拖拽）仍用自建控件 + `FindPanel` 挂载。
