# 课12：连接线 + 画布交互

## 目标

实现连接线图形项（贝塞尔曲线）和画布控件（缩放、平移、右键菜单、连线拖拽）。

---

## 背景知识

### UE5 连线风格

UE5 的节点连线是水平方向的 S 形贝塞尔曲线：

```
输出引脚 ●─────────────────● 输入引脚
         ╲                ╱
          ╲              ╱
           ╲            ╱
            ────────────
```

控制点在水平方向偏移，偏移量大约是两端点水平距离的 1/3~1/2。这样形成了 UE5 标志性的连线风格。

### 连线拖拽交互

1. 用户在引脚上按下鼠标
2. 拖拽时显示临时连线（从引脚到鼠标位置）
3. 松开在另一个引脚上 → 建立连接
4. 松开在空白处 → 取消

这和 UE5 的 `SGraphPanel::OnMouseMove` 中连线拖拽逻辑一致。

---

## 操作步骤

### 1. 创建文件

```
src/UI/Private/GraphicsItems/ConnectionGraphicsItem.h
src/UI/Private/GraphicsItems/ConnectionGraphicsItem.cpp
src/UI/Private/MaterialGraphWidget.h
src/UI/Private/MaterialGraphWidget.cpp
```

### 2. ConnectionGraphicsItem.h

```cpp
#pragma once
#include <QGraphicsPathItem>
#include <QColor>

class PinGraphicsItem;

class ConnectionGraphicsItem : public QGraphicsPathItem {
public:
    // 正式连接（两个引脚已确定）
    ConnectionGraphicsItem(PinGraphicsItem* sourcePin,
                           PinGraphicsItem* targetPin,
                           QGraphicsItem* parent = nullptr);

    // 临时连接（正在拖拽）
    ConnectionGraphicsItem(PinGraphicsItem* startPin,
                           const QPointF& endPoint,
                           QGraphicsItem* parent = nullptr);

    // 更新路径
    void UpdatePath();
    void UpdateTempPath(const QPointF& mousePos);

    // 访问
    PinGraphicsItem* GetSourcePin() const { return sourcePin_; }
    PinGraphicsItem* GetTargetPin() const { return targetPin_; }

    // 连线颜色（由引脚类型决定）
    QColor GetColor() const;

private:
    QPainterPath CreateBezierPath(const QPointF& start, const QPointF& end);

    PinGraphicsItem* sourcePin_ = nullptr;
    PinGraphicsItem* targetPin_ = nullptr;
    QPointF tempEndPoint_;
    bool isTemp_ = false;
};
```

### 3. ConnectionGraphicsItem.cpp

```cpp
#include "UI/Private/GraphicsItems/ConnectionGraphicsItem.h"
#include "UI/Private/GraphicsItems/PinGraphicsItem.h"
#include "MaterialGraph/Public/Types.h"
#include <QPainter>

ConnectionGraphicsItem::ConnectionGraphicsItem(
    PinGraphicsItem* sourcePin, PinGraphicsItem* targetPin,
    QGraphicsItem* parent)
    : QGraphicsPathItem(parent), sourcePin_(sourcePin), targetPin_(targetPin) {
    setZValue(-1);  // 连线在节点下方
    UpdatePath();
}

ConnectionGraphicsItem::ConnectionGraphicsItem(
    PinGraphicsItem* startPin, const QPointF& endPoint,
    QGraphicsItem* parent)
    : QGraphicsPathItem(parent), sourcePin_(startPin), isTemp_(true) {
    setZValue(-1);
    tempEndPoint_ = endPoint;
    UpdateTempPath(endPoint);
}

QPainterPath ConnectionGraphicsItem::CreateBezierPath(
    const QPointF& start, const QPointF& end) {
    QPainterPath path;
    path.moveTo(start);

    // 计算水平偏移（控制点距离）
    qreal dx = std::abs(end.x() - start.x());
    qreal offset = std::max(dx * 0.5, 50.0);  // 最小偏移 50px

    // S 形贝塞尔曲线
    QPointF cp1(start.x() + offset, start.y());
    QPointF cp2(end.x() - offset, end.y());
    path.cubicTo(cp1, cp2, end);

    return path;
}

void ConnectionGraphicsItem::UpdatePath() {
    if (!sourcePin_ || !targetPin_) return;

    QPointF start = sourcePin_->GetConnectionPoint();
    QPointF end = targetPin_->GetConnectionPoint();

    // 确保起点在左，终点在右
    if (start.x() > end.x()) {
        std::swap(start, end);
    }

    setPath(CreateBezierPath(start, end));
}

void ConnectionGraphicsItem::UpdateTempPath(const QPointF& mousePos) {
    if (!sourcePin_) return;

    QPointF start = sourcePin_->GetConnectionPoint();
    setPath(CreateBezierPath(start, mousePos));
}

QColor ConnectionGraphicsItem::GetColor() const {
    if (sourcePin_) return GetPinColor(sourcePin_->GetPinType());
    if (targetPin_) return GetPinColor(targetPin_->GetPinType());
    return QColor(200, 200, 200);
}

void ConnectionGraphicsItem::paint(QPainter* painter,
                                    const QStyleOptionGraphicsItem*,
                                    QWidget*) {
    QColor color = GetColor();
    if (isTemp_) color.setAlpha(150);

    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(color, 2.5, Qt::SolidLine, Qt::RoundCap));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(path());
}
```

### 4. MaterialGraphWidget.h

```cpp
#pragma once
#include <QGraphicsView>
#include <QMap>
#include "Core/Public/UUID.h"

class Graph;
class Node;
class NodeGraphicsItem;
class PinGraphicsItem;
class ConnectionGraphicsItem;
class Connection;
class NodeFactory;

class MaterialGraphWidget : public QGraphicsView {
    Q_OBJECT
public:
    MaterialGraphWidget(Graph* graph, NodeFactory* factory,
                        QWidget* parent = nullptr);

    // 同步 Graph 数据到 QGraphicsScene
    void SyncFromGraph();

    // 适配视图以显示所有节点
    void FitToView();

signals:
    void NodeSelected(Node* node);      // 选中节点时发出（属性面板用）
    void SelectionCleared();

protected:
    // 缩放/平移
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

    // 右键菜单
    void contextMenuEvent(QContextMenuEvent* event) override;

    // 网格背景
    void drawBackground(QPainter* painter, const QRectF& rect) override;

private slots:
    void OnNodeAdded(Node* node);
    void OnNodeRemoved(const UUID& nodeId);
    void OnConnectionAdded(const Connection& conn);
    void OnConnectionRemoved(const UUID& connId);

private:
    // 创建/删除图形项
    NodeGraphicsItem* CreateNodeItem(Node* node);
    void RemoveNodeItem(const UUID& nodeId);
    ConnectionGraphicsItem* CreateConnectionItem(const Connection& conn);
    void RemoveConnectionItem(const UUID& connId);

    // 连线拖拽
    enum class DragState { None, Connecting };
    DragState dragState_ = DragState::None;
    PinGraphicsItem* dragStartPin_ = nullptr;
    ConnectionGraphicsItem* tempConnection_ = nullptr;

    // 查找引脚项
    PinGraphicsItem* FindPinItemAt(const QPointF& scenePos) const;

    // 数据
    Graph* graph_;
    NodeFactory* factory_;
    QGraphicsScene* scene_;

    // 映射：数据 ID → 图形项
    QMap<UUID, NodeGraphicsItem*> nodeItems_;
    QMap<UUID, ConnectionGraphicsItem*> connectionItems_;

    // 平移状态
    bool isPanning_ = false;
    QPoint lastPanPoint_;
};
```

### 5. MaterialGraphWidget.cpp

```cpp
#include "UI/Private/MaterialGraphWidget.h"
#include "UI/Private/GraphicsItems/NodeGraphicsItem.h"
#include "UI/Private/GraphicsItems/PinGraphicsItem.h"
#include "UI/Private/GraphicsItems/ConnectionGraphicsItem.h"
#include "MaterialGraph/Public/Graph.h"
#include "MaterialGraph/Public/Node.h"
#include "MaterialGraph/Public/Pin.h"
#include "MaterialGraph/Public/Connection.h"
#include "MaterialGraph/Public/NodeFactory.h"
#include "Core/Public/Logger.h"

#include <QGraphicsScene>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QScrollBar>
#include <QTransform>

MaterialGraphWidget::MaterialGraphWidget(Graph* graph, NodeFactory* factory,
                                          QWidget* parent)
    : QGraphicsView(parent), graph_(graph), factory_(factory) {
    scene_ = new QGraphicsScene(this);
    scene_->setSceneRect(-5000, -5000, 10000, 10000);
    setScene(scene_);

    // 渲染设置
    setRenderHint(QPainter::Antialiasing);
    setRenderHint(QPainter::SmoothPixmapTransform);
    setViewportUpdateMode(FullViewportUpdate);
    setTransformationAnchor(AnchorUnderMouse);
    setDragMode(NoDrag);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    // 连接 Graph 信号
    connect(graph_, &Graph::NodeAdded, this, &MaterialGraphWidget::OnNodeAdded);
    connect(graph_, &Graph::NodeRemoved, this, &MaterialGraphWidget::OnNodeRemoved);
    connect(graph_, &Graph::ConnectionAdded, this, &MaterialGraphWidget::OnConnectionAdded);
    connect(graph_, &Graph::ConnectionRemoved, this, &MaterialGraphWidget::OnConnectionRemoved);

    // 初始同步
    SyncFromGraph();
}

// ====== Graph 信号处理 ======

void MaterialGraphWidget::OnNodeAdded(Node* node) {
    CreateNodeItem(node);
}

void MaterialGraphWidget::OnNodeRemoved(const UUID& nodeId) {
    RemoveNodeItem(nodeId);
}

void MaterialGraphWidget::OnConnectionAdded(const Connection& conn) {
    CreateConnectionItem(conn);
}

void MaterialGraphWidget::OnConnectionRemoved(const UUID& connId) {
    RemoveConnectionItem(connId);
}

// ====== 图形项创建/删除 ======

NodeGraphicsItem* MaterialGraphWidget::CreateNodeItem(Node* node) {
    if (nodeItems_.contains(node->id)) return nodeItems_[node->id];

    auto* item = new NodeGraphicsItem(node);
    scene_->addItem(item);
    nodeItems_[node->id] = item;
    return item;
}

void MaterialGraphWidget::RemoveNodeItem(const UUID& nodeId) {
    auto it = nodeItems_.find(nodeId);
    if (it != nodeItems_.end()) {
        scene_->removeItem(it.value());
        delete it.value();
        nodeItems_.erase(it);
    }
}

ConnectionGraphicsItem* MaterialGraphWidget::CreateConnectionItem(
    const Connection& conn) {
    if (connectionItems_.contains(conn.id)) return connectionItems_[conn.id];

    auto* srcNodeItem = nodeItems_.value(conn.sourceNodeId);
    auto* tgtNodeItem = nodeItems_.value(conn.targetNodeId);
    if (!srcNodeItem || !tgtNodeItem) return nullptr;

    auto* srcPinItem = srcNodeItem->GetPinItem(conn.sourcePinId);
    auto* tgtPinItem = tgtNodeItem->GetPinItem(conn.targetPinId);
    if (!srcPinItem || !tgtPinItem) return nullptr;

    auto* item = new ConnectionGraphicsItem(srcPinItem, tgtPinItem);
    scene_->addItem(item);
    connectionItems_[conn.id] = item;
    return item;
}

void MaterialGraphWidget::RemoveConnectionItem(const UUID& connId) {
    auto it = connectionItems_.find(connId);
    if (it != connectionItems_.end()) {
        scene_->removeItem(it.value());
        delete it.value();
        connectionItems_.erase(it);
    }
}

// ====== 初始同步 ======

void MaterialGraphWidget::SyncFromGraph() {
    // 清空现有项
    scene_->clear();
    nodeItems_.clear();
    connectionItems_.clear();

    // 创建所有节点（注意方法名是 GetNodes / GetConnections，不是 GetAllNodes）
    for (auto& [id, node] : graph_->GetNodes()) {
        CreateNodeItem(node.get());
    }

    // 创建所有连接
    for (auto& [id, conn] : graph_->GetConnections()) {
        CreateConnectionItem(conn);
    }
}

// ====== 鼠标交互 ======

void MaterialGraphWidget::wheelEvent(QWheelEvent* event) {
    // 缩放
    double factor = (event->angleDelta().y() > 0) ? 1.15 : 1.0 / 1.15;
    double newScale = transform().m11() * factor;

    // 限制缩放范围
    if (newScale < 0.1 || newScale > 3.0) return;

    scale(factor, factor);
}

void MaterialGraphWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        // 中键拖拽平移
        isPanning_ = true;
        lastPanPoint_ = event->pos();
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    if (event->button() == Qt::LeftButton) {
        // 检查是否点击了引脚
        QPointF scenePos = mapToScene(event->pos());
        PinGraphicsItem* pin = FindPinItemAt(scenePos);

        if (pin) {
            // 开始连线拖拽
            dragState_ = DragState::Connecting;
            dragStartPin_ = pin;
            tempConnection_ = new ConnectionGraphicsItem(pin, scenePos);
            scene_->addItem(tempConnection_);
            return;
        }
    }

    QGraphicsView::mousePressEvent(event);
}

void MaterialGraphWidget::mouseMoveEvent(QMouseEvent* event) {
    if (isPanning_) {
        // 平移
        QPoint delta = event->pos() - lastPanPoint_;
        lastPanPoint_ = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        return;
    }

    if (dragState_ == DragState::Connecting && tempConnection_) {
        // 更新临时连线
        QPointF scenePos = mapToScene(event->pos());
        tempConnection_->UpdateTempPath(scenePos);
        return;
    }

    QGraphicsView::mouseMoveEvent(event);
}

void MaterialGraphWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (isPanning_) {
        isPanning_ = false;
        setCursor(Qt::ArrowCursor);
        return;
    }

    if (dragState_ == DragState::Connecting) {
        // 检查是否松开在另一个引脚上
        QPointF scenePos = mapToScene(event->pos());
        PinGraphicsItem* endPin = FindPinItemAt(scenePos);

        if (endPin && endPin != dragStartPin_) {
            // 验证连接有效性
            const Pin* srcPin = dragStartPin_->GetPin();
            const Pin* tgtPin = endPin->GetPin();

            // 一个是输入一个是输出
            bool valid = (srcPin->direction != tgtPin->direction);
            // 类型兼容
            if (valid) valid = srcPin->CanConnectTo(*tgtPin);

            if (valid) {
                // 确定源和目标
                UUID sourcePinId = srcPin->direction == EPinDataDirection::Output
                                       ? srcPin->id : tgtPin->id;
                UUID targetPinId = srcPin->direction == EPinDataDirection::Input
                                       ? srcPin->id : tgtPin->id;
                graph_->Connect(sourcePinId, targetPinId);
            }
        }

        // 清理临时连线
        if (tempConnection_) {
            scene_->removeItem(tempConnection_);
            delete tempConnection_;
            tempConnection_ = nullptr;
        }
        dragState_ = DragState::None;
        dragStartPin_ = nullptr;
        return;
    }

    QGraphicsView::mouseReleaseEvent(event);
}

void MaterialGraphWidget::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Delete) {
        // 删除选中的节点或连线
        for (auto* item : scene_->selectedItems()) {
            auto* nodeItem = dynamic_cast<NodeGraphicsItem*>(item);
            if (nodeItem) {
                graph_->RemoveNode(nodeItem->GetNode()->id);
            }
        }
        return;
    }

    QGraphicsView::keyPressEvent(event);
}

// ====== 右键菜单 ======

void MaterialGraphWidget::contextMenuEvent(QContextMenuEvent* event) {
    QPointF scenePos = mapToScene(event->pos());

    QMenu menu;

    // 按类别分组添加节点
    auto types = factory_->GetAllTypes();
    std::map<std::string, std::vector<const NodeFactory::NodeTypeInfo*>> byCategory;
    for (const auto& t : types) {
        byCategory[t.category].push_back(&t);
    }

    for (const auto& [category, typeList] : byCategory) {
        auto* subMenu = menu.addMenu(QString::fromStdString(category));
        for (const auto* typeInfo : typeList) {
            auto* action = subMenu->addAction(
                QString::fromStdString(typeInfo->displayName));
            // 捕获 typeName 和位置
            QString typeName = QString::fromStdString(typeInfo->typeName);
            connect(action, &QAction::triggered, this, [this, typeName, scenePos]() {
                auto node = factory_->Create(typeName.toStdString(), scenePos);
                if (node) graph_->AddNode(node);
            });
        }
    }

    menu.addSeparator();
    menu.addAction("Fit to View", this, &MaterialGraphWidget::FitToView);

    menu.exec(event->globalPos());
}

// ====== 网格背景 ======

void MaterialGraphWidget::drawBackground(QPainter* painter, const QRectF& rect) {
    QGraphicsView::drawBackground(painter, rect);

    // 绘制网格点阵
    const qreal gridSize = 20.0;
    const qreal majorGridSize = 100.0;

    painter->setRenderHint(QPainter::Antialiasing, false);

    // 小网格点
    QColor dotColor(80, 80, 80, 100);
    painter->setPen(Qt::NoPen);
    painter->setBrush(dotColor);

    qreal left = std::floor(rect.left() / gridSize) * gridSize;
    qreal top = std::floor(rect.top() / gridSize) * gridSize;

    for (qreal x = left; x < rect.right(); x += gridSize) {
        for (qreal y = top; y < rect.bottom(); y += gridSize) {
            // 只绘制不在大网格上的点
            if (std::fmod(x, majorGridSize) != 0 || std::fmod(y, majorGridSize) != 0) {
                painter->drawRect(QRectF(x - 0.5, y - 0.5, 1, 1));
            }
        }
    }

    // 大网格点（更亮）
    QColor majorDotColor(100, 100, 100, 150);
    painter->setBrush(majorDotColor);

    qreal majorLeft = std::floor(rect.left() / majorGridSize) * majorGridSize;
    qreal majorTop = std::floor(rect.top() / majorGridSize) * majorGridSize;

    for (qreal x = majorLeft; x < rect.right(); x += majorGridSize) {
        for (qreal y = majorTop; y < rect.bottom(); y += majorGridSize) {
            painter->drawRect(QRectF(x - 1, y - 1, 2, 2));
        }
    }
}

// ====== 辅助方法 ======

PinGraphicsItem* MaterialGraphWidget::FindPinItemAt(const QPointF& scenePos) const {
    // 遍历所有节点的引脚，查找距离场景坐标最近的引脚
    for (auto* nodeItem : nodeItems_) {
        for (auto* pinItem : nodeItem->GetPinItems()) {
            QPointF pinScenePos = pinItem->GetConnectionPoint();
            qreal dist = (pinScenePos - scenePos).manhattanLength();
            if (dist < 15.0) {  // 15px 容差
                return pinItem;
            }
        }
    }
    return nullptr;
}

void MaterialGraphWidget::FitToView() {
    QRectF bounds;
    for (auto* item : nodeItems_) {
        bounds |= item->mapToScene(item->boundingRect()).boundingRect();
    }
    if (!bounds.isEmpty()) {
        fitInView(bounds.adjusted(-50, -50, 50, 50), Qt::KeepAspectRatio);
    }
}
```

### 6. 集成到 MainWindow

修改 `MainWindow::SetupDockWidgets()`，将中央占位替换为真实的 MaterialGraphWidget：

```cpp
// MainWindow.cpp 修改
#include "UI/Private/MaterialGraphWidget.h"

void MainWindow::SetupDockWidgets() {
    // 中央：材质图画布
    graphWidget_ = new MaterialGraphWidget(graph_, factory_, this);
    setCentralWidget(graphWidget_);

    // ... 其余面板不变 ...
}
```

---

## 验证

1. 运行程序
2. 在画布上右键 → Math → Add → 出现 Add 节点
3. 右键 → Constants → Constant → 出现 Constant 节点
4. 从 Constant 的输出引脚拖拽到 Add 的输入引脚 → 出现贝塞尔曲线连线
5. 拖拽节点移动 → 连线跟随更新
6. 滚轮缩放画布
7. 中键拖拽平移
8. 选中节点后 Delete 键删除
9. 网格背景显示

---

## UE5 参考（相对 `Engine/` 路径）

- `Engine/Source/Editor/GraphEditor/Private/SConnectionDrawingPolicy.cpp` — 连线绘制（对照 `ConnectionGraphicsItem`）
- `Engine/Source/Editor/GraphEditor/Private/SGraphPanel.cpp` — 画布控件（对照 `MaterialGraphWidget`）

### 对照 UE 连线/画布

| 我们的（Qt）| UE（Slate）| 作用 |
|------------|-----------|------|
| `ConnectionGraphicsItem`（QGraphicsPathItem）| `SConnectionDrawingPolicy` | 贝塞尔连线绘制 |
| `CreateBezierPath`（`cubicTo`）| `MakeCachedSpline` / `ComputeSpline` | S 形贝塞尔曲线 |
| `MaterialGraphWidget`（QGraphicsView）| `SGraphPanel` | 画布：缩放/平移/右键/连线拖拽 |
| `wheelEvent`（`scale`）| `OnMouseWheel` + `GetZoomAmount` | 缩放 |
| `contextMenuEvent`（QMenu）| `SpawnContextMenu`（FMenuBuilder）| 右键节点菜单 |
| `Pin::CanConnectTo`（连线验证）| `FConnectionDrawingPolicy::CheckConnection` | 类型兼容检查 |

**关键差异**：
1. **UE 的连线**由 `SConnectionDrawingPolicy` 统一管理（所有连线在一次 paint pass 里画，性能优化），我们每个连线是独立 `QGraphicsPathItem`（Qt 场景管理，节点多时可能慢，但教学够用）。
2. **连线验证**（`Pin::CanConnectTo`）：UE 在 `FConnectionDrawingPolicy::CheckConnection` 做类型兼容检查；我们在 `Pin::CanConnectTo`（Types.h），扩展版要支持 Int/Matrix/Texture 的新类型兼容规则（如 Int+Float 升级、Texture 类型必须相同等）。
3. **右键菜单**：UE 用 `FMenuBuilder`（Slate），我们用 `QMenu`——都是分组 + action，概念一致。

> **搜索关键词**（UE 源码）：`SConnectionDrawingPolicy`、`MakeCachedSpline`、`SGraphPanel::OnMouseMove`、`SpawnContextMenu`、`GetZoomAmount`。

---

## 完成标志

- [ ] 右键菜单可添加节点到画布
- [ ] 从引脚拖拽可建立连线（贝塞尔曲线）
- [ ] 拖拽节点时连线跟随更新
- [ ] 滚轮缩放（0.1x~3x）
- [ ] 中键平移画布
- [ ] Delete 键删除选中节点
- [ ] 网格背景正确渲染
- [ ] 类型不匹配时拒绝连线
