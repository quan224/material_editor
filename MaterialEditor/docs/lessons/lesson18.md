# 课18：保存/加载 + 着色器导出

## 目标

实现材质图的 JSON 序列化（保存/加载）和着色器导出功能。

---

## 背景知识

### 序列化格式

UE5 的 `.uasset` 使用二进制序列化（UObject 的 `Serialize()` 方法）。我们用更简单的 JSON 格式：

```json
{
  "version": "0.1.0",
  "nodes": [
    {
      "id": "a1b2c3d4-...",
      "type": "ExprConstant3Vector",
      "title": "Constant3Vector",
      "position": [100, 200],
      "parameters": {
        "R": 1.0, "G": 0.0, "B": 0.0
      }
    },
    {
      "id": "e5f6a7b8-...",
      "type": "ExprAdd",
      "title": "Add",
      "position": [400, 200],
      "parameters": {}
    }
  ],
  "connections": [
    {
      "id": "c0d1e2f3-...",
      "sourceNodeId": "a1b2c3d4-...",
      "sourcePinName": "Output",
      "targetNodeId": "e5f6a7b8-...",
      "targetPinName": "A"
    }
  ]
}
```

使用**引脚名称**而非引脚 ID 来存储连接，因为 ID 每次创建都不同。加载时根据名称查找引脚。

---

## 操作步骤

### 1. 创建文件

```
src/Core/Public/MaterialSerializer.h
src/Core/Private/MaterialSerializer.cpp
src/UI/Private/ExportDialog.h
src/UI/Private/ExportDialog.cpp
```

### 2. MaterialSerializer.h

```cpp
#pragma once
#include <string>
#include <nlohmann/json.hpp>

class Graph;

class MaterialSerializer {
public:
    // 保存到 JSON
    static nlohmann::json Serialize(const Graph* graph);
    static bool SaveToFile(const Graph* graph, const std::string& filePath);

    // 从 JSON 加载
    static Graph* Deserialize(const nlohmann::json& json);
    static Graph* LoadFromFile(const std::string& filePath);

    // 版本号
    static constexpr const char* VERSION = "0.1.0";
};
```

### 3. MaterialSerializer.cpp

```cpp
#include "Core/Public/MaterialSerializer.h"
#include "MaterialGraph/Public/Graph.h"
#include "MaterialGraph/Public/Node.h"
#include "MaterialGraph/Public/Pin.h"
#include "MaterialGraph/Public/Connection.h"
#include "MaterialGraph/Public/NodeFactory.h"
#include "Expression/Public/ExpressionRegistry.h"
#include "Core/Public/Logger.h"

#include <fstream>
#include <filesystem>

nlohmann::json MaterialSerializer::Serialize(const Graph* graph) {
    nlohmann::json j;
    j["version"] = VERSION;

    // 序列化节点（Graph 暴露的是 GetNodes / GetConnections，不是 GetAllNodes）
    auto& nodesJson = j["nodes"] = nlohmann::json::array();
    for (const auto& [id, node] : graph->GetNodes()) {
        nlohmann::json nodeJson;
        nodeJson["id"] = id.ToString();
        nodeJson["type"] = node->typeName;
        nodeJson["title"] = node->title;
        nodeJson["position"] = {node->position.x(), node->position.y()};
        nodeJson["parameters"] = node->parameters;
        nodesJson.push_back(nodeJson);
    }

    // 序列化连接
    auto& connsJson = j["connections"] = nlohmann::json::array();
    for (const auto& [id, conn] : graph->GetConnections()) {
        nlohmann::json connJson;
        connJson["id"] = id.ToString();

        // 用节点 ID + 引脚名称存储（而非引脚 ID）
        connJson["sourceNodeId"] = conn.sourceNodeId.ToString();

        // 查找源引脚名称
        auto* srcNode = graph->FindNode(conn.sourceNodeId);
        if (srcNode) {
            for (const auto& pin : srcNode->outputPins) {
                if (pin.id == conn.sourcePinId) {
                    connJson["sourcePinName"] = pin.name;
                    break;
                }
            }
        }

        connJson["targetNodeId"] = conn.targetNodeId.ToString();

        // 查找目标引脚名称
        auto* tgtNode = graph->FindNode(conn.targetNodeId);
        if (tgtNode) {
            for (const auto& pin : tgtNode->inputPins) {
                if (pin.id == conn.targetPinId) {
                    connJson["targetPinName"] = pin.name;
                    break;
                }
            }
        }

        connsJson.push_back(connJson);
    }

    return j;
}

bool MaterialSerializer::SaveToFile(const Graph* graph,
                                     const std::string& filePath) {
    auto json = Serialize(graph);

    std::ofstream file(filePath);
    if (!file.is_open()) {
        ME_LOG_ERROR("Failed to open file for writing: %s", filePath.c_str());
        return false;
    }

    file << json.dump(2);  // 缩进 2 空格
    file.close();

    ME_LOG_INFO("Material saved to: %s", filePath.c_str());
    return true;
}

Graph* MaterialSerializer::Deserialize(const nlohmann::json& j) {
    // 版本检查
    if (!j.contains("version")) {
        ME_LOG_ERROR("Invalid material file: no version");
        return nullptr;
    }

    std::string version = j["version"];
    ME_LOG_INFO("Loading material version: %s", version.c_str());

    auto* graph = new Graph;
    auto& factory = NodeFactory::GetInstance();  // 需要全局工厂
    // 或者传入工厂参数

    // 加载节点
    std::map<std::string, UUID> idMap;  // 字符串 ID → UUID

    for (const auto& nodeJson : j["nodes"]) {
        std::string type = nodeJson["type"];
        std::string idStr = nodeJson["id"];

        // 创建节点
        // 需要工厂来创建带引脚的节点
        // auto node = factory.Create(type, position);
        // 这里用简化方案：直接创建 Node 并设置属性

        auto node = MakeRef<Node>();
        node->id = UUID::FromString(idStr);
        node->typeName = type;
        node->title = nodeJson.value("title", type);

        auto& pos = nodeJson["position"];
        node->position = QPointF(pos[0], pos[1]);

        if (nodeJson.contains("parameters")) {
            node->parameters = nodeJson["parameters"];
        }

        // 需要设置引脚 — 使用 Expression 的引脚定义
        auto expr = ExpressionRegistry::GetInstance().Create(type);
        if (expr) {
            for (const auto& pinDesc : expr->GetInputPins()) {
                Pin pin;
                pin.id = UUID::Generate();
                pin.name = pinDesc.name;
                pin.type = pinDesc.type;
                pin.direction = EPinDataDirection::Input;
                pin.ownerNodeId = node->id;
                pin.defaultValue = pinDesc.defaultValue;
                node->inputPins.push_back(pin);
            }
            for (const auto& pinDesc : expr->GetOutputPins()) {
                Pin pin;
                pin.id = UUID::Generate();
                pin.name = pinDesc.name;
                pin.type = pinDesc.type;
                pin.direction = EPinDataDirection::Output;
                pin.ownerNodeId = node->id;
                node->outputPins.push_back(pin);
            }

            // 设置参数值
            for (const auto& param : expr->GetParameters()) {
                if (node->parameters.contains(param.name)) {
                    expr->SetParameter(param.name, node->parameters[param.name]);
                }
            }
        }

        graph->AddNode(node);
        idMap[idStr] = node->id;
    }

    // 加载连接
    if (j.contains("connections")) {
        for (const auto& connJson : j["connections"]) {
            std::string srcIdStr = connJson["sourceNodeId"];
            std::string tgtIdStr = connJson["targetNodeId"];
            std::string srcPinName = connJson["sourcePinName"];
            std::string tgtPinName = connJson["targetPinName"];

            // 查找节点
            auto srcIt = idMap.find(srcIdStr);
            auto tgtIt = idMap.find(tgtIdStr);
            if (srcIt == idMap.end() || tgtIt == idMap.end()) continue;

            auto* srcNode = graph->FindNode(srcIt->second);
            auto* tgtNode = graph->FindNode(tgtIt->second);
            if (!srcNode || !tgtNode) continue;

            // 查找引脚
            auto* srcPin = srcNode->FindOutputPin(srcPinName);
            auto* tgtPin = tgtNode->FindInputPin(tgtPinName);
            if (!srcPin || !tgtPin) continue;

            // 建立连接
            graph->Connect(srcPin->id, tgtPin->id);
        }
    }

    return graph;
}

Graph* MaterialSerializer::LoadFromFile(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        ME_LOG_ERROR("Failed to open file: %s", filePath.c_str());
        return nullptr;
    }

    nlohmann::json j;
    file >> j;
    file.close();

    ME_LOG_INFO("Material loaded from: %s", filePath.c_str());
    return Deserialize(j);
}
```

### 4. ExportDialog.h

```cpp
#pragma once
#include <QDialog>
#include <QComboBox>
#include <QCheckBox>
#include <QTextEdit>

class ExportDialog : public QDialog {
    Q_OBJECT
public:
    explicit ExportDialog(const std::string& hlslCode,
                          QWidget* parent = nullptr);

    QString GetExportPath() const;
    std::string GetExportFormat() const;

private:
    void SetupUI();
    void OnExportClicked();
    void OnPreviewClicked();

    std::string hlslCode_;
    QComboBox* formatCombo_;
    QTextEdit* preview_;
    QLineEdit* pathEdit_;
};
```

### 5. ExportDialog.cpp

```cpp
#include "UI/Private/ExportDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>

ExportDialog::ExportDialog(const std::string& hlslCode, QWidget* parent)
    : QDialog(parent), hlslCode_(hlslCode) {
    setWindowTitle("Export Shader");
    setMinimumSize(600, 500);
    SetupUI();
}

void ExportDialog::SetupUI() {
    auto* mainLayout = new QVBoxLayout(this);

    // 格式选择
    auto* formLayout = new QFormLayout;
    formatCombo_ = new QComboBox;
    formatCombo_->addItems({"HLSL Vertex Shader (*.vs.hlsl)",
                             "HLSL Pixel Shader (*.ps.hlsl)",
                             "HLSL (*.hlsl)",
                             "Plain Text (*.txt)"});
    formLayout->addRow("Format:", formatCombo_);

    pathEdit_ = new QLineEdit;
    auto* browseBtn = new QPushButton("Browse...");
    auto* pathLayout = new QHBoxLayout;
    pathLayout->addWidget(pathEdit_);
    pathLayout->addWidget(browseBtn);
    formLayout->addRow("File:", pathLayout);

    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        QString filter = formatCombo_->currentText();
        QString path = QFileDialog::getSaveFileName(
            this, "Export Shader", "", filter);
        if (!path.isEmpty()) pathEdit_->setText(path);
    });

    mainLayout->addLayout(formLayout);

    // 预览
    preview_ = new QTextEdit;
    preview_->setReadOnly(true);
    preview_->setFont(QFont("Consolas", 10));
    preview_->setPlainText(QString::fromStdString(hlslCode_));
    mainLayout->addWidget(new QLabel("Preview:"));
    mainLayout->addWidget(preview_);

    // 按钮
    auto* btnLayout = new QHBoxLayout;
    auto* exportBtn = new QPushButton("Export");
    auto* cancelBtn = new QPushButton("Cancel");
    btnLayout->addStretch();
    btnLayout->addWidget(exportBtn);
    btnLayout->addWidget(cancelBtn);
    mainLayout->addLayout(btnLayout);

    connect(exportBtn, &QPushButton::clicked, this, [this]() {
        QString path = pathEdit_->text();
        if (path.isEmpty()) return;

        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream stream(&file);
            stream << QString::fromStdString(hlslCode_);
            file.close();
            accept();
        }
    });

    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}
```

### 6. 集成到 MainWindow

更新 `MainWindow.cpp` 中的文件操作：

```cpp
#include "Core/Public/MaterialSerializer.h"
#include "UI/Private/ExportDialog.h"

void MainWindow::OnSaveMaterial() {
    if (currentFilePath_.isEmpty()) {
        OnSaveAsMaterial();
        return;
    }
    MaterialSerializer::SaveToFile(graph_, currentFilePath_.toStdString());
    statusLabel_->setText("Saved: " + currentFilePath_);
}

void MainWindow::OnSaveAsMaterial() {
    QString path = QFileDialog::getSaveFileName(
        this, "Save Material", "",
        "Material Files (*.mat.json);;All Files (*)");
    if (path.isEmpty()) return;
    currentFilePath_ = path;
    OnSaveMaterial();
}

void MainWindow::OnOpenMaterial() {
    QString path = QFileDialog::getOpenFileName(
        this, "Open Material", "",
        "Material Files (*.mat.json);;All Files (*)");
    if (path.isEmpty()) return;

    auto* newGraph = MaterialSerializer::LoadFromFile(path.toStdString());
    if (!newGraph) {
        QMessageBox::warning(this, "Error", "Failed to load material");
        return;
    }

    // 替换当前图
    delete graph_;
    graph_ = newGraph;
    currentFilePath_ = path;

    // 更新 UI
    graphWidget_->SyncFromGraph();
    statusLabel_->setText("Loaded: " + path);

    // 自动编译
    OnCompile();
}

void MainWindow::OnExportShader() {
    auto result = compiler_->Compile(graph_);
    if (!result.success) {
        QMessageBox::warning(this, "Error",
            QString::fromStdString(result.errorMessage));
        return;
    }

    ExportDialog dialog(result.hlslCode, this);
    dialog.exec();
}

void MainWindow::OnNewMaterial() {
    // 确认保存
    if (!currentFilePath_.isEmpty()) {
        auto ret = QMessageBox::question(
            this, "New Material",
            "Save current material before creating new one?",
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (ret == QMessageBox::Cancel) return;
        if (ret == QMessageBox::Yes) OnSaveMaterial();
    }

    // 创建新图
    delete graph_;
    graph_ = new Graph(this);
    currentFilePath_.clear();

    // 重新连接信号
    connect(graph_, &Graph::GraphChanged, this, [this]() {
        statusLabel_->setText("Material modified");
    });

    graphWidget_->SyncFromGraph();
    propertyPanel_->Clear();
    codePanel_->Clear();
    statusLabel_->setText("New material");
}
```

---

## 反射驱动的序列化（基于 ClassDesc）

序列化是反射系统的天然应用场景。所有 Expression 已经通过 `ME_BEGIN_CLASS/ME_FIELD` 注册了字段元信息（名字、偏移量、类型），序列化直接遍历 `ClassDesc`，**完全绕过 `Node::parameters` 这个 json 中间层**。

### 反射版 Serialize

```cpp
nlohmann::json SerializeNode(const Node& node) {
    nlohmann::json nodeJson;
    nodeJson["id"] = node.id.ToString();
    nodeJson["type"] = node.typeName;
    nodeJson["position"] = {node.position.x(), node.position.y()};

    // 反射路径：从 Expression 实例直接序列化字段
    auto expr = ExpressionRegistry::GetInstance().Create(node.typeName);
    if (!expr) {
        // 没注册的表达式类型，跳过参数
        return nodeJson;
    }

    // 先把 Node::parameters 同步到 Expression 字段
    // （Node::parameters 仍是 UI 编辑时的临时载体，最终序列化以 Expression 字段为准）
    for (const auto& [k, v] : node.parameters.items()) {
        expr->SetParameter(k, v);
    }

    // 遍历 ClassDesc 所有字段，每个字段用 Accessor 序列化
    const reflection::ClassDesc* desc = expr->GetClassDesc();
    nlohmann::json params;
    for (const reflection::FieldDesc& f : desc->fields) {
        params[f.name] = expr->GetParameter(f.name);
    }
    nodeJson["parameters"] = params;
    return nodeJson;
}
```

### 反射版 Deserialize

```cpp
void DeserializeNodeParameters(Node& node, const nlohmann::json& paramsJson) {
    auto expr = ExpressionRegistry::GetInstance().Create(node.typeName);
    if (!expr) return;

    const reflection::ClassDesc* desc = expr->GetClassDesc();
    // 遍历 ClassDesc，从 json 读字段（字段名拼写错误会被静默跳过，不会崩）
    for (const reflection::FieldDesc& f : desc->fields) {
        if (paramsJson.contains(f.name)) {
            expr->SetParameter(f.name, paramsJson[f.name]);
        }
    }
    // 把 Expression 字段同步回 Node::parameters（供 UI 编辑用）
    for (const reflection::FieldDesc& f : desc->fields) {
        node.parameters[f.name] = expr->GetParameter(f.name);
    }
}
```

### 反射序列化的好处

| 维度 | 旧方案（json 中间层） | 反射方案（ClassDesc） |
|---|---|---|
| 字段名一致性 | `"R"` 字符串硬编码，易写错 | 宏字符串化 `#FieldName`，编译期生成 |
| 类型安全 | `val.is_number()` 运行时检查 | 模板偏特化编译期检查 |
| 字段新增 | 改 3 处（声明、Get、Set） | 加 1 行 `ME_FIELD` |
| 字段重命名 | 改 N 处（所有字符串引用） | 改 1 处（成员声明 + 宏参数） |
| 序列化鲁棒性 | 字段名错 = 数据丢失 | 字段名错 = 静默跳过（不崩） |

JSON 文件格式保持兼容：依然是 `parameters` 字段下的 key-value 映射，老存档能读、新存档能写。

---

## 验证

1. 创建一个材质图（Constant3Vector → Add → Output）
2. File → Save → 选择路径保存
3. 打开保存的 `.mat.json` 文件检查 JSON 格式正确
4. File → New → 清空画布
5. File → Open → 选择刚保存的文件 → 图恢复
6. 修改参数 → 保存 → 重新加载 → 参数正确
7. File → Export Shader → 选择格式和路径 → 导出 HLSL 文件
8. 导出的 HLSL 文件可以用其他工具验证（如 fxc 编译器）

---

## UE5 参考（相对 `Engine/` 路径）

- `Engine/Source/Runtime/Engine/Private/Materials/Material.cpp` — 搜索 `Serialize`
- `Engine/Source/Runtime/Core/Private/Serialization/` — 序列化框架（对照我们的 `MaterialSerializer`）

### 对照 UE 序列化

| 我们的 | UE | 作用 |
|--------|-----|------|
| `MaterialSerializer`（JSON 文本）| `UMaterial::Serialize`（二进制 .uasset）| 保存/加载 |
| `node->parameters`（JSON map）| `UPROPERTY` 字段自动序列化 | 节点参数 |
| 引脚名称存连接（不是引脚 ID）| `FExpressionInput` 存对象引用 | 连接 |

**三个关键差异**：

1. **UE 用二进制 + 反射自动序列化**（`UPROPERTY` 字段自动进序列化流，不用手写每个字段）。我们用 **JSON + 手动序列化**（`MaterialSerializer::Serialize` 手写节点/连接每个字段）——好处是 `.mat.json` 人能读、能 git diff；代价是加字段要改序列化代码（反射能消除这个，见课5）。

2. **连接存储**：UE 的 `FExpressionInput` 直接存对象引用（表达式指针）。我们用**节点 ID + 引脚名称**（加载时按名查找引脚）——因为我们的引脚 ID 每次创建都变，但名称稳定。

3. **版本兼容**：UE 的 `FArchive` 有完善的版本控制 + 向后兼容（旧版本资产能加载）。我们用简单的 `"version": "0.1.0"` 字段 + 加载时检查——教学够用。

### 扩展预告：参数系统（块4，见 `lesson06-extension.md`）

当前 `node->parameters` 存的是**节点参数**（Constant 的 value、Constant3Vector 的 R/G/B）。块4 规划的**材质级参数**（`ScalarParameter`/`VectorParameter`/`TextureParameter`）是另一种——它们是**暴露给材质实例的 uniform**（蓝图/C++ 外部能改的变量），要单独存到材质参数表，不在 `node->parameters` 里。块4 在 `lesson06-extension.md` 详细规划，课18 先用 node->parameters。

> **搜索关键词**（UE 源码）：`UMaterial::Serialize`、`FArchive`、`FMaterialResource::Serialize`、`ExportMaterialFunctions`。

---

## MaterialAttributes 打包节点的序列化（课 6 `MCT_MaterialAttributes` 的节点侧消费）

课 6 引入的 `MCT_MaterialAttributes` 类型位在本课落地它的**节点**：Make/Break MaterialAttributes（对照 UE `MaterialExpressionMakeMaterialAttributes` / `BreakMaterialAttributes`）。

**节点的语义**（编译侧在课 8 生成代码时展开，本课只管图数据和序列化）：

- **MakeMaterialAttributes**：8 个输入引脚（BaseColor/Metallic/Roughness/Normal/Emissive/Opacity/AO/Tangent），1 个输出引脚（类型 `MCT_MaterialAttributes`）——把零散属性线打包成一根
- **BreakMaterialAttributes**：1 个输入引脚（`MCT_MaterialAttributes`），8 个输出——反向拆包
- 输出引脚类型在 `GetOutputPins()` 里声明为 `MCT_MaterialAttributes`，编译器的算术拦截（课 6 `GetArithmeticResultType` 对它返回 Unknown）保证它只能连进 Break 节点或材质根

**序列化要点**（本课的实现内容）：

```json
// MakeMaterialAttributes 节点的 JSON——和普通节点同构，无特殊处理：
// 引脚类型不序列化（加载时由 Expression::GetInputPins/GetOutputPins 重建），
// 连接按 "节点ID + 引脚名" 存（引脚名是 BaseColor/Metallic 等属性名）
{
  "type": "MakeMaterialAttributes",
  "id": "...",
  "pos": [120, 340],
  "parameters": {},
  "connections": [
    { "from_node": "n1", "from_pin": "RGB",  "to_pin": "BaseColor" },
    { "from_node": "n3", "from_pin": "Value", "to_pin": "Roughness" }
  ]
}
```

关键点：**`MCT_MaterialAttributes` 类型位不出现在序列化数据里**——类型是节点的固有元数据（`GetOutputPins()` 每次重建时报告），不是实例状态。序列化只存「谁连谁」，类型检查在加载后编译时重做。这和 UE 一致（.uasset 里也不存引脚类型，存 `FExpressionInput` 对象引用）。

---

## 完成标志

- [ ] 保存材质图到 JSON 文件
- [ ] 从 JSON 文件加载材质图
- [ ] 加载后节点、连接、参数完全恢复
- [ ] 导出 HLSL 着色器文件
- [ ] 新建/打开/保存/另存为操作正确
- [ ] 未保存提示
- [ ] Make/Break MaterialAttributes 节点可序列化（连接按引脚名 BaseColor/Metallic/... 存取，类型位不进序列化流）
