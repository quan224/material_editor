# F03 — SSS 皮肤材质

> 把 RE Engine 的次表面散射做成节点图里**一种可编辑、可预览的材质类型**（皮肤），打通"特殊材质如何贯穿节点图→编译器→渲染器"的范式。
> 类别：D 角色 TA。**执行序第 3**：确立特殊材质范式，F05/F06 套它。

## 1. 目标与动机

**变成什么功能**：节点图里能选"Skin"材质模板，调散射半径/颜色，预览窗口实时看到一个有次表面散射的皮肤材质（耳廓透红、鼻梁软光）。

**为什么不是 toy**：
- 材质编辑器本就该支持皮肤——这是经典材质类型，不是练习题。
- SSS 成为节点图里**一种材质模型**（带 Subsurface 输出通道），用户真正能用、能调、能保存。
- 它确立"特殊材质类型"范式：F05 头发、F06 眼睛都套这套模板。

## 2. 涉及课次与文件

| 课次 | 位置 | 做什么 |
|---|---|---|
| 课 7 | `src/Expression/` | 新增 Skin 材质模板 + SSS 参数（反射暴露） |
| 课 8 | `src/Compiler/` | 为 Skin 材质生成带 SSS 参数的 HLSL |
| 课 15-16 | `src/Renderer/`（shader + G-Buffer） | G-Buffer 写 SSS mask；延迟光照后跑 SSSS pass |
| 课 13 | UI 属性面板 | 调散射半径/颜色（走现有反射 GetParameters） |

**前置依赖**：课 14-16 deferred 渲染器（SSSS 是后处理散射 pass）。

## 3. 架构改动（逐层）

**Expression / 节点图层（L4/L2）**
- 新增 Skin 材质模板：`GetInputPins/GetOutputPins` 声明 BaseColor / Roughness / **Subsurface（半径+颜色）** 等通道。
- SSS 参数（散射半径 `Vec3`、散射颜色 `Vec3`、强度 `float`）用 `ME_FIELD` 宏暴露，自动进属性面板（复用课 5 反射 + 课 5c 表单范式）。

**Compiler 层（L5）**
- `MaterialCompiler` 识别 Skin 材质模型，在生成的 HLSL 材质属性块里输出 SSS 参数（如 `Material.ScatterRadius` 等 uniform）。
- 这是"编译器按材质模型分支生成 HLSL"的第一个实例，为 F05/F06 铺路。

**渲染器层（L5）**
- **G-Buffer 扩展**：按 RE Engine 做法，把 SSS mask 写进 G-Buffer 的某个通道（mamoniem 记录 RE Engine 用 `VelocityXYAoSss` 目标的 A 通道存 SSS mask）。
- **SSSS pass**：延迟光照后，跑 Separable SSSS（横向 + 纵向两次高斯散射 pass），用 mask 限定只在皮肤像素散射。散射核由散射半径/颜色参数驱动。
- 要求渲染器是 **deferred**（课 14-16 若选 deferred 正好；若 forward 则 SSS 改在光照 shader 内做，稍不同）。

**UI 层（课 13）**
- 属性面板自动显示 SSS 参数（反射已支持 Vec3 → 三个 spinbox，见课 5c `ReflectionDemoWidget`）。无需额外 UI 代码。

## 4. RE Engine 资料对应

| 步骤 | RE Engine 资料 |
|---|---|
| SSS mask 放哪、Fast SSSSS Apply、SubSurfaceParam | mamoniem「Behind the Pretty Frames」SSS 章节：https://mamoniem.com/behind-the-pretty-frames-resident-evil/ |
| SSSS 算法源头（Separable SSS） | Iryoku Separable SSS：https://www.iryoku.com/separable-sss-released/ |
| SSSS 在 deferred 里实跑的样子 | DMC5 抓帧（中文）：https://zhuanlan.zhihu.com/p/358786495 |

## 5. 里程碑

- **M1 G-Buffer 加 SSS mask**：渲染器在 G-Buffer 预留并写入 SSS mask 通道，能可视化 mask。
- **M2 SSSS 双 pass shader**：实现 Separable SSSS（横+纵），输入散射参数，能在 mask 区域看到散射。
- **M3 Skin 材质模板 + 节点**：课 7 加 Skin 模板，编译器生成带 SSS 参数的 HLSL，连进渲染器。
- **M4 参数面板 + 打磨**：属性面板调散射半径/颜色实时变化；调参体验顺滑。

## 6. 验收标准

- 能在编辑器里新建一个 Skin 材质、连图、调散射参数、实时预览。
- 散射参数变化时，皮肤透红/软光程度肉眼可见地变化。
- 材质能保存/加载（课 18），证明是完整材质资产，不是临时 hack。

## 7. 诚实说明

- **依赖 deferred**：SSSS 是后处理散射 pass，需要先有延迟管线（课 14-16）。若项目最终选 forward，SSS 改在光照 shader 内做（细节不同，但概念同）。
- **SSSS 局限**：对 backface 透光（如耳朵）和极薄几何，纯屏幕空间散射有偏差；RE Engine 也靠美术/网格配合。本功能先做前向散射，backface 透光留作进阶。
- **散射核质量**：Separable SSS 用预计算的高斯核近似，比真 SSS 快但略糙——业界公认折中（RE Engine 也用）。
