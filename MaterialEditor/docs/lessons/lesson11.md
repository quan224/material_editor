# 课11：节点图形项

## 目标

实现节点和引脚的 Qt 图形项，在 QGraphicsScene 中渲染 UE5 风格的材质节点。

---

## 背景知识

### QGraphicsView/QGraphicsScene 架构

Qt 的 Graphics View Framework 采用 MVC 模式：
- **QGraphicsScene** — 场景，管理所有图形项，处理碰撞检测和事件分发
- **QGraphicsView** — 视图，负责渲染场景、处理用户输入（缩放/平移）
- **QGraphicsItem** — 图形项，每个可视元素

对应到我们的项目：
- `MaterialGraphWidget`（课12）= QGraphicsView 子类
- `NodeGraphicsItem` = QGraphicsItem 子类，渲染一个节点
- `PinGraphicsItem` = QGraphicsItem 子类，渲染一个引脚圆点

UE5 用 Slate 的 `SGraphNode` 体系，本质一样：每个节点是一个 `SCompoundWidget`，管理自己的子组件。

### UE5 节点视觉风格

```
┌─────────────────────────────┐  ← 标题栏（深色背景 + 类别色条）
│ ████████████████████████████ │  ← 顶部颜色条（Math=青色, Texture=紫色...）
│          Add                │  ← 节点名称
├─────────────────────────────┤
│ ● A                    ○    │  ← 输入引脚（左侧圆点）
│ ● B                    ○    │  ← 引脚颜色按类型：Float=绿, Float3=黄
├─────────────────────────────┤
│                    Result ● │  ← 输出引脚（右侧圆点）
└─────────────────────────────┘
```

---

## 操作步骤

### 1. 创建文件

```
src/UI/Private/GraphicsItems/NodeGraphicsItem.h
src/UI/Private/GraphicsItems/NodeGraphicsItem.cpp
src/UI/Private/GraphicsItems/PinGraphicsItem.h
src/UI/Private/GraphicsItems/PinGraphicsItem.cpp
```

### 2. 引脚颜色规则

在 `MaterialGraph/Public/Types.h` 中添加（已有则跳过）：

```cpp
#include <QColor>

// 引脚颜色（参考 UE5 的连接点颜色）
// 扩展版：EValueType 已含 Int/Matrix/Texture/Sampler（课6 扩展），这里全覆盖
inline QColor GetPinColor(EValueType type) {
    switch (type) {
        // float 系列 —— 绿→黄→红渐变（按分量数区分）
        case EValueType::Float1: return QColor(100, 200, 100);   // 绿
        case EValueType::Float2: return QColor(180, 200, 80);    // 黄绿
        case EValueType::Float3: return QColor(240, 200, 60);    // 黄
        case EValueType::Float4: return QColor(240, 80, 80);     // 红
        // int 系列 —— 蓝色（和 float 区分）
        case EValueType::Int1: case EValueType::Int2:
        case EValueType::Int3: case EValueType::Int4:
            return QColor(80, 140, 240);
        // 矩阵 —— 紫色
        case EValueType::Matrix3x3: case EValueType::Matrix4x4:
            return QColor(180, 100, 220);
        // 纹理 / 采样器 —— 粉色（对象类型，和数值类型区分）
        case EValueType::Texture2D:    return QColor(220, 120, 180);
        case EValueType::SamplerState: return QColor(220, 150, 150);
        default: return QColor(150, 150, 150);   // Unknown，灰
    }
}
```

### 3. PinGraphicsItem.h

```cpp
#pragma once
#include <QGraphicsItem>
#include <QColor>
#include "MaterialGraph/Public/Types.h"

class Pin;
class NodeGraphicsItem;

class PinGraphicsItem : public QGraphicsItem {
public:
    PinGraphicsItem(const Pin* pin, NodeGraphicsItem* parent);

    // QGraphicsItem 接口
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;

    // 获取引脚数据
    const Pin* GetPin() const { return pin_; }
    EValueType GetPinType() const;

    // 引脚连接点在场景中的坐标（供连线使用）
    QPointF GetConnectionPoint() const;

    // 引脚在节点中的位置（相对节点左/右侧边缘）
    void SetLocalPosition(const QPointF& pos);
    QPointF GetLocalPosition() const { return localPos_; }

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    const Pin* pin_;
    QPointF localPos_;     // 相对于节点的位置
    bool hovered_ = false;

    static constexpr qreal PIN_RADIUS = 5.0;
    static constexpr qreal PIN_HOVER_RADIUS = 7.0;
};
```

### 4. PinGraphicsItem.cpp

```cpp
#include "UI/Private/GraphicsItems/PinGraphicsItem.h"
#include "UI/Private/GraphicsItems/NodeGraphicsItem.h"
#include "MaterialGraph/Public/Pin.h"
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QCursor>

PinGraphicsItem::PinGraphicsItem(const Pin* pin, NodeGraphicsItem* parent)
    : QGraphicsItem(parent), pin_(pin) {
    setAcceptHoverEvents(true);
    setCursor(Qt::CrossCursor);
    setFlag(ItemSendsScenePositionChanges);
}

QRectF PinGraphicsItem::boundingRect() const {
    qreal r = hovered_ ? PIN_HOVER_RADIUS : PIN_RADIUS;
    return QRectF(-r, -r, 2 * r, 2 * r);
}

void PinGraphicsItem::paint(QPainter* painter,
                             const QStyleOptionGraphicsItem*,
                             QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);

    QColor color = GetPinColor(pin_->type);

    // 悬停时放大
    qreal radius = hovered_ ? PIN_HOVER_RADIUS : PIN_RADIUS;

    // 外圈
    if (pin_->IsConnected()) {
        painter->setBrush(color);
        painter->setPen(QPen(color.darker(150), 1.5));
    } else {
        painter->setBrush(Qt::NoBrush);  // 空心 = 未连接
        painter->setPen(QPen(color, 1.5));
    }

    painter->drawEllipse(QPointF(0, 0), radius, radius);

    // 悬停高亮
    if (hovered_) {
        painter->setBrush(QColor(255, 255, 255, 60));
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(QPointF(0, 0), radius, radius);
    }
}

QPointF PinGraphicsItem::GetConnectionPoint() const {
    // 引脚圆心在场景中的坐标
    return mapToScene(QPointF(0, 0));
}

EValueType PinGraphicsItem::GetPinType() const {
    return pin_->type;
}

void PinGraphicsItem::SetLocalPosition(const QPointF& pos) {
    localPos_ = pos;
    setPos(pos);
}

void PinGraphicsItem::hoverEnterEvent(QGraphicsSceneHoverEvent*) {
    hovered_ = true;
    update();
}

void PinGraphicsItem::hoverLeaveEvent(QGraphicsSceneHoverEvent*) {
    hovered_ = false;
    update();
}
```

### 5. NodeGraphicsItem.h

```cpp
#pragma once
#include <QGraphicsItem>
#include <QColor>
#include <map>
#include "Core/Public/UUID.h"

class Node;
class PinGraphicsItem;
class Expression;

class NodeGraphicsItem : public QGraphicsItem {
public:
    NodeGraphicsItem(Node* node, QGraphicsItem* parent = nullptr);
    ~NodeGraphicsItem();

    // QGraphicsItem 接口
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;

    // 数据访问
    Node* GetNode() const { return node_; }

    // 引脚项访问
    PinGraphicsItem* GetPinItem(const UUID& pinId) const;
    const std::map<UUID, PinGraphicsItem*>& GetPinItems() const { return pinItems_; }

    // 获取类别颜色（从 Expression 获取，或根据 typeName 判断）
    QColor GetCategoryColor() const;

    // 选中状态变化
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

protected:
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;

private:
    void CreatePinItems();
    void CalculateLayout();

    Node* node_;
    std::map<UUID, PinGraphicsItem*> pinItems_;

    // 布局参数
    static constexpr int NODE_WIDTH = 180;
    static constexpr int HEADER_HEIGHT = 28;
    static constexpr int CATEGORY_BAR_HEIGHT = 3;
    static constexpr int PIN_ROW_HEIGHT = 22;
    static constexpr int PIN_MARGIN_X = 10;
    static constexpr int CORNER_RADIUS = 6;
    static constexpr int TEXT_MARGIN = 8;

    // 计算后的尺寸
    qreal height_ = 0;
};
```

### 6. NodeGraphicsItem.cpp

```cpp
#include "UI/Private/GraphicsItems/NodeGraphicsItem.h"
#include "UI/Private/GraphicsItems/PinGraphicsItem.h"
#include "MaterialGraph/Public/Node.h"
#include "MaterialGraph/Public/Pin.h"
#include "Expression/Public/ExpressionRegistry.h"
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QFontMetrics>
#include <QGraphicsSceneMouseEvent>

NodeGraphicsItem::NodeGraphicsItem(Node* node, QGraphicsItem* parent)
    : QGraphicsItem(parent), node_(node) {
    setFlag(ItemIsMovable);
    setFlag(ItemIsSelectable);
    setFlag(ItemSendsGeometryChanges);
    setCacheMode(DeviceCoordinateCache);  // 提升绘制性能

    // 设置位置
    setPos(node_->position);

    // 创建引脚图形项
    CreatePinItems();
    CalculateLayout();
}

NodeGraphicsItem::~NodeGraphicsItem() {
    // Qt 自动删除子项
}

void NodeGraphicsItem::CreatePinItems() {
    // 输入引脚（左侧）
    for (const auto& pin : node_->inputPins) {
        auto* pinItem = new PinGraphicsItem(&pin, this);
        pinItems_[pin.id] = pinItem;
    }

    // 输出引脚（右侧）
    for (const auto& pin : node_->outputPins) {
        auto* pinItem = new PinGraphicsItem(&pin, this);
        pinItems_[pin.id] = pinItem;
    }
}

void NodeGraphicsItem::CalculateLayout() {
    int inputCount = node_->inputPins.size();
    int outputCount = node_->outputPins.size();
    int maxPins = std::max(inputCount, outputCount);

    // 总高度 = 标题栏 + 类别条 + 引脚行 * 数量 + 底部留白
    height_ = HEADER_HEIGHT + CATEGORY_BAR_HEIGHT
              + maxPins * PIN_ROW_HEIGHT + 8;

    // 布局引脚位置
    int y = HEADER_HEIGHT + CATEGORY_BAR_HEIGHT + PIN_ROW_HEIGHT / 2 + 2;

    for (const auto& pin : node_->inputPins) {
        auto* item = pinItems_[pin.id];
        if (item) {
            item->SetLocalPosition(QPointF(PIN_MARGIN_X, y));
        }
        y += PIN_ROW_HEIGHT;
    }

    y = HEADER_HEIGHT + CATEGORY_BAR_HEIGHT + PIN_ROW_HEIGHT / 2 + 2;
    for (const auto& pin : node_->outputPins) {
        auto* item = pinItems_[pin.id];
        if (item) {
            item->SetLocalPosition(QPointF(NODE_WIDTH - PIN_MARGIN_X, y));
        }
        y += PIN_ROW_HEIGHT;
    }
}

QRectF NodeGraphicsItem::boundingRect() const {
    return QRectF(0, 0, NODE_WIDTH, height_).adjusted(-2, -2, 2, 2);
}

void NodeGraphicsItem::paint(QPainter* painter,
                              const QStyleOptionGraphicsItem* option,
                              QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);

    bool selected = option->state & QStyle::State_Selected;

    // === 1. 主体背景 ===
    QColor bgColor(45, 45, 45);
    if (selected) bgColor = bgColor.lighter(130);

    QPainterPath path;
    path.addRoundedRect(0, 0, NODE_WIDTH, height_, CORNER_RADIUS, CORNER_RADIUS);
    painter->setPen(Qt::NoPen);
    painter->setBrush(bgColor);
    painter->drawPath(path);

    // === 2. 类别颜色条（顶部） ===
    QColor catColor = GetCategoryColor();
    QPainterPath barPath;
    barPath.addRoundedRect(0, 0, NODE_WIDTH, CATEGORY_BAR_HEIGHT + HEADER_HEIGHT,
                            CORNER_RADIUS, CORNER_RADIUS);
    // 底部不要圆角
    barPath.addRect(0, HEADER_HEIGHT, NODE_WIDTH, CATEGORY_BAR_HEIGHT);

    painter->setBrush(catColor);
    painter->drawPath(barPath);

    // === 3. 标题栏背景 ===
    QColor headerBg = catColor.darker(300);
    QPainterPath headerPath;
    headerPath.addRoundedRect(0, 0, NODE_WIDTH, HEADER_HEIGHT,
                               CORNER_RADIUS, CORNER_RADIUS);
    headerPath.addRect(0, HEADER_HEIGHT - CORNER_RADIUS,
                        NODE_WIDTH, CORNER_RADIUS);
    painter->setBrush(headerBg);
    painter->drawPath(headerPath);

    // === 4. 标题文字 ===
    painter->setPen(Qt::white);
    QFont titleFont("Segoe UI", 9, QFont::Bold);
    painter->setFont(titleFont);
    painter->drawText(QRectF(TEXT_MARGIN, 2, NODE_WIDTH - 2 * TEXT_MARGIN, HEADER_HEIGHT),
                       Qt::AlignVCenter | Qt::AlignLeft,
                       QString::fromStdString(node_->title));

    // === 5. 引脚名称 ===
    QFont pinFont("Segoe UI", 8);
    painter->setFont(pinFont);

    for (const auto& pin : node_->inputPins) {
        QColor pinColor = GetPinColor(pin.type);
        painter->setPen(pinColor);
        qreal y = pinItems_[pin.id]->pos().y();
        painter->drawText(QRectF(PIN_MARGIN_X + 10, y - 8, 80, 16),
                           Qt::AlignVCenter | Qt::AlignLeft,
                           QString::fromStdString(pin.name));
    }

    for (const auto& pin : node_->outputPins) {
        QColor pinColor = GetPinColor(pin.type);
        painter->setPen(pinColor);
        qreal y = pinItems_[pin.id]->pos().y();
        painter->drawText(QRectF(NODE_WIDTH - PIN_MARGIN_X - 90, y - 8, 80, 16),
                           Qt::AlignVCenter | Qt::AlignRight,
                           QString::fromStdString(pin.name));
    }

    // === 6. 选中边框 ===
    if (selected) {
        painter->setPen(QPen(QColor(255, 200, 50), 2.0));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(0, 0, NODE_WIDTH, height_, CORNER_RADIUS, CORNER_RADIUS);
    }

    // === 7. 分割线（标题和引脚之间） ===
    painter->setPen(QPen(QColor(80, 80, 80), 0.5));
    qreal lineY = HEADER_HEIGHT + CATEGORY_BAR_HEIGHT;
    painter->drawLine(4, lineY, NODE_WIDTH - 4, lineY);
}

QColor NodeGraphicsItem::GetCategoryColor() const {
    // 从反射 ClassDesc 查询颜色（课5：UI 元数据存在 ClassDesc 里）
    auto expr = ExpressionRegistry::GetInstance().Create(node_->typeName);
    if (expr) {
        const reflection::ClassDesc* desc = expr->GetClassDesc();
        if (desc && !desc->category_color.empty()) {
            return QColor(QString::fromStdString(desc->category_color));
        }
        // category_color 为空时，根据 category 字符串映射到默认色
        if (desc) {
            static const std::map<std::string, std::string> categoryColors = {
                {"Math",       "#4CA7E8"},
                {"Constants",  "#8E8E8E"},
                {"Parameters", "#8E8E8E"},
                {"Texture",    "#9B59B6"},
                {"Vector",     "#4CA7E8"},
                {"Utility",    "#2ECC71"},
                {"Misc",       "#666666"},
            };
            auto it = categoryColors.find(desc->category);
            if (it != categoryColors.end()) {
                return QColor(QString::fromStdString(it->second));
            }
        }
    }
    return QColor("#666666");  // 未知类型的兜底色
}

PinGraphicsItem* NodeGraphicsItem::GetPinItem(const UUID& pinId) const {
    auto it = pinItems_.find(pinId);
    return (it != pinItems_.end()) ? it->second : nullptr;
}

QVariant NodeGraphicsItem::itemChange(GraphicsItemChange change,
                                       const QVariant& value) {
    if (change == ItemPositionHasChanged) {
        // 通知连接线更新位置
        // 后续课12实现：通知场景中的所有连接线更新路径
    }
    return QGraphicsItem::itemChange(change, value);
}

void NodeGraphicsItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
    // 双击节点 → 打开属性面板（后续实现）
    QGraphicsItem::mouseDoubleClickEvent(event);
}
```

### 7. 材质输出节点特殊样式（可选增强）

材质输出节点是特殊的，可以用不同颜色和更大的尺寸：

```cpp
// 在 NodeGraphicsItem::paint 开头添加
bool isOutputNode = (node_->typeName == "MaterialOutput");

if (isOutputNode) {
    // 输出节点用特殊颜色
    // 标题栏用红色/橙色
    // 尺寸更大
}
```

---

## 验证

此时还无法在主窗口中看到节点（需要课12的 MaterialGraphWidget），但可以写一个简单的验证：

```cpp
// 在 main.cpp 中临时添加
#include <QGraphicsScene>
#include <QGraphicsView>
#include "UI/Private/GraphicsItems/NodeGraphicsItem.h"
#include "MaterialGraph/Public/NodeFactory.h"

// 创建一个测试场景
QGraphicsScene scene;
QGraphicsView view(&scene);

auto& factory = NodeFactory::GetInstance();
factory.Register("ExprAdd", "Add", "Math", {
    {"A", EValueType::Float1, EPinDataDirection::Input, 0.0f},
    {"B", EValueType::Float1, EPinDataDirection::Input, 0.0f},
    {"Result", EValueType::Float1, EPinDataDirection::Output}
});

auto node = factory.Create("ExprAdd", {100, 100});
NodeGraphicsItem item(node.get());
scene.addItem(&item);

view.setRenderHint(QPainter::Antialiasing);
view.setSceneRect(0, 0, 400, 300);
view.show();
```

预期看到一个蓝色标题栏的 "Add" 节点，左侧两个绿色引脚（A、B），右侧一个绿色引脚（Result）。

---

## UE5 参考（相对 `Engine/` 路径）

- `Engine/Source/Editor/GraphEditor/Private/SGraphNode.cpp` — 节点 `paint()` 方法（对照我们的 `NodeGraphicsItem::paint`）
- `Engine/Source/Editor/MaterialEditor/Private/MaterialNodes/SGraphNodeMaterialBase.cpp` — 材质节点基类
- `Engine/Source/Editor/GraphEditor/Private/SGraphPin.cpp` — 引脚绘制（对照 `PinGraphicsItem::paint`）
- `Engine/Source/Editor/MaterialEditor/Private/MaterialPins/SGraphPinMaterialInput.cpp` — 材质引脚

### 对照 UE 节点渲染（SGraphNode vs QGraphicsItem）

| 我们的（Qt Graphics View）| UE（Slate Graph Editor）| 作用 |
|---------------------------|------------------------|------|
| `QGraphicsScene` | `SGraphPanel` | 场景，管理所有图形项 |
| `QGraphicsView` | `SGraphViewer` | 视图，渲染 + 缩放/平移 |
| `NodeGraphicsItem : QGraphicsItem` | `SGraphNode : SCompoundWidget` | 单个节点的渲染 + 交互 |
| `PinGraphicsItem` | `SGraphPin` | 引脚圆点 |
| `GetPinColor(EValueType)` | `GetDefaultPinColor` / `GetPinColor` | 引脚类型颜色 |
| `GetCategoryColor()`（从 ClassDesc）| `GetDefaultTitleBarColor` | 节点标题栏颜色 |

**关键差异**：
1. **UE 用 Slate widget**（`SGraphNode` 是 `SCompoundWidget`，用 Slate 声明式 UI），我们用 **QGraphicsItem + QPainter** 手绘——本质都是"自己画节点矩形 + 引脚圆点 + 文字"。
2. **UE 的引脚颜色**（`SGraphPin::GetPinColor`）和我们的 `GetPinColor` 一样按类型分色：Float=绿、Float3=黄、Bool=红等。我们扩展版加了 Int(蓝)/Matrix(紫)/Texture(粉)。
3. **节点标题色**：UE 用 `GetDefaultTitleBarColor`（按节点类别），我们用 `GetCategoryColor`（从反射 `ClassDesc::category_color` 查，课5 的设计）。

> **搜索关键词**（UE 源码）：`SGraphNode::Paint`、`SGraphPin`、`GetDefaultTitleBarColor`、`GetPinColor`。

---

## 完成标志

- [ ] NodeGraphicsItem 渲染正确：标题栏、引脚、颜色
- [ ] PinGraphicsItem 渲染正确：圆点、类型颜色、悬停效果
- [ ] 选中节点时边框高亮
- [ ] 引脚名称正确显示
- [ ] 节点可拖拽移动
