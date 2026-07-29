# F10 — 动态 GI（DPGI）

> 把预览光照从静态 IBL 升级到**动态 probe GI**，间接光随场景/光源变化实时更新。
> 类别：A 渲染进阶。**执行序第 10**：独立于主线，按兴趣做。

## 1. 目标与动机

**变成什么功能**：预览场景的间接光由动态 probe 网格计算，移动物体/光源时，间接光照实时跟随变化（静态 IBL 做不到）。

**为什么不是 toy**：
- "材质在不同光照下表现如何"是材质编辑器的核心预览场景；静态 IBL 只能看一个固定环境。
- 动态 GI 是预览质量的真实提升，且 DPGI 是 RE Engine 荒野的最新方案，有现成蓝图。

## 2. 涉及课次与文件

| 课次 | 位置 | 做什么 |
|---|---|---|
| 课 17 | `src/Renderer/` 预览光照 | IBL baseline → 升级为动态 probe GI |

**前置依赖**：课 17 静态 IBL baseline（无 F 依赖，独立特性）。

## 3. 架构改动（逐层）

**渲染器层**
- **probe 网格放置**：在预览场景布点（网格或自适应）。
- **辐照累积**：每 probe 采集周围辐照（存 cubemap 或球谐 SH）。
- **动态更新**：场景/光源变化时重算受影响 probe（增量更新控成本）。
- **四面体插值**：RE Engine 用 probe 四面体（Delaunay）插值，生成屏幕空间的 GI Diffuse / GI Specular 缓冲。
- **可选 RT 增强**：probe 内用 F04 的 RayQuery 做一次反弹，提精度（贵）。

**UI 层**
- probe 调试视图：可视化 probe 位置/辐照，便于调参。

## 4. RE Engine 资料对应

| 步骤 | RE Engine 资料 |
|---|---|
| 动态环境变化应对实时 GI（DPGI） | CEDEC2025 荒野：https://cedec.cesa.or.jp/2025/timetable/detail/s67aea5fc8fdc6 |
| 当前 probe GI（probe 四面体 + IBL cubemap） | mamoniem：https://mamoniem.com/behind-the-pretty-frames-resident-evil/ |
| 弃 lightmap 用 probe network 的历史 | CEDEC2016 RE7 光照（见 [../02-CEDEC与日文资料.md](../02-CEDEC与日文资料.md) A 节） |

## 5. 里程碑

- **M1 静态 IBL baseline**（课 17 已有）。
- **M2 probe 网格 + 辐照累积**。
- **M3 四面体插值 GI**：屏幕空间生成 GI Diffuse/Specular。
- **M4 动态更新 + probe 调试视图**：光源移动时间接光跟随。

## 6. 验收标准

- 移动光源/物体时，间接光（阴影柔边、色溢）实时变化——这是静态 IBL 做不到的。
- probe 布局可可视化，便于排查。
- 性能可接受（预览场景小，probe 数有限）。

## 7. 诚实说明

- **probe 数量 vs 更新频率是性能权衡**。预览场景小可接受；大场景才需 RE Engine 级优化。
- **RT GI（F04）是更高精度替代**，但更贵；DPGI 是光栅友好的折中。
- DP（动态 probe）详细算法在 CEDEC2025，slides 公开后照搬；当前可先做静态 probe 插值再动态化。
