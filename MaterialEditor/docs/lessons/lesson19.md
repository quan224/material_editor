# 课19：错误处理 + 撤销/重做

## 目标

实现编译错误反馈（可视化标注错误节点）和撤销/重做系统（命令模式）。

---

## 背景知识

### 编译错误反馈

UE5 材质编辑器的错误反馈：
1. 编译失败时，错误节点标题栏变红
2. 错误信息显示在底部面板
3. 点击错误信息可以定位到出错节点

我们用简单方案：编译失败时在状态栏显示错误，代码面板标红，并在节点图上高亮错误节点。

### 撤销/重做（命令模式）

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

---

## 操作步骤

### 1. 创建文件

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

### 2. Command.h（基类）

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

### 3. CommandStack.h/cpp

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

### 4. AddNodeCommand.h

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

### 5. RemoveNodeCommand.h

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

### 6. ConnectCommand.h

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

### 7. MoveNodeCommand.h

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

### 8. ChangeParameterCommand.h

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

### 9. 集成到 MainWindow

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

### 10. 编译错误可视化

修改 `MaterialCompiler::Compile` 或在 `MainWindow::OnCompile` 中添加错误反馈：

```cpp
// MainWindow.cpp
void MainWindow::OnCompile() {
    auto result = compiler_->Compile(graph_);

    if (result.success) {
        // 清除所有节点的错误状态
        graphWidget_->ClearErrorHighlights();

        compileStatus_->setText("Compiled OK");
        compileStatus_->setStyleSheet("color: green;");
        if (codePanel_) codePanel_->SetCode(result.hlslCode);
        if (viewportPanel_) viewportPanel_->SetMaterialResult(result);
    } else {
        compileStatus_->setText("Compile Error");
        compileStatus_->setStyleSheet("color: red;");

        // 在代码面板中显示错误
        if (codePanel_) codePanel_->SetError(result.errorMessage);

        // 高亮错误节点（如果有）
        // 注意：CompileResult 默认没有 errorNodeId 字段（课6 只定义了 success/hlslCode/errorMessage）。
        // 如果要做这个特性，需要给 CompileResult 加上：
        //   UUID errorNodeId = UUID::Invalid();
        // 并让 MaterialCompiler 在错误发生时填充它。这里假定该字段已加。
        if (!result.errorNodeId.IsValid()) {
            // 当前 errorNodeId 是全零（未设置），跳过
        } else {
            graphWidget_->HighlightErrorNode(result.errorNodeId);
        }

        // 状态栏显示简短错误
        statusLabel_->setText(
            QString("Error: %1").arg(
                QString::fromStdString(result.errorMessage)));
    }
}

// MaterialGraphWidget 中添加
void MaterialGraphWidget::HighlightErrorNode(const UUID& nodeId) {
    auto it = nodeItems_.find(nodeId);
    if (it != nodeItems_.end()) {
        // 设置错误状态（节点变红）
        it.value()->SetErrorState(true);
    }
}

void MaterialGraphWidget::ClearErrorHighlights() {
    for (auto* item : nodeItems_) {
        item->SetErrorState(false);
    }
}

// NodeGraphicsItem 中添加
void NodeGraphicsItem::SetErrorState(bool hasError) {
    hasError_ = hasError;
    update();
}

// 在 paint() 中添加错误样式
if (hasError_) {
    painter->setPen(QPen(QColor(255, 50, 50), 2.0));
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(0, 0, NODE_WIDTH, height_, CORNER_RADIUS, CORNER_RADIUS);
}
```

---

## 验证

### 撤销/重做

1. 添加一个节点 → Ctrl+Z → 节点消失 → Ctrl+Y → 节点恢复
2. 连接两个引脚 → Ctrl+Z → 连线消失 → Ctrl+Y → 连线恢复
3. 移动节点 → Ctrl+Z → 节点回到原位
4. 修改参数 → Ctrl+Z → 参数恢复原值
5. 多步操作 → 连续 Ctrl+Z → 连续 Ctrl+Y

### 错误处理

1. 创建循环引用 → 编译 → 错误提示
2. 连接类型不匹配的引脚 → 编译 → 错误提示
3. 缺少必要输入 → 编译 → 错误节点标红

---

## UE5 参考（相对 `Engine/` 路径）

- `Engine/Source/Editor/MaterialEditor/Private/MaterialEditor.cpp` — 编辑器主类
- 搜索 `CompileMaterial` / `HandleMaterialCompilationErrors` — 编译 + 错误可视化
- UE5 撤销/重做：搜索 `FScopedTransaction` / `GEditor->UndoTransaction`

### 对照 UE 撤销重做系统

| 我们的（命令模式）| UE（事务 + 对象快照）| 作用 |
|----------------|---------------------|------|
| `ICommand`（Do/Undo 虚函数）| `FUndoableTransaction` | 可撤销操作 |
| `UndoStack`（命令栈）| `GEditor->UndoTransaction` / `RedoTransaction` | 撤销栈 |
| 手写每个命令的 Do/Undo | `FScopedTransaction` + 对象 `Serialize` 快照 | 记录变更 |

**三个关键差异**：

1. **UE 用事务 + 对象快照**（`FScopedTransaction` 开启事务 → 修改 UObject → 事务自动记录修改前/后状态 → Undo 时恢复快照）。我们用**命令模式**（每个操作一个 `ICommand`，手写 Do/Undo）——更直接易懂，但每种操作（加节点/连线/改参数）都要写专门的命令类。

2. **UE 的撤销是通用的**（任何 UObject 修改都能撤销，因为反射 + 序列化自动记录）。我们的命令模式要为每种操作手写——代价是没有"通用撤销"，但好处是撤销逻辑显式、可控。

3. **错误可视化**：UE 的编译错误高亮到具体节点（`HandleMaterialCompilationErrors`）。这关联块5（错误诊断，`lesson06-extension.md`）——编译错误带节点/pin 定位，编辑器据此高亮。

> **搜索关键词**（UE 源码）：`FScopedTransaction`、`GEditor->Undo`、`HandleMaterialCompilationErrors`、`CompileMaterial`。

---

## 完成标志

- [ ] Ctrl+Z 撤销最后操作
- [ ] Ctrl+Y 重做
- [ ] 撤销/重做对添加/删除/连接/移动/参数修改都有效
- [ ] 编译错误在状态栏和代码面板中显示
- [ ] 错误节点在图中标红
- [ ] 命令栈在新建材质时清空
