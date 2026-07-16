# 课5c：迷你垂直切片 —— 把反射"看"起来

## 目标

跳过课 6-9（编译器）和课 10（QGraphicsView 框架），用一个**最小 Qt 窗口**验证反射系统真的能用。

完成后你会看到：
- 一个标题为 "Test Expr" 的窗口
- 顶部 4 个可编辑字段（scale / color / enabled / label），**完全由反射自动生成**
- 底部实时 JSON 预览，改任何字段 → JSON 立刻刷新

**这不是正式 UI**，课 10-13 会做完整版。这一课是"垂直切片" —— 用最小代码穿透数据层 + 反射层 + UI 层，验证设计、建立直觉。

---

## 背景知识

### 为什么需要垂直切片

按"正统"顺序往下走（课 6 → 9），你要等 4-5 节课才能看到第一个节点。这中间只有"它没崩"这种**负面反馈**，没有正面反馈。调试变成主要学习手段 —— 因为只有出 bug 时才有反应。

工程实践中，这叫**垂直切片（Vertical Slice）**：用最小代码穿透所有层（数据 → 反射 → UI），即使每一层都是简化版。目的是**验证设计**和**建立直觉**。垂直切片不是"完成品"，做完可以扔掉。

### 反射能驱动 UI 的本质

回头看反射对外提供的 API：

```cpp
class Expression {
public:
    std::vector<FieldDesc>   GetParameters() const;              // 枚举字段
    void                     SetParameter(name, json);            // 写
    nlohmann::json           GetParameter(name) const;            // 读
};
```

每个 `FieldDesc` 包含：
- `name`：字段名（"scale"、"color"…）
- `type`：Property 子类身份（FloatProperty / BoolProperty / StringProperty / Vec3Property…，用 `dynamic_cast` 判断）
- `default_value`：默认值

这就够了 —— UI 完全可以**数据驱动**地生成：

```
for (字段 in GetParameters()) {
    根据 field.type 创建对应 Qt 控件;
    控件值变化时，调 SetParameter(field.name, newValue);
}
```

**完全不知道字段叫什么、是什么类型**，也能生成完整 UI。这就是 UE5 Details Panel 的工作原理（虽然 UE5 更复杂）。

### Qt 布局回顾

| 布局类 | 用途 |
|--------|------|
| `QVBoxLayout` | 垂直堆叠（从上到下） |
| `QHBoxLayout` | 水平堆叠（从左到右） |
| `QFormLayout` | "字段名: 控件" 的两列表单 |

我们要的窗口结构：

```
QVBoxLayout（主）
├── QLabel（标题：类型 + 分类）
├── QFormLayout（字段表单）
│     ├── "scale:"   → QDoubleSpinBox
│     ├── "color:"   → QWidget(3 个 QDoubleSpinBox)
│     ├── "enabled:" → QCheckBox
│     └── "label:"   → QLineEdit
└── QLabel（JSON 预览，自适应拉伸）
```

---

## 操作步骤

### 1. 创建文件

```
src/Demos/ReflectionDemo/ReflectionDemoWidget.h
src/Demos/ReflectionDemo/ReflectionDemoWidget.cpp
```

目录 `src/Demos/` 是新加的，专门放"教学用垂直切片"，做完后可以删除。

### 2. ReflectionDemoWidget.h

```cpp
#pragma once
#include <QWidget>
#include <QLabel>
#include <QFormLayout>
#include <QVBoxLayout>
#include <map>
#include <string>
#include "Expression/Public/Expression.h"

class ReflectionDemoWidget : public QWidget {
    Q_OBJECT  // 因为用到了 qobject_cast，必须加 Q_OBJECT 让 moc 处理

public:
    explicit ReflectionDemoWidget(Expression* expr, QWidget* parent = nullptr);

private slots:
    // 任何控件变化都触发这个 slot
    void OnFieldChanged();

private:
    void BuildForm();          // 遍历 GetParameters() 构建 UI
    void RefreshJsonPreview(); // 刷新底部 JSON 显示

    Expression*  expr_;            // 不持有所有权，外部传进来
    QVBoxLayout* mainLayout_;
    QFormLayout* formLayout_;
    QLabel*      jsonLabel_;

    // 字段名 → Qt 控件 的映射
    // 关键：因为不知道字段类型，统一存 QWidget* 基类指针
    // 取值时用 qobject_cast<XxxWidget*>(widget) 转回具体类型
    std::map<std::string, QWidget*> fieldWidgets_;
};
```

**讲解**：
- `Q_OBJECT` 宏：让 Qt 的 moc（Meta-Object Compiler）处理这个类，提供 signals/slots/qobject_cast 能力。CMake 里 `set(CMAKE_AUTOMOC ON)` 会自动调用 moc。
- `fieldWidgets_`：核心数据结构。**字段名 → 控件** 的映射，让我们能反向从控件值构造 JSON 写回 Expression。
- `Expression*` 不持有所有权 —— 由 main 函数拥有，widget 只借用。

### 3. ReflectionDemoWidget.cpp

```cpp
#include "Demos/ReflectionDemo/ReflectionDemoWidget.h"
#include <QHBoxLayout>
#include <QFrame>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLineEdit>
#include <sstream>
#include <iomanip>

ReflectionDemoWidget::ReflectionDemoWidget(Expression* expr, QWidget* parent)
    : QWidget(parent), expr_(expr) {

    setWindowTitle("Reflection Demo");
    resize(640, 480);

    mainLayout_ = new QVBoxLayout(this);

    // === 1. 顶部标题（从 ClassDesc 读元数据）===
    const reflection::ClassDesc* desc = expr_->GetClassDesc();
    QLabel* titleLabel = new QLabel(QString("类型：%1   分类：%2   颜色标记：%3")
        .arg(QString::fromStdString(desc->display_name))
        .arg(QString::fromStdString(desc->category))
        .arg(QString::fromStdString(desc->category_color)));
    titleLabel->setStyleSheet("font-size: 14px; font-weight: bold; padding: 8px;");
    mainLayout_->addWidget(titleLabel);

    // 分隔线
    QFrame* line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    mainLayout_->addWidget(line);

    // === 2. 属性表单（数据驱动构建）===
    formLayout_ = new QFormLayout;
    mainLayout_->addLayout(formLayout_);
    BuildForm();

    // === 3. JSON 预览（底部）===
    mainLayout_->addWidget(new QLabel("实时 JSON 状态："));
    jsonLabel_ = new QLabel;
    jsonLabel_->setStyleSheet(
        "font-family: Consolas, 'Courier New', monospace;"
        "background: #1e1e1e; color: #dcdcdc;"
        "padding: 10px; border-radius: 4px;");
    jsonLabel_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    jsonLabel_->setMinimumHeight(150);
    jsonLabel_->setTextFormat(Qt::RichText);  // 支持 <br> 和 &nbsp;
    mainLayout_->addWidget(jsonLabel_, /*stretch=*/1);

    RefreshJsonPreview();
}

void ReflectionDemoWidget::BuildForm() {
    // GetParameters 返回 vector<const Property*>（课5 新反射系统）
    for (const Property* prop : expr_->GetParameters()) {
        QWidget* editor = nullptr;
        nlohmann::json cur = expr_->GetParameter(prop->name);

        // === 用 dynamic_cast 判断 Property 子类类型（不用枚举 switch）===
        if (dynamic_cast<const FloatProperty*>(prop)) {
            auto* spin = new QDoubleSpinBox;
            spin->setRange(-1000.0, 1000.0);
            spin->setDecimals(3);
            spin->setSingleStep(0.1);
            spin->setValue(cur.is_number() ? cur.get<float>() : 0.0f);
            connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                    this, &ReflectionDemoWidget::OnFieldChanged);
            editor = spin;
        }
        else if (dynamic_cast<const BoolProperty*>(prop)) {
            auto* check = new QCheckBox;
            check->setChecked(cur.is_boolean() ? cur.get<bool>() : false);
            connect(check, &QCheckBox::stateChanged,
                    this, &ReflectionDemoWidget::OnFieldChanged);
            editor = check;
        }
        else if (dynamic_cast<const StringProperty*>(prop)) {
            auto* edit = new QLineEdit;
            edit->setText(QString::fromStdString(
                cur.is_string() ? cur.get<std::string>() : ""));
            connect(edit, &QLineEdit::textEdited,
                    this, &ReflectionDemoWidget::OnFieldChanged);
            editor = edit;
        }
        else if (dynamic_cast<const Vec3Property*>(prop)) {
            auto* container = new QWidget;
            auto* hbox = new QHBoxLayout(container);
            hbox->setContentsMargins(0, 0, 0, 0);
            for (int i = 0; i < 3; ++i) {
                auto* spin = new QDoubleSpinBox;
                spin->setRange(-1000.0, 1000.0);
                spin->setDecimals(3);
                spin->setSingleStep(0.1);
                float v = (cur.is_array() && cur.size() > i) ? cur[i].get<float>() : 0.0f;
                spin->setValue(v);
                connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                        this, &ReflectionDemoWidget::OnFieldChanged);
                hbox->addWidget(spin);
            }
            editor = container;
        }
        else {
            editor = new QLabel("(暂不支持的属性类型)");
        }

        formLayout_->addRow(QString::fromStdString(prop->name) + " :", editor);
        fieldWidgets_[prop->name] = editor;
    }
}

void ReflectionDemoWidget::OnFieldChanged() {
    const ClassDesc* desc = expr_->GetClassDesc();
    if (!desc) return;

    for (const auto& [name, widget] : fieldWidgets_) {
        const Property* prop = desc->find(name);  // find 返回 const Property*
        if (!prop) continue;

        if (dynamic_cast<const FloatProperty*>(prop)) {
            auto* spin = qobject_cast<QDoubleSpinBox*>(widget);
            if (spin) expr_->SetParameter(name, (float)spin->value());
        }
        else if (dynamic_cast<const BoolProperty*>(prop)) {
            auto* check = qobject_cast<QCheckBox*>(widget);
            if (check) expr_->SetParameter(name, check->isChecked());
        }
        else if (dynamic_cast<const StringProperty*>(prop)) {
            auto* edit = qobject_cast<QLineEdit*>(widget);
            if (edit) expr_->SetParameter(name, edit->text().toStdString());
        }
        else if (dynamic_cast<const Vec3Property*>(prop)) {
            auto* container = qobject_cast<QWidget*>(widget);
            if (!container) continue;
            auto* hbox = container->layout();
            nlohmann::json jv = expr_->GetParameter(name);
            if (!jv.is_array()) jv = nlohmann::json::array({0, 0, 0});
            for (int i = 0; i < 3 && i < hbox->count(); ++i) {
                auto* spin = qobject_cast<QDoubleSpinBox*>(hbox->itemAt(i)->widget());
                if (spin) {
                    while ((int)jv.size() <= i) jv.push_back(0.0f);
                    jv[i] = (float)spin->value();
                }
            }
            expr_->SetParameter(name, jv);
        }
    }

    RefreshJsonPreview();
}

void ReflectionDemoWidget::RefreshJsonPreview() {
    // 把所有字段值序列化成 JSON，显示在底部
    nlohmann::json j;
    for (const Property* prop : expr_->GetParameters()) {
        j[prop->name] = expr_->GetParameter(prop->name);
    }

    std::stringstream ss;
    ss << std::setw(2) << j;   // 缩进 2 空格，pretty-print
    std::string raw = ss.str();

    // 转义 HTML 字符（让 QLabel 不会把 JSON 当富文本解析）
    QString html = QString::fromStdString(raw)
        .toHtmlEscaped()
        .replace(" ", "&nbsp;")
        .replace("\n", "<br>");
    // 用等宽字体显示
    html = "<pre style='margin:0'>" + html + "</pre>";
    jsonLabel_->setText(html);
}
```

**关键讲解**：

1. **`QOverload<double>::of(&QDoubleSpinBox::valueChanged)`**
   `valueChanged` 有两个重载（QString 版和 double 版）。`connect` 不知道用哪个，必须用 `QOverload` 显式指定。

2. **`textEdited` vs `textChanged`**
   `textEdited` 只在**用户输入**时触发；`textChanged` 在 `setText()` 程序化修改时也触发（会引发回环）。文本字段用 `textEdited`。

3. **`qobject_cast<T*>`**
   类似 `dynamic_cast`，但只支持 Qt 类，且需要 `Q_OBJECT`。比 `dynamic_cast` 快，是 Qt 的标准方式。

4. **数据驱动的 UI 构建**
   `BuildForm()` 全程**没有硬编码任何字段名**。换一个 Expression 子类（比如 7 个字段的 Constant），UI 自动适配。

### 4. 修改 main.cpp

```cpp
#include <QApplication>
#include "Demos/ReflectionDemo/ReflectionDemoWidget.h"
#include "Expression/Public/Expression.h"
#include "Reflection/Public/ReflectionMacros.h"
#include "MaterialGraph/Public/Types.h"
#include "Core/Public/MathTypes.h"
#include "Core/Public/Logger.h"
#include <string>
#include <vector>

// 测试用 Expression 子类：4 个不同类型的字段
class TestExpr : public Expression {
public:
    float       scale   = 1.0f;
    Vec3        color   = Vec3(1.0f, 0.0f, 0.0f);
    bool        enabled = true;
    std::string label   = "default";

    ME_BEGIN_CLASS(TestExpr)
        ME_DISPLAY_NAME("Test Expr")
        ME_CATEGORY("Demo")
        ME_CATEGORY_COLOR("#FF8800")
        ME_FIELD(TestExpr, scale,   1.0f)
        ME_FIELD(TestExpr, color,   Vec3(1.0f, 0.0f, 0.0f))
        ME_FIELD(TestExpr, enabled, true)
        ME_FIELD(TestExpr, label,   std::string("default"))
    ME_END_CLASS(TestExpr)

    std::vector<ExpressionPinDesc> GetInputPins()  const override { return {}; }
    std::vector<ExpressionPinDesc> GetOutputPins() const override { return {}; }
    std::vector<int32_t> Compile(MaterialCompiler*, Node*) const override { return {}; }
};

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    TestExpr expr;
    ReflectionDemoWidget widget(&expr);
    widget.show();

    return app.exec();
}
```

### 5. CMake 重新配置

新增了 `src/Demos/...` 目录，CMake 的 `GLOB_RECURSE` 不会自动发现新文件，必须重跑 configure：

```bash
cmake -B build -S .
cmake --build build --config Debug
```

---

## 预期效果

启动后窗口大致这样：

```
┌────────────────────────────────────────────────────────┐
│ Reflection Demo                                         │
├────────────────────────────────────────────────────────┤
│  类型：Test Expr   分类：Demo   颜色标记：#FF8800       │
├────────────────────────────────────────────────────────┤
│  scale   : [1.000 ↕]                                   │
│  color   : [1.000 ↕][0.000 ↕][0.000 ↕]                 │
│  enabled : [✓]                                          │
│  label   : [default                              ]     │
├────────────────────────────────────────────────────────┤
│  实时 JSON 状态：                                       │
│  ┌─────────────────────────────────────────────────┐  │
│  │ {                                                 │  │
│  │   "color": [1.0, 0.0, 0.0],                      │  │
│  │   "enabled": true,                               │  │
│  │   "label": "default",                            │  │
│  │   "scale": 1.0                                   │  │
│  │ }                                                 │  │
│  └─────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────┘
```

操作：
- 改 `scale` 的 SpinBox → 下方 JSON `"scale"` 字段立刻更新
- 改 `color` 三个分量 → JSON `"color"` 数组刷新
- 取消 `enabled` 复选框 → JSON `"enabled"` 变 false
- 改 `label` 文本 → 回车后 JSON `"label"` 刷新

---

## 验证清单

- [ ] 窗口正常显示，标题正确
- [ ] 4 个字段控件类型正确（Float→SpinBox, Vec3→3×SpinBox, Bool→CheckBox, String→LineEdit）
- [ ] 改任何控件 → SetParameter → JSON 立刻刷新
- [ ] **代码完全数据驱动**：把 TestExpr 加一个字段 `ME_FIELD(TestExpr, speed, 5.0f)`，重新编译运行，UI 自动多出一行 `speed` 编辑器，**不需要改 ReflectionDemoWidget 代码**
- [ ] 关闭窗口程序正常退出

最后一个验证项最关键 —— **不修改 widget 代码就能适应新字段**，这是反射的真正价值。

---

## UE5 参考

- `Engine/Source/Editor/PropertyEditor/Private/DetailItemTree.cpp` —— UE5 属性面板的核心
- `Engine/Source/Editor/DetailCustomizations/Private/DetailWidgetCreator.cpp` —— 根据字段类型（FProperty）生成对应 widget
- 搜索 `FProperty::GetPropertyFlags` —— UE5 字段元数据查询

UE5 的属性面板比我们的复杂 100 倍（嵌套对象、数组、回调、自定义 drawer），但**核心思路一样**：遍历字段元信息 → 根据类型生成控件 → 控件变化时写回。

---

## 完成后

做完这一课后，**继续课 6（编译器核心）**。

垂直切片的目的不是替代主线，是**让你看到反射在真实场景里干什么**。等以后写到课 13 的属性面板时，你会发现"原来我早就在课 5c 做过迷你版了"，那时只是把它做得更完整、更鲁棒、更美观。

如果你做完后想继续看更多反馈：
- 课 6 写完编译器后，回来加一个"编译此节点"按钮，调用 Expression::Compile，把 CodeChunk 打印到 JSON 预览下方
- 课 9 写完 HLSL 生成后，再加一个"生成 HLSL"按钮，把生成的着色器代码显示在另一个窗口

让这个垂直切片**跟着主线一起长大**。

---

## 完成标志

- [ ] ReflectionDemoWidget 编译通过
- [ ] 窗口显示 4 个字段控件，类型正确
- [ ] 编辑控件实时更新 JSON
- [ ] **数据驱动验证**：TestExpr 加字段，widget 不改代码也能正常显示
- [ ] 理解反射为什么能驱动 UI（FieldDesc 里的 type 字段决定 Qt 控件类型）
