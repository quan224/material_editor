# F02 — 渲染器 Profiler 面板

> 编辑器里一个性能面板，实时显示帧时间、各 pass GPU 耗时、draw call 数、三角形数、显存。它是 F04/F08 等所有渲染优化的"眼睛"。
> 类别：B 引擎工程。**执行序第 2**：紧随 F01 地基；建议先于其他渲染优化做。

## 1. 目标与动机

**变成什么功能**：编辑器一个 dock 面板，实时显示：
- 总帧时间 / FPS（CPU + GPU 分列）
- 每个 render pass 的 GPU 耗时（G-Buffer / 光照 / 阴影 / 后处理 …）
- draw call 数、提交三角形数、被剔除三角形数
- 显存占用（粗粒度，数据来自 F01）

**为什么不是 toy**：
- DX12 项目**本来就需要**性能可视——没有它，F08 meshlet 优化、F04 RT 开关的效果全是玄学。
- 它是"自动性能计测"理念在编辑器里的落地，RE Engine 把这套做成了全作品通用基建。
- 让后面每个优化特性都能用数字说话，是基础设施级功能。

## 2. 涉及课次与文件

| 课次 | 位置 | 做什么 |
|---|---|---|
| 课 14-16 | `src/Renderer/` | 渲染器各 pass 插 timestamp query |
| 课 19/20 | UI（Qt dock） | 性能面板可视化 |

**前置依赖**：课 14-16 渲染器；显存数值最好接 F01。

## 3. 架构改动（逐层）

**渲染器层（L5）— 数据采集**
- **GPU timestamp**：用 DX12 的 query heap（`D3D12_QUERY_TYPE_TIMESTAMP`），在每个 pass 前后 `EndBeginQuery`，读回时间戳算 pass 耗时。
  - 注意：GPU query 有**一帧延迟**（结果下一帧才可读），要正确处理（读上一帧的 query 结果显示当前帧）。
- **CPU 计时**：记录每帧 CPU 提交（录制 command list）耗时、draw call 计数、三角形计数（从 meshlet/indirect draw 的 count buffer 读，配合 F08）。
- **显存统计**：从 F01 分配器读已分配/驻留量。

**采集层设计（建议）**
- 一个 `FrameStats` 结构体，渲染器每帧填充，UI 每帧读取；保留最近 N 帧滑动窗口做折线图。

**UI 层（课 19/20，Qt）**
- `QDockWidget` 面板：顶部大数字（FPS / frame time），中间 pass 耗时柱状图/列表（彩色按 pass），底部 draw call / 三角形 / 显存数值。

## 4. RE Engine 资料对应

| 步骤 | RE Engine 资料 |
|---|---|
| 自动计测系统设计（全作品通用、每日采集） | CEDEC2018「正確なパフォーマンス情報を毎日蓄積！」：https://www.docswell.com/s/CAPCOM_RandD/5DE486-cedec2018 |
| profiler 工具形态 | RE:2023 REProfiler（工具链 RELauncher/REProfiler/REUI/REFlows/REAssetStream） |

## 5. 里程碑

- **M1 GPU timestamp 采集**：渲染器各 pass 插 query，能读回单 pass GPU 耗时（先打印到日志）。
- **M2 CPU/draw call 统计**：记录 CPU 提交时间、draw call / 三角形数。
- **M3 Qt 面板**：dock 显示实时数值 + 帧时间折线 + pass 耗时列表。
- **M4（可选）落盘报告**：每帧/每日写性能日志（RE Engine 自动计测理念），便于回归对比。

## 6. 验收标准

- 面板实时刷新，FPS / frame time 正确。
- 各 pass GPU 耗时可读、求和约等于总 GPU 时间。
- **能对比开关某特性前后的数字**——例如关掉后处理 pass 该耗时归零；切 F08 后端，CPU 提交时间下降。这是它作为"优化眼睛"的核心验收。

## 7. 诚实说明

- **GPU query 延迟**：timestamp 结果下一帧才可读，新手常踩坑（显示成上一帧）。注释里写清楚。
- **精度**：GPU timestamp 频率要从 `GetTimestampFrequency` 拿，别假设。
- **本功能先做**：F04（RT 开关）、F08（meshlet 优化）都依赖它证明效果。
- **不追求 RE Engine 级全自动化**：先做实时面板（够用），落盘回归报告是 M4 进阶。
