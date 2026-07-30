# F03 — SSS 皮肤材质（完整参考实现）

> 节点图里一种**皮肤材质**（标准 PBR + 次表面散射），材质侧落进项目已有的 `Expression` / 反射 / `MaterialCompiler`，渲染侧给完整 **Separable SSSS** HLSL（屏幕空间双 pass）+ G-Buffer mask 集成。
> 蓝本：RE Engine Fast SSSSS（mamoniem 拆解）+ Jimenez《Separable SSS》(2015) / Iryoku 开源实现。

## 文件清单

| 文件 | 作用 |
|---|---|
| `ExprSkin.h/.cpp` | 皮肤材质 `Expression` 子类：`ME_FIELD` 暴露 ScatterRadius/ScatterColor/Intensity；标准 PBR 输入引脚；Compile 出 GGX 光照色 chunk |
| `MaterialCompiler_SkinBranch.cpp` | 编译器的 Skin 材质模型分支（补丁示例）：写 G-Buffer 含 SSS mask；声明 SSS 参数 cbuffer 绑定 |
| `SSSS.hlsli` | 6 高斯皮肤扩散 profile + 可分离核构建 |
| `SSSS_SeparablePass.hlsl` | 可分离模糊 PS（水平/垂直各跑一次），mask 门控 + 细节保持 |
| `SSSS_IntegrationNotes.md` | G-Buffer mask 怎么放、延迟光照后如何挂 SSSS 双 pass、参数怎么从反射读出来绑 cbuffer、完整 GGX 着色函数 |

## 工作流（材质 vs 渲染 两侧）

```
[材质侧 — 节点图]
  ExprSkin(BaseColor, Roughness, Metallic, Normal, AO)
    → Compile 出标准 GGX 光照色（皮肤通常 Metallic≈0）
    → 渲染器认出 Skin 材质 → 该材质像素在 G-Buffer 写 SSS mask = 1

[渲染侧 — 延迟光照后]
  延迟光照得到 lit color（皮肤像素已被正常照亮）
  → SSSS 水平 pass（mask=1 才散射）→ 临时 RT
  → SSSS 垂直 pass（mask=1 才散射）→ 最终 RT
  参数（ScatterRadius/Color/Intensity）由渲染器从 ExprSkin 反射 GetParameter 读出，绑进 SSSS cbuffer
```

## 集成步骤（对照你的课次）

1. **课 7（表达式）**：把 `ExprSkin.h/.cpp` 加进 `src/Expression/`，在节点工厂注册（`NodeFactory::Register`）一个 "Skin" 类型。
2. **课 8（HLSL 生成）**：参考 `MaterialCompiler_SkinBranch.cpp` 给 `MaterialCompiler` 加 Skin 材质模型分支（写 G-Buffer 含 mask + SSS 参数 cbuffer）。
3. **课 15-16（渲染器 / G-Buffer）**：G-Buffer 预留一个 SSS mask 通道（见 IntegrationNotes，RE Engine 用 `VelocityXYAoSss` 的 A 通道）。
4. **课 16（后处理）**：延迟光照后挂 SSSS 双 pass（`SSSS_SeparablePass.hlsl` ×2，方向 (1,0)/(0,1)）。
5. **课 13（属性面板）**：参数走反射 `ME_FIELD`，自动出现在属性面板（复用课 5c `ReflectionDemoWidget` 的 Vec3→三 spinbox）——零额外 UI 代码。

## 参考文献

- **Separable Subsurface Scattering（Jimenez et al., 2015 论文）**：https://iryoku.com/separable-sss-released/ （含论文 PDF + GPL 参考实现 + 精确皮肤 profile 拟合工具）
- **RE Engine Fast SSSSS（mamoniem 抓帧）**：https://mamoniem.com/behind-the-pretty-frames-resident-evil/ （SSS mask 放 A 通道、Fast SSSSS Apply、SubSurfaceParam）
- **DMC5 抓帧（中文，SSSSS 实跑）**：https://zhuanlan.zhihu.com/p/358786495
- **SeparableSSS 开源参考实现（Iryoku，GPL）**：https://github.com/iryoku/separable-sss （精确 6 高斯常量与最优采样位置）

## 诚实边界

- 皮肤 6 高斯 profile 给的是**代表性值**；精确拟合用 Iryoku 仓库的工具标定（链接见上）。
- 采样位置用**均匀对称**分布（完整、正确）；Iryoku 用优化的非均匀位置，质量略好——可替换核构建，PS 不变。
- 细节保持用 `gDetailStrength` 简化版（保中心原色比例）；Iryoku 的"diffuse/specular 分离"更干净，留作进阶。
- 不保证首次编译跑通——SSSS 是后处理，要渲染器（课 14-16）先有 deferred 管线；报错我修。
