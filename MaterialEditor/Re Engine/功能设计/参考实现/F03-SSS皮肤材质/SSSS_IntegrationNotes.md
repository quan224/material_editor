# SSSS 集成说明

## 1. G-Buffer：SSS mask 放哪

参考 RE Engine（mamoniem 拆解）：它把 **SSS mask 放在 `VelocityXYAoSss` 这个 G-Buffer 目标的 A 通道**（RG 存速度、B 存 AO、A 存 SSS mask）。

本项目建议（任选其一）：
- **复用现有 G-Buffer 的空通道**：如果 deferred 的某张 G-Buffer 目标有空的 alpha，把 mask 写进 alpha。零新增目标。
- **加一张单通道 mask 目标**：`DXGI_FORMAT_R8_UNORM`，皮肤像素写 1，其余 0。简单清晰。

材质像素如何写 mask：`MaterialCompiler_SkinBranch.cpp` 里 `GBufferOut.SSSMask = 1.0;`——只有 ExprSkin 材质写 1，其它材质 0。

## 2. SSSS 双 pass 挂在延迟光照后

渲染管线顺序：
```
G-Buffer pass（写 albedo/normal/.../SSS mask）
↓
延迟光照 pass（得到 lit color RT）
↓
SSSS 水平 pass   gDirection=(1,0)  color → tempRT   （PS: SSSS_SeparablePass.hlsl）
↓
SSSS 垂直 pass   gDirection=(0,1)  tempRT → finalRT
↓
... 其余后处理（tonemap/bloom/...）
```
注意：SSSS 在 **HDR 线性空间、tonemap 之前**做（散射是线性的物理过程）。

## 3. 参数从反射读出绑 cbuffer

渲染器拿到当前皮肤的 ExprSkin，用反射 API 读三个参数（项目已有的 `Expression::GetParameter`）：
```cpp
auto jr = skin->GetParameter("scatterRadius"); // json array [x,y,z]
auto jc = skin->GetParameter("scatterColor");
auto ji = skin->GetParameter("intensity");      // json number
// 写进 SSSS cbuffer：
cbSSS.gScatterRadius = Vec3(jr[0], jr[1], jr[2]);
cbSSS.gScatterColor  = Vec3(jc[0], jc[1], jc[2]);
cbSSS.gIntensity     = ji.get<float>();
```
`gRadiusScale` 和 `gDetailStrength` 是渲染器全局/预览设置（不属材质），按场景定，建议默认 `0.5` / `0.4`。

## 4. 完整 GGX 着色函数（`SkinShading`）

ExprSkin::Compile 引用了 `SkinShading(...)`。把这段放进材质 HLSL 头（如 `MaterialSkin.hlsli`），编译器 `GenerateCode` 时 include 进来：

```hlsl
// GGX 微表面 BRDF + Lambert 漫射，皮肤用（metallic 通常 0）
float3 SkinShading(float3 baseColor, float roughness, float metallic,
                   float3 N, float ao)
{
    float3 Nv = normalize(N);
    float3 V  = normalize(CameraViewDir);      // 渲染器提供
    float3 L  = normalize(LightDir);           // 渲染器提供（主光）
    float3 H  = normalize(L + V);

    float NdotL = max(dot(Nv, L), 1e-4);
    float NdotV = max(dot(Nv, V), 1e-4);
    float NdotH = max(dot(Nv, H), 1e-4);
    float VdotH = max(dot(V, H), 1e-4);

    float a  = roughness * roughness;
    float a2 = a * a;

    // GGX 法线分布
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    float D = a2 / (3.14159265 * denom * denom);

    // Schlick-Smith-Geometric（用 Smith G2 简化）
    float k  = (roughness + 1.0); k = k * k / 8.0;
    float Gl = NdotL / (NdotL * (1.0 - k) + k);
    float Gv = NdotV / (NdotV * (1.0 - k) + k);
    float G  = Gl * Gv;

    // Fresnel Schlick（皮肤高光用 0.028 的 F0）
    float3 F0 = lerp(0.04, baseColor, metallic);
    float3 F  = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);

    float3 specular = (D * G * F) / (4.0 * NdotV * NdotL + 1e-4);

    // 漫射（能量守恒近似）
    float3 kd = (1.0 - F) * (1.0 - metallic);
    float3 diffuse = kd * baseColor / 3.14159265;

    return (diffuse + specular) * LightColor * NdotL * ao;
}
```

## 5. 调参指引

- `ScatterRadius`：R/G/B 三通道，R 最大（红光散射最远）→ 偏暖。典型 `(1.0, 0.45, 0.25)`。
- `ScatterColor`：肤色暖染。`(1.0, 0.75, 0.65)`。
- `Intensity`：整体强度，0 关闭 SSS。
- `gRadiusScale`（渲染器）：像素半径缩放，分辨率越高需越大。先 `0.5`。
- `gDetailStrength`：`0.3~0.7`，太小糊、太大没散射。

## 6. 参考实现的简化点（与 RE Engine / Iryoku 的差距）

| 点 | 本实现 | 生产级（可演进） |
|---|---|---|
| 采样位置 | 均匀对称 | Iryoku 优化非均匀位置（质量略好） |
| profile 常量 | 代表性值 | Iryoku 拟合工具精确标定 |
| 细节保持 | lerp(模糊,原色) | diffuse/specular 分离，仅模糊漫射 |
| 透射（背光透红） | 无（屏幕空间限制） | 加 separate transmission pass（RE Engine 有） |
