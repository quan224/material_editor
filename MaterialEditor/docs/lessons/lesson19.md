# 课19：错误处理 UI + 撤销重做

## 目标

实现两块编辑器能力：

1. **错误面板 UI**（Part A）：把课 6 编译器收集的 `CompileError` 数组渲染成可视化反馈——错误列表 Dock + 节点高亮（按严重级别染色）+ 引脚标红（精确到出错 pin）+ 点击错误条目跳到对应节点。
2. **撤销/重做系统**（Part B）：基于命令模式（Command Pattern）的 `CommandStack` + 5 个具体 Command（添加 / 删除 / 连接 / 移动 / 改参）。
3. **编辑器层错误桥接**（Part C）：`GraphEditor::CompileAndShowErrors()`——调 `MaterialCompiler::Compile(graph)` → 把 `CompileResult::errors` 推给 Part A 的 UI。

**关键边界**（不挖坑原则）：
- 课 6 已经把编译器层错误诊断做完整了（`CompileError` 结构 + `EmitError` 收集器 + `errors_` 数组 + `GetErrors()` const 访问器都 public 暴露）。**课 19 是课 6 的"消费者"，不修改 `MaterialCompiler` 类**——只读 `errors` 数组渲染 UI。
- 课 19 也不重新定义 `CompileError` / `EErrorSeverity`（那是 `Compiler/Public/CompileError.h` 的事，课 6 已完成）。

---

## Part A：错误面板 UI

### 背景知识：错误展示设计

课 6 的编译器产出的 `CompileError` 结构：

```cpp
struct CompileError {
    UUID            nodeId;       // 出错节点
    std::string     pinName;      // 出错引脚（可空——节点级错误如环检测）
    std::string     message;      // 用户可读描述
    EErrorSeverity  severity;     // Error / Warning / Info
};
```

UI 设计三件套：

| UI 元素 | 数据来源 | 作用 |
|---------|---------|------|
| **错误列表 Dock**（QDockWidget + QListWidget）| `MaterialCompiler::GetErrors()` | 列出所有错误，每条一行带图标（红/黄/蓝）+ 节点名 + 引脚名 + 消息 |
| **节点标题栏染色** | 按 `nodeId` 聚合，取最严重的 severity | Error=红 / Warning=黄 / Info=蓝。一个节点可能有多个错误（如类型不匹配 + 缺连），取最严重的染色，避免 Error 被同节点的 Warning 盖掉 |
| **出错引脚标红** | 按 `nodeId + pinName` 精确定位 | 节点级错误（环检测，`pinName` 空）不标 pin；引脚级错误（类型不匹配）只标出错的那个 pin，不是整节点一片红 |
| **点击错误条目跳转** | 错误条目存 `nodeId` + `pinName` | 点击 → 视图居中到节点 + 选中节点 + 高亮出错 pin |

**对照 UE**：UE 的 `SGraphNodeMaterial` 根据 `UMaterialExpression::LastErrorText` 给节点染色，`MaterialEditor.cpp` 的错误列表 Dock 显示编译错误。`HandleMaterialCompilationErrors` 把编译器错误回填到 expression 的 `LastErrorText`。

> **关键差异**：UE 编译时错误**不带 nodeId**，要回填到 expression 对象（"后处理"）；我们课 6 编译时**直接带 nodeId**——`CompileInputPin` 入口设 `current_node_`，`EmitError` 直接定位。**课 19 省去了"编译器错误 → 表达式对象"的回填层**，直接拿 `nodeId` 渲染。

### 操作步骤

#### A.1 新文件

```
src/Editor/Public/ErrorListDock.h
src/Editor/Private/ErrorListDock.cpp
src/Editor/Public/NodeErrorHighlight.h       # 节点/引脚染色 helper
src/Editor/Private/NodeErrorHighlight.cpp
```

#### A.2 `ErrorListDock.h`（错误列表 Dock）

```cpp
#pragma once
#include <QDockWidget>
#include <QListWidget>
#include <QHash>
#include "Compiler/Public/CompileError.h"
#include "Core/Public/UUID.h"

class GraphScene;        // QGraphicsView 派生类（课 11）
class GraphEditor;       // 主编辑视图

// 错误严重级别 → 图标 + 颜色（QListWidgetItem role）
struct ErrorVisual {
    QIcon  icon;
    QColor color;
};

class ErrorListDock : public QDockWidget {
    Q_OBJECT
public:
    explicit ErrorListDock(QWidget* parent = nullptr);

    // 设置错误列表（编译完成后调）
    // 同时按 nodeId 聚合，通知节点高亮系统
    void SetErrors(const std::vector<CompileError>& errors);

    // 清空（关闭材质 / 新建材质时调）
    void Clear();

signals:
    // 用户点击一条错误 → 发这个信号，GraphEditor 接收后跳转
    void JumpToNode(const UUID& nodeId, const std::string& pinName);

private slots:
    void OnItemSelected(int row);    // QListWidget itemClicked → 发 JumpToNode

private:
    QListWidget* list_ = nullptr;

    // 错误索引：row → CompileError（点击时查回 nodeId/pinName）
    std::vector<CompileError> indexedErrors_;

    // 视觉查找表（按 severity 给图标 + 颜色）
    const ErrorVisual& VisualFor(EErrorSeverity sev) const;
};
```

#### A.3 `ErrorListDock.cpp`

```cpp
#include "Editor/Public/ErrorListDock.h"
#include <QAction>
#include <QApplication>

static ErrorVisual MakeVisual(const char* resourcePath, QColor color) {
    return { QIcon(resourcePath), color };
}

ErrorListDock::ErrorListDock(QWidget* parent)
    : QDockWidget("Errors", parent) {
    list_ = new QListWidget(this);
    list_->setIconSize(QSize(16, 16));
    setWidget(list_);

    connect(list_, &QListWidget::currentRowChanged,
            this, &ErrorListDock::OnItemSelected);
}

void ErrorListDock::SetErrors(const std::vector<CompileError>& errors) {
    list_->clear();
    indexedErrors_ = errors;

    for (int i = 0; i < (int)errors.size(); ++i) {
        const auto& e = errors[i];
        const auto& vis = VisualFor(e.severity);

        // 拼条目文字：[节点标题] pin 名: message
        // （节点标题查找留给 GraphEditor 注入的 GetNodeTitle 函数；这里先用 UUID 简写）
        QString text = QString::fromStdString(
            "[" + e.nodeId.ToString().substr(0, 8) + "] "
            + (e.pinName.empty() ? std::string("") : (e.pinName + ": "))
            + e.message);

        auto* item = new QListWidgetItem(vis.icon, text, list_);
        item->setForeground(vis.color);
        // 存 row 索引到 item data，便于点击时取回
        item->setData(Qt::UserRole, i);
        list_->addItem(item);
    }
}

void ErrorListDock::Clear() {
    list_->clear();
    indexedErrors_.clear();
}

void ErrorListDock::OnItemSelected(int row) {
    if (row < 0 || row >= (int)indexedErrors_.size()) return;
    const auto& e = indexedErrors_[row];
    emit JumpToNode(e.nodeId, e.pinName);
}
```

> **节点标题查找**：上面用 UUID 简写显示（`substr(0, 8)`）。如果想错误信息更友好（显示节点 title 而不是 UUID），可以让 `ErrorListDock` 持一个 `std::function<std::string(const UUID&)>` 类型的 `nodeTitleLookup`（由 `GraphEditor` 注入 `graph_->FindNode(id)->title`）。这是 UI 层的便利函数，不属于编译器层。

#### A.4 `NodeErrorHighlight.h`（节点 + 引脚染色）

```cpp
#pragma once
#include <QColor>
#include <unordered_map>
#include "Compiler/Public/CompileError.h"
#include "Core/Public/UUID.h"

// 给定一组 CompileError，按 nodeId 聚合出每个节点的"最严重 severity"
// 和每个 (nodeId, pinName) 的"是否出错"——供 GraphEditor 在 paint 时用
class NodeErrorHighlight {
public:
    // 重新计算（每次编译后调一次）
    void RebuildFrom(const std::vector<CompileError>& errors);

    // 节点级：返回该节点的最严重 severity（用于标题栏背景色）
    // 没有错误返回 nullopt（节点不被染色）
    std::optional<EErrorSeverity> NodeSeverity(const UUID& nodeId) const;

    // 引脚级：该 (nodeId, pinName) 是否在错误列表里
    bool IsPinErrored(const UUID& nodeId, const std::string& pinName) const;

    // severity → 颜色（QGraphicsRectItem 的 brush 用）
    static QColor ColorFor(EErrorSeverity sev);

    void Clear() {
        nodeSeverity_.clear();
        erroredPins_.clear();
    }

private:
    // nodeId → 最严重的 severity
    std::unordered_map<UUID, EErrorSeverity> nodeSeverity_;

    // (nodeId, pinName) → true 的集合（用 pair<string, string> 作 key 简化 hash）
    // 实现细节：UUID 已经有 hash，pinName 是 string；用 std::unordered_map<UUID, std::set<std::string>>
    std::unordered_map<UUID, std::set<std::string>> erroredPins_;
};
```

```cpp
#include "Editor/Public/NodeErrorHighlight.h"

// EErrorSeverity 默认是 enum class，要给它定义 < 运算符让 std::min 工作
// （enum class 不允许隐式转 int，但可以显式比较）
static int SeverityRank(EErrorSeverity s) {
    switch (s) {
        case EErrorSeverity::Error:   return 0;   // 最严重
        case EErrorSeverity::Warning: return 1;
        case EErrorSeverity::Info:    return 2;
    }
    return 99;
}

void NodeErrorHighlight::RebuildFrom(const std::vector<CompileError>& errors) {
    Clear();

    for (const auto& e : errors) {
        // 节点级聚合：取最严重
        auto it = nodeSeverity_.find(e.nodeId);
        if (it == nodeSeverity_.end()) {
            nodeSeverity_[e.nodeId] = e.severity;
        } else if (SeverityRank(e.severity) < SeverityRank(it->second)) {
            it->second = e.severity;   // 更新为更严重的
        }

        // 引脚级：记下出错的 (nodeId, pinName)
        if (!e.pinName.empty()) {
            erroredPins_[e.nodeId].insert(e.pinName);
        }
    }
}

std::optional<EErrorSeverity> NodeErrorHighlight::NodeSeverity(const UUID& nodeId) const {
    auto it = nodeSeverity_.find(nodeId);
    if (it == nodeSeverity_.end()) return std::nullopt;
    return it->second;
}

bool NodeErrorHighlight::IsPinErrored(const UUID& nodeId, const std::string& pinName) const {
    auto it = erroredPins_.find(nodeId);
    if (it == erroredPins_.end()) return false;
    return it->second.count(pinName) > 0;
}

QColor NodeErrorHighlight::ColorFor(EErrorSeverity sev) {
    switch (sev) {
        case EErrorSeverity::Error:   return QColor(220,  80,  80);   // 红
        case EErrorSeverity::Warning: return QColor(220, 180,  80);   // 黄
        case EErrorSeverity::Info:    return QColor( 80, 140, 220);   // 蓝
    }
    return QColor(128, 128, 128);
}
```

**为什么节点级取最严重**：一个 Add 节点可能同时有 Error（类型不匹配）+ Warning（隐式窄化）。如果分别染色就乱了；取最严重（Error → 红）让用户一眼看到"这节点有问题需修"。`SeverityRank` 用 `enum class` 显式转 int（不允许隐式，但要比较）。

#### A.5 在 `GraphEditor` / 节点 `paint` 里用染色

```cpp
// GraphEditor.h 添加成员
NodeErrorHighlight errorHighlight_;
ErrorListDock*     errorDock_ = nullptr;

// 编译完成后（Part C）：
void GraphEditor::OnCompileFinished(const MaterialCompiler::CompileResult& result) {
    errorHighlight_.RebuildFrom(result.errors);
    errorDock_->SetErrors(result.errors);
    scene_->invalidate();   // 触发重绘
}

// 节点 paint（在 NodeGraphicsItem 里）：
void NodeGraphicsItem::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) {
    auto sev = graphEditor_->errorHighlight_.NodeSeverity(node_->id);
    QColor titleColor = sev.has_value()
                      ? NodeErrorHighlight::ColorFor(*sev)
                      : QColor(60, 60, 60);   // 默认深灰
    // 画标题栏背景...
    p->fillRect(titleRect, titleColor);

    // 引脚标红：画每个 pin 时查 IsPinErrored
    for (const auto& pin : node_->inputPins) {
        bool err = graphEditor_->errorHighlight_.IsPinErrored(node_->id, pin.name);
        QColor pinColor = err ? QColor(255, 60, 60) : NormalPinColor(pin.type);
        // 画 pin 圆点...
    }
}

// 点击错误条目跳转：
connect(errorDock_, &ErrorListDock::JumpToNode, this, [this](const UUID& id, const std::string& pin) {
    if (auto* item = FindNodeGraphicsItem(id)) {
        scene_->clearSelection();
        item->setSelected(true);
        centerOn(item);          // 视图居中到节点
        // 高亮 pin（item 内部根据 errorHighlight_ 已经标红，无需额外动作）
    }
});
```

---

## Part B：撤销/重做系统

### 背景知识：命令模式（Command Pattern）

经典的 Command Pattern：

```
用户操作 → 创建 Command 对象 → 执行并压入 undo 栈
                ↓
            Ctrl+Z → 从 undo 栈弹出 → 执行 Undo() → 压入 redo 栈
            Ctrl+Y → 从 redo 栈弹出 → 执行 Redo() → 压入 undo 栈
```

需要支持的操作：
- 添加节点
- 删除节点
- 连接引脚
- 断开连接
- 移动节点
- 修改参数

### B.1 新文件

```
src/UndoRedo/Public/Command.h
src/UndoRedo/Public/CommandStack.h
src/UndoRedo/Private/CommandStack.cpp
src/UndoRedo/Private/Commands/AddNodeCommand.h
src/UndoRedo/Private/Commands/RemoveNodeCommand.h
src/UndoRedo/Private/Commands/ConnectCommand.h
src/UndoRedo/Private/Commands/MoveNodeCommand.h
src/UndoRedo/Private/Commands/ChangeParameterCommand.h
```

### B.2 `Command.h`（基类）

```cpp
#pragma once
#include <string>
#include <memory>

class Command {
public:
    virtual ~Command() = default;

    virtual void Execute() = 0;   // 执行（首次或重做）
    virtual void Undo() = 0;      // 撤销

    virtual std::string GetName() const = 0;  // 用于显示
};

using CommandPtr = std::unique_ptr<Command>;
```

### B.3 `CommandStack.h` / `cpp`

```cpp
#pragma once
#include "UndoRedo/Public/Command.h"
#include <vector>
#include <stack>
#include <functional>

class CommandStack {
public:
    CommandStack() = default;

    // 执行命令并压入 undo 栈
    void Execute(CommandPtr cmd);

    // 撤销
    bool Undo();

    // 重做
    bool Redo();

    // 清空所有历史
    void Clear();

    // 状态查询
    bool CanUndo() const { return !undoStack_.empty(); }
    bool CanRedo() const { return !redoStack_.empty(); }

    // 回调（UI 更新用）
    std::function<void()> onStackChanged;

private:
    std::stack<CommandPtr> undoStack_;
    std::stack<CommandPtr> redoStack_;
};
```

```cpp
#include "UndoRedo/Public/CommandStack.h"

void CommandStack::Execute(CommandPtr cmd) {
    cmd->Execute();
    undoStack_.push(std::move(cmd));

    // 新操作清空 redo 栈
    while (!redoStack_.empty()) redoStack_.pop();

    if (onStackChanged) onStackChanged();
}

bool CommandStack::Undo() {
    if (undoStack_.empty()) return false;

    auto cmd = std::move(undoStack_.top());
    undoStack_.pop();
    cmd->Undo();
    redoStack_.push(std::move(cmd));

    if (onStackChanged) onStackChanged();
    return true;
}

bool CommandStack::Redo() {
    if (redoStack_.empty()) return false;

    auto cmd = std::move(redoStack_.top());
    redoStack_.pop();
    cmd->Execute();
    undoStack_.push(std::move(cmd));

    if (onStackChanged) onStackChanged();
    return true;
}

void CommandStack::Clear() {
    while (!undoStack_.empty()) undoStack_.pop();
    while (!redoStack_.empty()) redoStack_.pop();
    if (onStackChanged) onStackChanged();
}
```

### B.4 `AddNodeCommand.h`

```cpp
#pragma once
#include "UndoRedo/Public/Command.h"
#include "MaterialGraph/Public/Graph.h"
#include "MaterialGraph/Public/NodeFactory.h"
#include "Core/Public/RefCounted.h"

class AddNodeCommand : public Command {
public:
    AddNodeCommand(Graph* graph, const Ref<Node>& node)
        : graph_(graph), node_(node) {}

    void Execute() override {
        graph_->AddNode(node_);
    }

    void Undo() override {
        graph_->RemoveNode(node_->id);
    }

    std::string GetName() const override {
        return "Add " + node_->title;
    }

private:
    Graph* graph_;
    Ref<Node> node_;
};
```

### B.5 `RemoveNodeCommand.h`

```cpp
#pragma once
#include "UndoRedo/Public/Command.h"
#include "MaterialGraph/Public/Graph.h"
#include "Core/Public/RefCounted.h"

class RemoveNodeCommand : public Command {
public:
    RemoveNodeCommand(Graph* graph, const Ref<Node>& node)
        : graph_(graph), node_(node) {}

    void Execute() override {
        // 保存连接信息以便恢复
        savedConnections_.clear();
        // 遍历图中所有连接，找到与该节点相关的连接
        // （需要在 Graph 中提供查询方法）
        graph_->RemoveNode(node_->id);
    }

    void Undo() override {
        graph_->AddNode(node_);
        // 恢复连接
        for (auto& [srcPinId, tgtPinId] : savedConnections_) {
            graph_->Connect(srcPinId, tgtPinId);
        }
    }

    std::string GetName() const override {
        return "Remove " + node_->title;
    }

private:
    Graph* graph_;
    Ref<Node> node_;
    std::vector<std::pair<UUID, UUID>> savedConnections_;
};
```

### B.6 `ConnectCommand.h`

```cpp
#pragma once
#include "UndoRedo/Public/Command.h"
#include "MaterialGraph/Public/Graph.h"

class ConnectCommand : public Command {
public:
    ConnectCommand(Graph* graph, const UUID& srcPinId, const UUID& tgtPinId)
        : graph_(graph), srcPinId_(srcPinId), tgtPinId_(tgtPinId) {}

    void Execute() override {
        // Graph::Connect 不返回 connectionId（课3 设计），所以撤销时需要遍历
        // GetConnections() 找到刚建立的那条。这里先调 Connect，再扫一遍找新增的
        // （另一种设计：改 Graph::Connect 返回 UUID，更高效更准确——可作重构练习）
        graph_->Connect(srcPinId_, tgtPinId_);
        // 找到刚建立的连接，记下 connectionId 供 Undo 使用
        for (const auto& [cid, conn] : graph_->GetConnections()) {
            if (conn.sourcePinId == srcPinId_ && conn.targetPinId == tgtPinId_) {
                connectionId_ = cid;
                break;
            }
        }
    }

    void Undo() override {
        if (connectionId_.IsValid()) {
            graph_->Disconnect(connectionId_);
        }
    }

    std::string GetName() const override {
        return "Connect pins";
    }

private:
    Graph* graph_;
    UUID srcPinId_;
    UUID tgtPinId_;
    UUID connectionId_;  // Execute 后填充，Undo 用
};
```

### B.7 `MoveNodeCommand.h`

```cpp
#pragma once
#include "UndoRedo/Public/Command.h"
#include "MaterialGraph/Public/Node.h"

class MoveNodeCommand : public Command {
public:
    MoveNodeCommand(Node* node, const QPointF& oldPos, const QPointF& newPos)
        : node_(node), oldPos_(oldPos), newPos_(newPos) {}

    void Execute() override {
        node_->position = newPos_;
    }

    void Undo() override {
        node_->position = oldPos_;
    }

    std::string GetName() const override {
        return "Move " + node_->title;
    }

private:
    Node* node_;
    QPointF oldPos_;
    QPointF newPos_;
};
```

### B.8 `ChangeParameterCommand.h`

```cpp
#pragma once
#include "UndoRedo/Public/Command.h"
#include "MaterialGraph/Public/Node.h"
#include <nlohmann/json.hpp>

class ChangeParameterCommand : public Command {
public:
    ChangeParameterCommand(Node* node, const std::string& paramName,
                            const nlohmann::json& oldValue,
                            const nlohmann::json& newValue)
        : node_(node), paramName_(paramName),
          oldValue_(oldValue), newValue_(newValue) {}

    void Execute() override {
        node_->parameters[paramName_] = newValue_;
    }

    void Undo() override {
        node_->parameters[paramName_] = oldValue_;
    }

    std::string GetName() const override {
        return "Change " + paramName_;
    }

private:
    Node* node_;
    std::string paramName_;
    nlohmann::json oldValue_;
    nlohmann::json newValue_;
};
```

### B.9 集成到 MainWindow

```cpp
// MainWindow.h 添加成员
#include "UndoRedo/Public/CommandStack.h"

class MainWindow : public QMainWindow {
    // ...
private:
    CommandStack commandStack_;

    // 修改 Edit 菜单
    void SetupMenuBar() {
        // ...
        auto* editMenu = menuBar()->addMenu("&Edit");

        auto* undoAction = editMenu->addAction("&Undo");
        undoAction->setShortcut(QKeySequence::Undo);
        connect(undoAction, &QAction::triggered, this, [this]() {
            if (commandStack_.Undo()) {
                OnCompile();  // 重新编译
            }
        });

        auto* redoAction = editMenu->addAction("&Redo");
        redoAction->setShortcut(QKeySequence::Redo);
        connect(redoAction, &QAction::triggered, this, [this]() {
            if (commandStack_.Redo()) {
                OnCompile();
            }
        });

        // 更新菜单状态
        commandStack_.onStackChanged = [this]() {
            // 更新 Undo/Redo 按钮的 enabled 状态
        };
    }

    // 修改图操作，通过命令栈
    void AddNodeToGraph(const std::string& typeName, const QPointF& pos) {
        auto node = factory_->Create(typeName, pos);
        if (node) {
            commandStack_.Execute(
                std::make_unique<AddNodeCommand>(graph_, node));
        }
    }
};
```

---

## Part C：编辑器层错误桥接

### C.1 `GraphEditor::CompileAndShowErrors()`

编辑器主类 `GraphEditor` 在用户每次"编译"按钮 / 自动编译触发时，调一次编译器并把结果推给 UI：

```cpp
// GraphEditor.h
class GraphEditor : public QObject {
    Q_OBJECT
public:
    // 编译入口（"Compile" 按钮触发 / 自动编译定时器触发）
    void CompileAndShowErrors();

private:
    MaterialCompiler  compiler_;
    ErrorListDock*    errorDock_;
    NodeErrorHighlight errorHighlight_;
    GraphScene*       scene_;        // QGraphicsScene 派生
};

// GraphEditor.cpp
void GraphEditor::CompileAndShowErrors() {
    auto result = compiler_.Compile(graph_.get());   // 课 7 实现的入口

    // 把错误推给 UI（Part A）
    errorHighlight_.RebuildFrom(result.errors);
    errorDock_->SetErrors(result.errors);

    // 失败时也保留部分 HLSL（课 6 设计：错误分支短路，其他分支正常）
    codePanel_->SetCode(result.hlsl_code);
    codePanel_->SetErrorState(!result.success);   // 失败时面板边框染红

    scene_->invalidate();   // 触发重绘（让节点按新 errors 染色）
}
```

**关键**：
- `MaterialCompiler` 实例由 `GraphEditor` 拥有——这是 UI 层对编译器层的单向依赖（`GraphEditor` → `MaterialCompiler`），符合架构分层。
- **不修改 `MaterialCompiler` 类**——课 19 只调用课 6 已暴露的 public API（`Compile` / `CompileResult::errors` / `GetErrors`）。课 6 把编译器层错误诊断做完整后，课 19 是纯消费者。
- 编译失败也显示代码：课 6 的 `CompileResult` 设计是"即使有 Error 也填部分 HLSL"，这里直接显示——用户对照错误信息和部分代码能更快定位问题。

### C.2 错误条目点击跳转完整流程

```cpp
// GraphEditor 构造函数里
connect(errorDock_, &ErrorListDock::JumpToNode,
        this, [this](const UUID& id, const std::string& pin) {
    // 1. 找到节点的 graphics item
    auto* item = FindNodeGraphicsItem(id);
    if (!item) return;   // 节点可能已被删除

    // 2. 视图居中 + 选中
    auto* view = currentView_;
    view->clearSelection();
    item->setSelected(true);
    view->centerOn(item);

    // 3. 引脚级高亮（如果 pinName 非空）
    // NodeGraphicsItem 在 paint 时根据 errorHighlight_ 已经标红，无需额外动作；
    // 这里可以加一个临时闪烁动画让用户更容易注意到具体 pin（可选）
    if (!pin.empty()) {
        item->FlashPin(pin);   // 自定义方法：临时 200ms 高亮闪烁
    }
});
```

---

## 验证

### 错误面板 UI

1. **类型不匹配**：连一个 `Float2` 输出到 `Add` 节点的 A 引脚 + `Float3` 输出到 B 引脚 → 编译 → 错误面板出现一条 Error，节点 Add 标题栏染红，A 和 B 两个 pin 都标红
2. **除零**：`Divide` 的 B 引脚连 `Constant(0)` → 编译 → 错误面板出现一条 Warning（黄色），`Divide` 节点标题染黄，B pin 标红
3. **多错误**：同时构造 3 个错误 → 一次编译 → 错误面板出现 3 条 → 节点按最严重 severity 染色
4. **点击跳转**：滚动视图让错误节点出屏 → 点击错误面板条目 → 视图居中到节点 → 选中节点 → 错误 pin 闪烁
5. **新建材质**：清空图 → 错误面板清空 → 所有节点恢复默认色

### 撤销 / 重做

1. 添加一个节点 → Ctrl+Z → 节点消失 → Ctrl+Y → 节点恢复
2. 连接两个引脚 → Ctrl+Z → 连线消失 → Ctrl+Y → 连线恢复
3. 移动节点 → Ctrl+Z → 节点回到原位
4. 修改参数 → Ctrl+Z → 参数恢复原值
5. 多步操作 → 连续 Ctrl+Z → 连续 Ctrl+Y

### 端到端

撤销 / 重做后自动触发 `CompileAndShowErrors()`——错误面板 + 节点染色 + 代码面板同步更新。

---

## UE5 参考（相对 `Engine/` 路径）

### 错误 UI 对照

| 我们的 | UE 对应 | Engine 路径 |
|--------|---------|------------|
| `ErrorListDock`（QListWidget）| `IMaterialEditor::MaterialEditorUpgradeConflictsUI` / 错误列表 Dock | `Engine/Source/Editor/MaterialEditor/Private/MaterialEditor.cpp` |
| `NodeErrorHighlight::NodeSeverity` 染色 | `SGraphNodeMaterial` 根据 `LastErrorText` 染色 | `Engine/Source/Editor/MaterialEditor/Private/SGraphNodeMaterial.cpp` |
| 引脚级标红 | UE 是节点级染色（无 pin 级高亮） | — |
| `JumpToNode` 跳转 | `MaterialEditor::FocusError` | `Engine/Source/Editor/MaterialEditor/Private/MaterialEditor.cpp` |

> **关键差异**：UE 错误**不带 pin 概念**（UMaterialExpression 是类，输入是命名成员 `FExpressionInput`，错误格式 `"line N: expression ClassName: error text"`），所以 UE 只能节点级染色；**我们 pin 一等公民**（`Pin::name`），错误能精确到引脚，UI 引脚级高亮更直接。

### 撤销 / 重做对照

| 我们的（命令模式）| UE（事务 + 对象快照）| 作用 |
|----------------|---------------------|------|
| `ICommand`（Do/Undo 虚函数）| `FUndoableTransaction` | 可撤销操作 |
| `UndoStack`（命令栈）| `GEditor->UndoTransaction` / `RedoTransaction` | 撤销栈 |
| 手写每个命令的 Do/Undo | `FScopedTransaction` + 对象 `Serialize` 快照 | 记录变更 |

**两个关键差异**：

1. **UE 用事务 + 对象快照**（`FScopedTransaction` 开启事务 → 修改 UObject → 事务自动记录修改前/后状态 → Undo 时恢复快照）。我们用**命令模式**（每个操作一个 `ICommand`，手写 Do/Undo）——更直接易懂，但每种操作（加节点/连线/改参数）都要写专门的命令类。

2. **UE 的撤销是通用的**（任何 UObject 修改都能撤销，因为反射 + 序列化自动记录）。我们的命令模式要为每种操作手写——代价是没有"通用撤销"，但好处是撤销逻辑显式、可控。

> **搜索关键词**（UE 源码）：`FScopedTransaction`、`GEditor->Undo`、`HandleMaterialCompilationErrors`、`SGraphNodeMaterial`、`LastErrorText`。

---

## 完成标志

**Part A：错误面板 UI**
- [ ] `ErrorListDock` Dock 窗口，QListWidget 列出所有错误
- [ ] 错误条目按 severity 显示不同图标 + 颜色（Error=红 / Warning=黄 / Info=蓝）
- [ ] `NodeErrorHighlight` 按 nodeId 聚合取最严重 severity
- [ ] 节点标题栏染色（按聚合后的 severity）
- [ ] 引脚级标红（`IsPinErrored(nodeId, pinName)` 精确高亮）
- [ ] 点击错误条目 → 视图居中到节点 + 选中 + pin 闪烁

**Part B：撤销/重做**
- [ ] Ctrl+Z 撤销最后操作
- [ ] Ctrl+Y 重做
- [ ] 撤销/重做对添加/删除/连接/移动/参数修改都有效
- [ ] 命令栈在新建材质时清空

**Part C：编辑器层桥接**
- [ ] `GraphEditor::CompileAndShowErrors()` 调编译器 + 推 errors 到 UI
- [ ] 编译失败也保留部分 HLSL（代码面板显示残缺代码 + 边框染红）
- [ ] 撤销 / 重做后自动触发 `CompileAndShowErrors()`，UI 全同步
- [ ] 不修改 `MaterialCompiler` 类（只读 `CompileResult::errors` + `GetErrors()`）

---

## 核心原则回顾

1. **消费者角色**：课 19 是课 6 编译器错误诊断的纯消费者——只读 `CompileResult::errors` 渲染 UI，不重新定义 `CompileError`、不修改 `MaterialCompiler` 类
2. **pin 一等公民**：UE 错误只到节点级，我们错误带 `pinName` → 引脚级标红，UI 反馈更精确
3. **失败也展示**：编译失败时保留部分 HLSL + 染红代码面板，用户对照错误信息和残缺代码能更快定位问题
4. **命令模式显式可控**：手写每个 Command 的 Do/Undo 比 UE 的事务快照更直接易懂，代价是没"通用撤销"——但教学项目操作种类有限，命令模式够用
