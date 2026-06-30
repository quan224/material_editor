# 课13：面板实现（调色板、属性、代码预览）

## 目标

实现三个功能面板：节点调色板（选择添加节点）、属性面板（编辑节点参数）、代码预览面板（显示编译结果）。

---

## 背景知识

### UE5 面板体系

UE5 材质编辑器有三个核心面板：

1. **调色板**（Palette）：按类别分组的节点列表，搜索过滤，拖拽添加
2. **详情面板**（Details）：选中节点后显示可编辑参数
3. **统计/代码**（Stats/Source）：显示材质统计和 HLSL 代码

这些都用 QDockWidget 实现，可以自由拖拽、浮动、堆叠为标签页。

---

## 操作步骤

### 1. 创建文件

```
src/UI/Private/Panels/NodePalettePanel.h
src/UI/Private/Panels/NodePalettePanel.cpp
src/UI/Private/Panels/PropertyPanel.h
src/UI/Private/Panels/PropertyPanel.cpp
src/UI/Private/Panels/CodePreviewPanel.h
src/UI/Private/Panels/CodePreviewPanel.cpp
```

### 2. NodePalettePanel.h

```cpp
#pragma once
#include <QWidget>
#include <QTreeWidget>
#include <QLineEdit>
#include <QVBoxLayout>

class NodeFactory;

class NodePalettePanel : public QWidget {
    Q_OBJECT
public:
    NodePalettePanel(NodeFactory* factory, QWidget* parent = nullptr);

signals:
    void NodeTypeDoubleClicked(const std::string& typeName);

private:
    void BuildTree();
    void OnSearchChanged(const QString& text);

    QTreeWidget* tree_;
    QLineEdit* searchBox_;
    NodeFactory* factory_;
};
```

### 3. NodePalettePanel.cpp

```cpp
#include "UI/Private/Panels/NodePalettePanel.h"
#include "MaterialGraph/Public/NodeFactory.h"
#include <QLabel>
#include <QTreeWidgetItem>

NodePalettePanel::NodePalettePanel(NodeFactory* factory, QWidget* parent)
    : QWidget(parent), factory_(factory) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    // 搜索框
    searchBox_ = new QLineEdit;
    searchBox_->setPlaceholderText("Search nodes...");
    layout->addWidget(searchBox_);

    // 节点树
    tree_ = new QTreeWidget;
    tree_->setHeaderHidden(true);
    tree_->setIndentation(12);
    layout->addWidget(tree_);

    BuildTree();

    // 搜索过滤
    connect(searchBox_, &QLineEdit::textChanged,
            this, &NodePalettePanel::OnSearchChanged);

    // 双击添加
    connect(tree_, &QTreeWidget::itemDoubleClicked,
            this, [this](QTreeWidgetItem* item) {
        std::string typeName = item->data(0, Qt::UserRole).toString().toStdString();
        if (!typeName.empty()) {
            emit NodeTypeDoubleClicked(typeName);
        }
    });
}

void NodePalettePanel::BuildTree() {
    tree_->clear();

    auto types = factory_->GetAllTypes();

    // 按类别分组
    std::map<std::string, std::vector<NodeFactory::NodeTypeInfo>> byCategory;
    for (const auto& t : types) {
        byCategory[t.category].push_back(t);
    }

    // 类别颜色
    std::map<std::string, QColor> categoryColors = {
        {"Math", QColor(76, 167, 232)},
        {"Constants", QColor(142, 142, 142)},
        {"Parameters", QColor(142, 142, 142)},
        {"Texture", QColor(155, 89, 182)},
        {"Vector", QColor(76, 167, 232)},
        {"Utility", QColor(46, 204, 113)},
        {"ControlFlow", QColor(231, 76, 60)},
    };

    for (auto& [category, typeList] : byCategory) {
        auto* catItem = new QTreeWidgetItem(tree_);
        catItem->setText(0, QString::fromStdString(category));
        catItem->setExpanded(true);

        QFont catFont = catItem->font(0);
        catFont.setBold(true);
        catItem->setFont(0, catFont);

        QColor catColor = categoryColors.count(category)
                              ? categoryColors[category] : QColor(100, 100, 100);
        catItem->setForeground(0, catColor);

        for (const auto& typeInfo : typeList) {
            auto* nodeItem = new QTreeWidgetItem(catItem);
            nodeItem->setText(0, QString::fromStdString(typeInfo.displayName));
            nodeItem->setData(0, Qt::UserRole,
                              QString::fromStdString(typeInfo.typeName));
            nodeItem->setToolTip(0, QString::fromStdString(typeInfo.typeName));
        }
    }
}

void NodePalettePanel::OnSearchChanged(const QString& text) {
    // 遍历所有项，根据搜索文字过滤
    QString lowerText = text.toLower();

    for (int i = 0; i < tree_->topLevelItemCount(); i++) {
        auto* catItem = tree_->topLevelItem(i);
        bool hasVisibleChild = false;

        for (int j = 0; j < catItem->childCount(); j++) {
            auto* nodeItem = catItem->child(j);
            bool match = lowerText.isEmpty() ||
                         nodeItem->text(0).toLower().contains(lowerText);
            nodeItem->setHidden(!match);
            if (match) hasVisibleChild = true;
        }

        catItem->setHidden(!hasVisibleChild && !lowerText.isEmpty());
    }
}
```

### 4. PropertyPanel.h

```cpp
#pragma once
#include <QWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QMap>
#include <nlohmann/json.hpp>

class Node;
class Expression;
namespace reflection { struct FieldDesc; }

class PropertyPanel : public QWidget {
    Q_OBJECT
public:
    PropertyPanel(QWidget* parent = nullptr);

    // 设置要显示的节点
    void SetNode(Node* node);
    void Clear();

signals:
    void ParameterChanged();  // 参数被修改

private:
    void BuildUI();
    void CreateFloatEditor(const std::string& name, float value,
                           reflection::FieldDesc* param);
    void CreateVectorEditor(const std::string& name,
                            const std::vector<std::pair<std::string, float>>& components);

    QScrollArea* scrollArea_;
    QWidget* content_;
    QVBoxLayout* contentLayout_;
    Node* currentNode_ = nullptr;

    // 编辑器映射（参数名 → 控件）
    QMap<std::string, QDoubleSpinBox*> spinBoxes_;
};
```

### 5. PropertyPanel.cpp

```cpp
#include "UI/Private/Panels/PropertyPanel.h"
#include "MaterialGraph/Public/Node.h"
#include "Expression/Public/ExpressionRegistry.h"
#include "Expression/Public/Expression.h"
#include <QGroupBox>
#include <QFormLayout>
#include <QFrame>

PropertyPanel::PropertyPanel(QWidget* parent) : QWidget(parent) {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    scrollArea_ = new QScrollArea;
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    content_ = new QWidget;
    contentLayout_ = new QVBoxLayout(content_);
    contentLayout_->setContentsMargins(8, 8, 8, 8);
    contentLayout_->setSpacing(6);
    contentLayout_->addStretch();

    scrollArea_->setWidget(content_);
    mainLayout->addWidget(scrollArea_);
}

void PropertyPanel::SetNode(Node* node) {
    Clear();
    currentNode_ = node;
    if (!node) return;

    // 节点信息头
    auto* header = new QLabel(QString("<b>%1</b> <span style='color:gray'>(%2)</span>")
        .arg(QString::fromStdString(node->title))
        .arg(QString::fromStdString(node->typeName)));
    contentLayout_->insertWidget(0, header);

    // 分割线
    auto* line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    contentLayout_->insertWidget(1, line);

    // 获取 Expression 的参数定义
    auto expr = ExpressionRegistry::GetInstance().Create(node->typeName);
    if (!expr) return;

    auto params = expr->GetParameters();
    if (params.empty()) return;

    // 创建参数编辑区
    auto* paramGroup = new QGroupBox("Parameters");
    auto* formLayout = new QFormLayout(paramGroup);

    for (auto& param : params) {
        switch (param.type) {
            case reflection::FieldType::Float: {
                float val = 0.0f;
                if (node->parameters.contains(param.name) &&
                    node->parameters[param.name].is_number()) {
                    val = node->parameters[param.name].get<float>();
                }
                auto* spin = new QDoubleSpinBox;
                spin->setRange(-10000.0, 10000.0);
                spin->setDecimals(3);
                spin->setSingleStep(0.1);
                spin->setValue(val);
                spinBoxes_[param.name] = spin;

                formLayout->addRow(
                    QString::fromStdString(param.name), spin);

                // 值变化时更新节点参数
                connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                        this, [this, name = param.name](double val) {
                    if (currentNode_) {
                        currentNode_->parameters[name] = (float)val;
                        emit ParameterChanged();
                    }
                });
                break;
            }
            case reflection::FieldType::Float3: {
                // 三个浮点编辑器（R/G/B 或 X/Y/Z）
                auto* widget = new QWidget;
                auto* hLayout = new QHBoxLayout(widget);
                hLayout->setContentsMargins(0, 0, 0, 0);

                QStringList labels = {"R", "G", "B"};
                for (int i = 0; i < 3; i++) {
                    auto* spin = new QDoubleSpinBox;
                    spin->setRange(-10000.0, 10000.0);
                    spin->setDecimals(3);
                    spin->setSingleStep(0.1);
                    hLayout->addWidget(new QLabel(labels[i]));
                    hLayout->addWidget(spin);

                    std::string paramKey = labels[i].toStdString();
                    float val = 0.0f;
                    if (currentNode_->parameters.contains(paramKey) &&
                        currentNode_->parameters[paramKey].is_number()) {
                        val = currentNode_->parameters[paramKey].get<float>();
                    }
                    spin->setValue(val);
                    spinBoxes_[paramKey] = spin;

                    connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                            this, [this, key = paramKey](double val) {
                        if (currentNode_) {
                            currentNode_->parameters[key] = (float)val;
                            emit ParameterChanged();
                        }
                    });
                }
                formLayout->addRow(
                    QString::fromStdString(param.name), widget);
                break;
            }
            default:
                break;
        }
    }

    contentLayout_->insertWidget(2, paramGroup);
}

void PropertyPanel::Clear() {
    currentNode_ = nullptr;
    spinBoxes_.clear();

    // 删除所有子控件
    QLayoutItem* item;
    while ((item = contentLayout_->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }
    contentLayout_->addStretch();
}
```

### 6. CodePreviewPanel.h

```cpp
#pragma once
#include <QWidget>
#include <QPlainTextEdit>
#include <QSyntaxHighlighter>

class CodePreviewPanel : public QWidget {
    Q_OBJECT
public:
    CodePreviewPanel(QWidget* parent = nullptr);

    void SetCode(const std::string& code);
    void SetError(const std::string& errorMessage, int line = -1);
    void Clear();

private:
    QPlainTextEdit* textEdit_;
    std::unique_ptr<QSyntaxHighlighter> highlighter_;
};

// 简单的 HLSL 语法高亮
class HLSLHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit HLSLHighlighter(QTextDocument* parent);

protected:
    void highlightBlock(const QString& text) override;

private:
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    QVector<Rule> rules_;
};
```

### 7. CodePreviewPanel.cpp

```cpp
#include "UI/Private/Panels/CodePreviewPanel.h"
#include <QVBoxLayout>
#include <QFont>
#include <QRegularExpression>
#include <QTextCharFormat>

// ====== HLSL 语法高亮 ======

HLSLHighlighter::HLSLHighlighter(QTextDocument* parent)
    : QSyntaxHighlighter(parent) {
    // 关键字（HLSL，注意与 GLSL 的区别：HLSL 用 float2/3/4 而非 vec2/3/4）
    QTextCharFormat keywordFormat;
    keywordFormat.setForeground(QColor(86, 156, 214));  // 蓝色
    keywordFormat.setFontWeight(QFont::Bold);

    QStringList keywords = {
        "void", "float", "float2", "float3", "float4", "float4x4", "float3x3",
        "half", "int", "uint", "bool", "true", "false",
        "return", "if", "else", "for", "while", "switch", "case", "default",
        "const", "static", "struct", "cbuffer", "register", "packoffset",
        "SamplerState", "Texture2D", "TextureCube", "Buffer", "RWByteAddressBuffer",
        "groupshared", "numthreads", "in", "out", "inout"
    };
    for (const auto& kw : keywords) {
        Rule rule;
        rule.pattern = QRegularExpression("\\b" + kw + "\\b");
        rule.format = keywordFormat;
        rules_.append(rule);
    }

    // 内置函数（HLSL，注意 mix→lerp，fract→frac，texture→.Sample 等差异）
    QTextCharFormat funcFormat;
    funcFormat.setForeground(QColor(220, 180, 100));  // 黄色

    QStringList functions = {
        "normalize", "dot", "cross", "length", "lerp", "clamp", "saturate", "smoothstep",
        "pow", "sqrt", "rsqrt", "abs", "sin", "cos", "tan", "atan", "atan2", "exp", "log",
        "min", "max", "fmod", "step", "sign", "floor", "ceil", "frac",
        "reflect", "refract", "mul", "transpose", "inverse", "determinant",
        "ddx", "ddy", "all", "any", "tex2D", "tex2Dlod", "Sample", "SampleLevel"
    };
    for (const auto& fn : functions) {
        Rule rule;
        rule.pattern = QRegularExpression("\\b" + fn + "\\b");
        rule.format = funcFormat;
        rules_.append(rule);
    }

    // 数字
    QTextCharFormat numberFormat;
    numberFormat.setForeground(QColor(181, 206, 168));  // 浅绿

    Rule numRule;
    numRule.pattern = QRegularExpression("\\b[0-9]+\\.?[0-9]*([eE][+-]?[0-9]+)?[fFhH]?\\b");
    numRule.format = numberFormat;
    rules_.append(numRule);

    // 注释
    QTextCharFormat commentFormat;
    commentFormat.setForeground(QColor(106, 153, 85));  // 绿色

    Rule commentRule;
    commentRule.pattern = QRegularExpression("//[^\n]*");
    commentRule.format = commentFormat;
    rules_.append(commentRule);

    // 预处理器
    QTextCharFormat preprocFormat;
    preprocFormat.setForeground(QColor(155, 120, 200));  // 紫色

    Rule preprocRule;
    preprocRule.pattern = QRegularExpression("#[^\n]*");
    preprocRule.format = preprocFormat;
    rules_.append(preprocRule);
}

void HLSLHighlighter::highlightBlock(const QString& text) {
    for (const auto& rule : rules_) {
        auto it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            auto match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(),
                      rule.format);
        }
    }
}

// ====== CodePreviewPanel ======

CodePreviewPanel::CodePreviewPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    textEdit_ = new QPlainTextEdit;
    textEdit_->setReadOnly(true);
    textEdit_->setFont(QFont("Consolas", 10));
    textEdit_->setLineWrapMode(QPlainTextEdit::NoWrap);

    // 语法高亮
    highlighter_ = std::make_unique<HLSLHighlighter>(textEdit_->document());

    layout->addWidget(textEdit_);
}

void CodePreviewPanel::SetCode(const std::string& code) {
    textEdit_->setPlainText(QString::fromStdString(code));
    textEdit_->setStyleSheet("");  // 清除错误样式
}

void CodePreviewPanel::SetError(const std::string& errorMessage, int line) {
    textEdit_->setPlainText(QString::fromStdString(errorMessage));
    textEdit_->setStyleSheet("color: red;");

    if (line >= 0) {
        // 高亮错误行（简单方案：在文本中标注）
        QTextCursor cursor = textEdit_->textCursor();
        cursor.movePosition(QTextCursor::Start);
        for (int i = 0; i < line && !cursor.atEnd(); i++) {
            cursor.movePosition(QTextCursor::Down);
        }
        cursor.select(QTextCursor::LineUnderCursor);
        QTextCharFormat fmt;
        fmt.setBackground(QColor(255, 100, 100, 80));
        cursor.mergeCharFormat(fmt);
    }
}

void CodePreviewPanel::Clear() {
    textEdit_->clear();
    textEdit_->setStyleSheet("");
}
```

### 8. 集成到 MainWindow

修改 `MainWindow.cpp`，将占位替换为真实面板：

```cpp
#include "UI/Private/MaterialGraphWidget.h"
#include "UI/Private/Panels/NodePalettePanel.h"
#include "UI/Private/Panels/PropertyPanel.h"
#include "UI/Private/Panels/CodePreviewPanel.h"

void MainWindow::SetupDockWidgets() {
    // 中央：画布
    graphWidget_ = new MaterialGraphWidget(graph_, factory_, this);
    setCentralWidget(graphWidget_);

    // 左侧：节点调色板
    palettePanel_ = new NodePalettePanel(factory_);
    auto* paletteDock = new QDockWidget("Palette", this);
    paletteDock->setWidget(palettePanel_);
    addDockWidget(Qt::LeftDockWidgetArea, paletteDock);

    // 右侧：属性面板
    propertyPanel_ = new PropertyPanel();
    auto* propertyDock = new QDockWidget("Details", this);
    propertyDock->setWidget(propertyPanel_);
    addDockWidget(Qt::RightDockWidgetArea, propertyDock);

    // 底部：代码预览
    codePanel_ = new CodePreviewPanel();
    auto* codeDock = new QDockWidget("HLSL Code", this);
    codeDock->setWidget(codePanel_);
    addDockWidget(Qt::BottomDockWidgetArea, codeDock);

    // 连接信号
    // 调色板双击 → 在画布中心添加节点
    connect(palettePanel_, &NodePalettePanel::NodeTypeDoubleClicked,
            this, [this](const std::string& typeName) {
        QPointF center = graphWidget_->mapToScene(
            graphWidget_->viewport()->rect().center());
        auto node = factory_->Create(typeName, center);
        if (node) graph_->AddNode(node);
    });

    // 图变化时重新编译
    connect(graph_, &Graph::GraphChanged, this, &MainWindow::OnCompile);

    // 参数修改时重新编译
    connect(propertyPanel_, &PropertyPanel::ParameterChanged,
            this, &MainWindow::OnCompile);
}

// 选中节点时更新属性面板
// 在 MaterialGraphWidget 中添加 selectionChanged 信号处理
// 或在 MainWindow 中监听 scene 的 selectionChanged
```

---

## 验证

1. 运行程序
2. 左侧调色板显示按类别分组的节点列表
3. 搜索框输入 "add" → 过滤显示 Add 节点
4. 双击调色板中的 "Constant" → 画布中出现 Constant 节点
5. 选中 Constant 节点 → 右侧属性面板显示 Value 编辑器
6. 修改 Value → 图变化 → 自动编译
7. 底部代码面板显示生成的 GLSL（带语法高亮）
8. 选中 Add 节点 → 属性面板无参数（Add 没有可编辑参数）

---

## UE5 参考

- `E:\UE5\Engine\Source\Editor\MaterialEditor\Private\SMaterialPalette.cpp` — 调色板
- `E:\UE5\Engine\Source\Editor\MaterialEditor\Private\MaterialEditorDetailCustomization.cpp` — 详情面板
- `E:\UE5\Engine\Source\Editor\PropertyEditor\Private\PropertyEditor.cpp` — 属性编辑器框架

---

## 完成标志

- [ ] 调色板按类别显示节点，搜索过滤正常
- [ ] 双击调色板添加节点到画布
- [ ] 选中节点后属性面板显示参数编辑器
- [ ] 修改参数后自动触发重编译
- [ ] 代码预览面板显示 GLSL 并有语法高亮
- [ ] 编译错误时代码面板标红显示
