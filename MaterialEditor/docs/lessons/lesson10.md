# 课10：Qt 主窗口布局

## 目标

搭建 Qt 主窗口框架，使用 QDockWidget 实现可停靠面板布局，和 UE5 材质编辑器一样可以自由拖拽面板。

---

## 背景知识

UE5 的材质编辑器布局：

```
┌─────────────────────────────────────────────────────┐
│ 菜单栏: File | Edit | View | Material              │
│ 工具栏: [新建] [打开] [保存] [编译] [预览球体▼]      │
├──────────┬────────────────────────┬─────────────────┤
│          │                        │                 │
│  节点    │    材质图画布            │   属性面板      │
│  调色板  │  (MaterialGraphWidget)  │  (PropertyPanel)│
│          │                        │                 │
│          │                        │                 │
├──────────┴────────────────────────┴─────────────────┤
│  代码预览 / 着色器统计 / 3D 预览                      │
└─────────────────────────────────────────────────────┘
```

UE5 用 Slate 的 `SDockSplitter` 实现可停靠布局。Qt 有等价的 `QDockWidget`，行为一致：面板可以拖拽、浮动、堆叠为标签页。

> **节点添加约定**（影响课13 调色板）：所有节点类型都注册在 `NodeFactory`，`Graph::AddNode(typeName, pos)` 是**唯一**添加入口（内部经工厂构造，不手动 new Node）。其中 `MaterialOutput`（材质输出节点）注册时带 `hidden=true`，由 `Graph` 自动创建一个、**不进调色板**——课13 调色板遍历 `GetAllTypes()` 时按 `hidden` 过滤掉它。

---

## 操作步骤

### 1. 创建文件

```
src/UI/Public/MainWindow.h
src/UI/Private/MainWindow.cpp
```

### 2. MainWindow.h

```cpp
#pragma once
#include <QMainWindow>
#include <QDockWidget>
#include <QToolBar>
#include <QMenuBar>
#include <QStatusBar>
#include <QLabel>
#include <QComboBox>
#include <QAction>

// 前向声明各面板（后续课程实现）
class MaterialGraphWidget;
class NodePalettePanel;
class PropertyPanel;
class CodePreviewPanel;
class ViewportPanel;
class StatsPanel;

class Graph;
class NodeFactory;
class MaterialCompiler;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

    // 获取各组件（供其他模块访问）
    Graph* GetGraph() const { return graph_; }
    NodeFactory* GetNodeFactory() const { return factory_; }

public slots:
    // 文件操作
    void OnNewMaterial();
    void OnOpenMaterial();
    void OnSaveMaterial();
    void OnSaveAsMaterial();
    void OnExportShader();

    // 编辑操作
    void OnCompile();
    void OnDeleteSelected();

    // 视图操作
    void OnFitToView();
    void OnToggleGrid();

private:
    void SetupUI();
    void SetupMenuBar();
    void SetupToolBar();
    void SetupDockWidgets();
    void SetupStatusBar();
    void ConnectSignals();

    // 数据模型
    Graph* graph_ = nullptr;
    NodeFactory* factory_ = nullptr;
    MaterialCompiler* compiler_ = nullptr;

    // 面板
    MaterialGraphWidget* graphWidget_ = nullptr;
    NodePalettePanel* palettePanel_ = nullptr;
    PropertyPanel* propertyPanel_ = nullptr;
    CodePreviewPanel* codePanel_ = nullptr;
    ViewportPanel* viewportPanel_ = nullptr;
    StatsPanel* statsPanel_ = nullptr;

    // UI 组件
    QComboBox* previewMeshCombo_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* compileStatus_ = nullptr;

    // 当前文件路径
    QString currentFilePath_;
};
```

### 3. MainWindow.cpp

```cpp
#include "UI/Public/MainWindow.h"
#include "MaterialGraph/Public/Graph.h"
#include "MaterialGraph/Public/NodeFactory.h"
#include "Compiler/Public/MaterialCompiler.h"
#include "Expression/Public/ExpressionRegistry.h"

// 后续课程实现的面板，先用占位 QWidget
// #include "UI/MaterialGraphWidget.h"
// #include "UI/Panels/NodePalettePanel.h"
// #include "UI/Panels/PropertyPanel.h"
// #include "UI/Panels/CodePreviewPanel.h"
// #include "UI/Panels/ViewportPanel.h"
// #include "UI/Panels/StatsPanel.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>
#include <QSplitter>
#include <QShortcut>
#include <QKeySequence>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    // 初始化数据模型
    RegisterAllExpressions();

    factory_ = new NodeFactory(this);
    // 注册表达式到工厂（课7的内容，这里先注册几个基础类型）
    factory_->Register("ExprConstant", "Constant", "Constants", {
        {"Value", EValueType::Float1, EPinDataDirection::Output}
    });
    factory_->Register("ExprConstant3Vector", "Constant3Vector", "Constants", {
        {"Output", EValueType::Float3, EPinDataDirection::Output}
    });
    factory_->Register("ExprAdd", "Add", "Math", {
        {"A", EValueType::Float1, EPinDataDirection::Input, 0.0f},
        {"B", EValueType::Float1, EPinDataDirection::Input, 0.0f},
        {"Result", EValueType::Float1, EPinDataDirection::Output}
    });
    factory_->Register("ExprMultiply", "Multiply", "Math", {
        {"A", EValueType::Float1, EPinDataDirection::Input, 0.0f},
        {"B", EValueType::Float1, EPinDataDirection::Input, 1.0f},
        {"Result", EValueType::Float1, EPinDataDirection::Output}
    });
    // ... 其他表达式注册

    graph_ = new Graph(this);
    compiler_ = new MaterialCompiler();

    // 构建 UI
    SetupUI();

    setWindowTitle("Material Editor");
    resize(1400, 900);
}

MainWindow::~MainWindow() {
    delete compiler_;
}

void MainWindow::SetupUI() {
    SetupMenuBar();
    SetupToolBar();
    SetupDockWidgets();
    SetupStatusBar();
    ConnectSignals();
}

void MainWindow::SetupMenuBar() {
    // === 文件菜单 ===
    auto* fileMenu = menuBar()->addMenu("&File");

    auto* newAction = fileMenu->addAction("&New Material");
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &MainWindow::OnNewMaterial);

    auto* openAction = fileMenu->addAction("&Open...");
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::OnOpenMaterial);

    auto* saveAction = fileMenu->addAction("&Save");
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, &MainWindow::OnSaveMaterial);

    auto* saveAsAction = fileMenu->addAction("Save &As...");
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction, &QAction::triggered, this, &MainWindow::OnSaveAsMaterial);

    fileMenu->addSeparator();

    auto* exportAction = fileMenu->addAction("&Export Shader...");
    connect(exportAction, &QAction::triggered, this, &MainWindow::OnExportShader);

    fileMenu->addSeparator();

    auto* quitAction = fileMenu->addAction("&Quit");
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    // === 编辑菜单 ===
    auto* editMenu = menuBar()->addMenu("&Edit");

    auto* deleteAction = editMenu->addAction("&Delete Selected");
    deleteAction->setShortcut(QKeySequence::Delete);
    connect(deleteAction, &QAction::triggered, this, &MainWindow::OnDeleteSelected);

    // === 视图菜单 ===
    auto* viewMenu = menuBar()->addMenu("&View");

    auto* fitAction = viewMenu->addAction("&Fit to View");
    fitAction->setShortcut(QKeySequence("Ctrl+F"));
    connect(fitAction, &QAction::triggered, this, &MainWindow::OnFitToView);

    // === 材质菜单 ===
    auto* materialMenu = menuBar()->addMenu("&Material");

    auto* compileAction = materialMenu->addAction("&Compile");
    compileAction->setShortcut(QKeySequence("Ctrl+Shift+C"));
    connect(compileAction, &QAction::triggered, this, &MainWindow::OnCompile);
}

void MainWindow::SetupToolBar() {
    auto* toolbar = addToolBar("Main");
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(24, 24));

    // 工具栏按钮（后续替换为图标）
    toolbar->addAction("New", this, &MainWindow::OnNewMaterial);
    toolbar->addAction("Open", this, &MainWindow::OnOpenMaterial);
    toolbar->addAction("Save", this, &MainWindow::OnSaveMaterial);
    toolbar->addSeparator();
    toolbar->addAction("Compile", this, &MainWindow::OnCompile);
    toolbar->addSeparator();

    // 预览网格选择
    previewMeshCombo_ = new QComboBox();
    previewMeshCombo_->addItems({"Sphere", "Cube", "Plane", "Cylinder", "Torus"});
    toolbar->addWidget(previewMeshCombo_);
}

void MainWindow::SetupDockWidgets() {
    // 设置中央部件为占位（后续替换为 MaterialGraphWidget）
    auto* centralPlaceholder = new QWidget(this);
    setCentralWidget(centralPlaceholder);

    // === 左侧：节点调色板 ===
    auto* paletteDock = new QDockWidget("Node Palette", this);
    paletteDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    // 后续替换为真实面板：paletteDock->setWidget(palettePanel_);
    auto* palettePlaceholder = new QWidget;
    palettePlaceholder->setMinimumWidth(200);
    paletteDock->setWidget(palettePlaceholder);
    addDockWidget(Qt::LeftDockWidgetArea, paletteDock);

    // === 右侧：属性面板 ===
    auto* propertyDock = new QDockWidget("Details", this);
    propertyDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    auto* propertyPlaceholder = new QWidget;
    propertyPlaceholder->setMinimumWidth(250);
    propertyDock->setWidget(propertyPlaceholder);
    addDockWidget(Qt::RightDockWidgetArea, propertyDock);

    // === 底部左：代码预览 ===
    auto* codeDock = new QDockWidget("HLSL Code", this);
    codeDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    auto* codePlaceholder = new QWidget;
    codePlaceholder->setMinimumHeight(200);
    codeDock->setWidget(codePlaceholder);
    addDockWidget(Qt::BottomDockWidgetArea, codeDock);

    // === 底部右：3D 预览视口 ===
    auto* viewportDock = new QDockWidget("Viewport", this);
    viewportDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    auto* viewportPlaceholder = new QWidget;
    viewportPlaceholder->setMinimumSize(300, 200);
    viewportDock->setWidget(viewportPlaceholder);
    addDockWidget(Qt::BottomDockWidgetArea, viewportDock);

    // 将底部两个面板堆叠为标签页
    tabifyDockWidget(codeDock, viewportDock);
    codeDock->raise();  // 默认显示代码预览
}

void MainWindow::SetupStatusBar() {
    statusLabel_ = new QLabel("Ready");
    compileStatus_ = new QLabel("");

    statusBar()->addWidget(statusLabel_, 1);
    statusBar()->addPermanentWidget(compileStatus_);
}

void MainWindow::ConnectSignals() {
    // 图变化时更新状态栏
    connect(graph_, &Graph::GraphChanged, this, [this]() {
        statusLabel_->setText("Material modified");
        compileStatus_->setText("Not compiled");
        compileStatus_->setStyleSheet("color: orange;");
    });
}

// ====== 槽函数实现 ======

void MainWindow::OnNewMaterial() {
    // 清空当前图，创建新的
    // 后续实现：确认保存当前材质
    ME_LOG_INFO("New material");
}

void MainWindow::OnOpenMaterial() {
    QString path = QFileDialog::getOpenFileName(
        this, "Open Material", "", "Material Files (*.mat.json)");
    if (path.isEmpty()) return;

    // 后续课16实现加载
    ME_LOG_INFO("Open: %s", path.toStdString().c_str());
}

void MainWindow::OnSaveMaterial() {
    if (currentFilePath_.isEmpty()) {
        OnSaveAsMaterial();
        return;
    }
    // 后续课16实现保存
    ME_LOG_INFO("Save: %s", currentFilePath_.toStdString().c_str());
}

void MainWindow::OnSaveAsMaterial() {
    QString path = QFileDialog::getSaveFileName(
        this, "Save Material", "", "Material Files (*.mat.json)");
    if (path.isEmpty()) return;

    currentFilePath_ = path;
    OnSaveMaterial();
}

void MainWindow::OnExportShader() {
    QString path = QFileDialog::getSaveFileName(
        this, "Export Shader", "",
        "HLSL (*.hlsl);;All Files (*)");
    if (path.isEmpty()) return;

    // 先编译
    OnCompile();
    // 后续课16实现导出
    ME_LOG_INFO("Export: %s", path.toStdString().c_str());
}

void MainWindow::OnCompile() {
    auto result = compiler_->Compile(graph_);

    if (result.success) {
        compileStatus_->setText("Compiled OK");
        compileStatus_->setStyleSheet("color: green;");
        statusLabel_->setText(QString("Compiled: %1 bytes HLSL")
            .arg(result.hlslCode.size()));
        // 后续：更新代码预览面板
        // codePanel_->SetCode(result.hlslCode);
        // 后续：更新3D预览
        // viewportPanel_->SetMaterialResult(result);
    } else {
        compileStatus_->setText("Compile Error");
        compileStatus_->setStyleSheet("color: red;");
        statusLabel_->setText(QString("Error: %1").arg(
            QString::fromStdString(result.errorMessage)));
        // 后续：在代码面板中高亮错误
    }
}

void MainWindow::OnDeleteSelected() {
    // 后续实现：获取当前选中节点/连线并删除
    ME_LOG_INFO("Delete selected");
}

void MainWindow::OnFitToView() {
    // 后续实现：调整图视图以显示所有节点
    ME_LOG_INFO("Fit to view");
}

void MainWindow::OnToggleGrid() {
    // 切换网格背景显示
}
```

### 4. 更新 main.cpp

```cpp
#include <QApplication>
#include "UI/Public/MainWindow.h"
#include "Core/Public/Logger.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // 设置应用信息
    QApplication::setApplicationName("Material Editor");
    QApplication::setApplicationVersion("0.1.0");
    QApplication::setOrganizationName("MaterialEditor");

    MainWindow window;
    window.show();

    ME_LOG_INFO("Material Editor started");
    return app.exec();
}
```

---

## 验证

编译运行后应看到：

1. 1400×900 的主窗口
2. 菜单栏：File | Edit | View | Material
3. 工具栏：New, Open, Save, Compile 按钮 + 预览网格下拉框
4. 左侧：Node Palette 面板（空白占位）
5. 右侧：Details 面板（空白占位）
6. 底部：HLSL Code 和 Viewport 标签页切换
7. 状态栏：显示 "Ready"
8. 面板可以拖拽、浮动、重新排列

尝试：
- 拖拽面板到不同位置
- 双击面板标题栏浮动/恢复
- 点击 Compile 按钮（此时应该报错，因为没有节点）
- Ctrl+N, Ctrl+O, Ctrl+S 快捷键工作

---

## UE5 参考（相对 `Engine/` 路径）

- `Engine/Source/Editor/MaterialEditor/Private/MaterialEditor.h` — UE 主编辑器类（对照我们的 `MainWindow`）
- `Engine/Source/Editor/MaterialEditor/Private/MaterialEditor.cpp` — `InitToolMenuContext` 看菜单/工具栏注册
- `Engine/Source/Editor/MaterialEditor/Private/SMaterialEditorToolBar.cpp` — 工具栏实现

### 对照 UE 布局系统（Slate vs Qt）

| 我们的（Qt）| UE（Slate）| 作用 |
|------------|-----------|------|
| `QMainWindow` | `IMaterialEditor` 接口 + `SDockTab` 容器 | 主窗口 |
| `QDockWidget` | `SDockableTab` / `SDockSplitter` | 可停靠、浮动、堆叠的面板 |
| `addDockWidget(LeftDockArea, dock)` | `FLayoutExtender` + `FDefaultPlacementInfo` | 面板默认位置布局 |
| `tabifyDockWidget(a, b)` | `SDockSplitter` 的标签页模式 | 堆叠为标签页 |
| `menuBar()->addMenu("&File")` | `FExtender` + `FMenuEntryDesc` | 菜单栏 |
| `addToolBar` + `QAction` | `SMaterialEditorToolBar`（Slate widget）| 工具栏 |

**关键差异**：
1. **UE 用 Slate（声明式 C++ UI 框架）**，我们用 Qt Widgets——两套完全不同的 UI 框架，但**布局概念一致**（可停靠面板、菜单、工具栏）。
2. **UE 的布局是可序列化的**（`FLayoutExtender` 把面板布局存进编辑器配置，下次打开恢复）——我们的 `QMainWindow` 用 `saveState()/restoreState()` 能做到同样的事（课18 保存时加上）。
3. **UE 工具栏是 Slate widget**（`SMaterialEditorToolBar`），Qt 用 `QToolBar` + `QAction`，更简单。

> **搜索关键词**（UE 源码）：`MaterialEditor.h`、`FLayoutExtender`、`SDockableTab`、`SMaterialEditorToolBar`。

---

## 完成标志

- [ ] 主窗口显示正确，面板布局可拖拽
- [ ] 菜单栏和工具栏正确显示
- [ ] 快捷键工作（Ctrl+N/O/S/Delete）
- [ ] Compile 按钮触发编译（即使此时报错）
- [ ] 状态栏显示编译结果
