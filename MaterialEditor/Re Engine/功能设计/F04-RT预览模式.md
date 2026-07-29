# F04 — RT 预览模式（反射 / 阴影）

> 把 RE Engine 的硬件光追做成预览视口的一个**可开关渲染模式**，提升金属/玻璃等材质的预览真实度。
> 类别：A 渲染进阶。**执行序第 4**：确立"可切换渲染路径"范式，F09 路径追踪在其上。

## 1. 目标与动机

**变成什么功能**：预览面板加「RT 反射」「RT 阴影」开关。开启后，金属反射屏外数据、阴影边缘软化这些光栅做不好的效果，由 DXR 光追补上。

**为什么不是 toy**：
- 直接服务材质预览质量（材质编辑器的核心价值）。
- 项目已是 DX12，DXR 是同一 API 家族的自然延伸，不是换技术栈。
- 采用 RE Engine 的 **RayQuery 路线**（不是全混合 RT 管线），粒度可控——在现有光栅 shader 里按需插几条 ray query。

## 2. 涉及课次与文件

| 课次 | 位置 | 做什么 |
|---|---|---|
| 课 14-16 | `src/Renderer/`（新建，DX12 渲染器） | 先有完整光栅 deferred/forward 渲染器 |
| 课 20 | 渲染器扩展 | 加 DXR 路径，作为"可切换渲染后端"的第二条 |

**前置依赖**：F01（加速结构的大块显存）、F02（量化 RT pass 耗时）。

## 3. 架构改动（逐层）

**渲染器层（L5）**
- 设备：`D3D12CreateDevice` 时请求 DXR 支持，拿 `ID3D12Device5`。
- 加速结构：几何体建 BLAS，场景建 TLAS，每帧/场景变更时更新（大块显存走 F01）。
- shader：在反射/阴影计算处插 `RayQuery`（HLSL 的 `RayQuery<flags>` 对象，不需要单独的 raytracing PSO / SBT——这正是 RayQuery 比 full RT 轻量的地方）。

**材质 / shader 层**
- 反射项：`#if RT_REFLECTIONS` 分支调 RayQuery 打反射 ray，命中取命中点着色；`#else` 走 SSR 或环境贴图。
- 阴影项同理：`#if RT_SHADOWS` 用 RayQuery 打向光源，命中=阴影；带随机 = 软阴影。

**UI 层（课 13/20）**
- 预览 dock 加两个 checkbox（RT 反射 / RT 阴影），通过渲染器配置传入 shader 的 `#define`/root constant。

## 4. RE Engine 资料对应

| 步骤 | RE Engine 资料 |
|---|---|
| RayQuery 路线选择（非全 RT） | RE:2023「Advances in Ray Tracing」（龔奕雄）：https://www.capcom-games.com/coc/2023/en/session/24/ |
| RT 集成思路、各作品实测 | NVIDIA 博客：https://developer.nvidia.com/blog/qa-how-capcom-brought-path-tracing-to-re-engine-across-pragmata-and-resident-evil-requiem/ |
| 效果参考（RT 反射/阴影该长什么样） | DF DMC5 SE / Village 评测（见 [../03-技术媒体抓帧拆解.md](../03-技术媒体抓帧拆解.md) A/E 节） |

## 5. 里程碑

- **M1 DXR 地基**：设备拿 `ID3D12Device5`，建一个三角形的 BLAS+TLAS，raygen shader 打中输出颜色（hello-raytracing）。
- **M2 RayQuery 反射**：在现有光栅 shader 反射项插 RayQuery，金属球能反射屏外物体。
- **M3 RayQuery 阴影**：阴影项插 RayQuery，方向光阴影边缘软化。
- **M4 UI 开关 + 性能档**：预览面板 toggle；Profiler（F02）显示 RT pass 耗时；提供"开 RT/关 RT"性能档。

## 6. 验收标准

- 开 RT 反射后，金属材质能反射视口外的几何体（光栅 SSR 做不到屏外）。
- 开 RT 阴影后，阴影边缘自然软化。
- Profiler（F02）能看到 RT pass 的 GPU 耗时，开关前后帧时间对比可读。
- 不开 RT 时退回纯光栅，行为不变（证明是"可切换路径"）。

## 7. 诚实说明

- **硬件门槛**：要 RT 核心（RTX 20+ / RDNA2+）。无 RT 硬件则该模式不可用，光栅路径仍跑。
- **RayQuery ≠ 全路径追踪**：RayQuery 是在光栅 shader 里按需打光线，复杂度远低于 F09 路径追踪。本功能刻意停在 RayQuery，不碰 ReSTIR/降噪那套（那是 F09）。
- **依赖**：加速结构的大块显存走 F01；优化效果量化依赖 F02。可先用简单分配跑通，再接 F01。
