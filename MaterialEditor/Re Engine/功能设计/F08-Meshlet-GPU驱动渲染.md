# F08 — Meshlet / GPU 驱动渲染

> 加载一个**超复杂模型**时，用 GPU 驱动的 meshlet 管线渲染，用 Profiler（F02）量化"优化前 vs 优化后"的效果。
> 类别：A 渲染进阶。**执行序第 8**：依赖 F02（量化）+ F07（重资产）。

## 1. 目标与动机

**变成什么功能**：渲染器有两条可切换后端——① 经典顶点管线 ② GPU 驱动 meshlet 管线。加载百万级三角形的高模后，切换两条后端，Profiler 直接显示 CPU 提交时间、被剔除三角形数、帧时间的差异。

**为什么不是 toy**（本项目转正它的关键）：
- meshlet 的价值是**去掉 CPU→GPU 的 draw-call 瓶颈 + GPU 端剔除**——只有场景足够复杂时才看得到收益。
- 加载超复杂模型（课 21 + F07 摄影测量高模）提供"足够复杂"的场景，使优化收益**可量化、可演示**。
- 不是玩具：它解决真实存在的性能问题，且用数字证明。

## 2. 涉及课次与文件

| 课次 | 位置 | 做什么 |
|---|---|---|
| 课 21 | `src/Renderer/` 网格导入 | 导入超复杂模型（百万级三角形） |
| 课 14-16 | `src/Renderer/` | 经典顶点管线（基线） |
| 课 20 | 渲染器扩展 | meshlet 烘焙 + GPU 驱动剔除 + indirect draw 后端 |

**前置依赖**：F02（Profiler，量化效果）、F07（重资产来源）。

## 3. 架构改动（逐层）

**资产 / 网格导入层**
- 导入时（或离线工具）把网格**烘焙成 meshlet**：每簇 128 顶点 / 128 三角形（RE Engine 规格），每簇存包围盒（cone + AABB）用于剔除。
- meshlet 数据布局：顶点 index buffer 压缩、三角形索引打包（REAC2025 有属性压缩细节）。

**渲染器层（两条后端）**
- **后端① 经典顶点管线**：`DrawIndexed`，CPU 逐 draw 提交（基线，吃 CPU）。
- **后端② GPU 驱动 meshlet**：
  - compute shader 做 **frustum 剔除 + 遮挡剔除**（two-phase：粗筛 + Hi-Z 精筛），输出存活 meshlet 的 indirect draw 参数。
  - `ExecuteIndirect` 一次提交，CPU 几乎不参与逐 draw。
  - （硬件升级项）**mesh shader** 路径：用 amplification shader + mesh shader 替代 compute+indirect，硬件原生。

**UI 层**
- 预览面板加"渲染后端"下拉（经典 / GPU 驱动 / Mesh Shader），配合 F02 Profiler 看数字。

## 4. RE Engine 资料对应

| 步骤 | RE Engine 资料 |
|---|---|
| meshlet 规格、two-phase 遮挡剔除、visibility buffer、性能数据 | REAC2025 Capcom Meshlet（slides）：https://enginearchitecture.org/downloads/REAC_2025_Capcom.pdf ｜ 中文完整译文：https://zhuanlan.zhihu.com/p/1999214629591738027 |
| 不用 mesh shader 也能 GPU 驱动（compute + indirect draw） | CEDEC2018 RE2/DMC5 优化（94 页）：https://www.docswell.com/s/CAPCOM_RandD/ZXYVJG-cedec2018 |

## 5. 里程碑

- **M1 重模型导入**（课 21 + F07）：能加载百万级三角形模型，经典管线能渲染（建立基线）。
- **M2 基线 + Profiler**：经典管线渲染重模型，F02 记录 CPU 提交时间、帧时间（这是要打败的基线）。
- **M3 meshlet 烘焙**：导入时把网格切成 meshlet，存包围盒。
- **M4 compute 剔除 + indirect draw**：frustum + 遮挡剔除，`ExecuteIndirect` 提交——**此时切换后端，F02 应显示 CPU 提交时间大幅下降、被剔除三角形数 > 0**。
- **M5（可选）mesh shader 路径**：有 DX12 Ultimate 硬件时加硬件原生路径。

## 6. 验收标准

- 能加载超复杂模型，且经典管线 vs GPU 驱动两条路径都能渲染同一模型。
- Profiler（F02）量化对比：GPU 驱动路径下 **CPU draw 提交时间显著下降**（核心收益）。
- 遮挡剔除可见效果：被挡住的 meshlet 数可统计（剔除率）。
- 切换后端只改渲染路径，材质/光照结果一致（证明是"可切换后端"）。

## 7. 诚实说明

- **mesh shader 要 DX12 Ultimate**（RTX20+/RDNA2+）；但 **meshlet + compute + indirect draw 任何 DX12 卡都能跑**，收益一样。**先做 compute+indirect（宽兼容，RE Engine CEDEC2018 主力路线），mesh shader 作为有新卡时的升级**——不被硬件绑死。
- **必须配合 F02 Profiler**：没有量化面板，优化效果看不见，这个功能就退化成 toy。
- **重资产来源**：依赖课 21 + F07（摄影测量高模）提供足够复杂的模型；用简单球测不出收益。
- **协同弧**：`F07 重资产 → 课21 → F08 优化 → F02 量化` 是一条完整特性线。
