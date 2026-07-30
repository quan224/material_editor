// SSSS.hlsli
// Separable Subsurface Scattering（屏幕空间）—— 核函数与可分离核构建。
// 算法：Jimenez et al. 2015《Separable Subsurface Scattering》+ Iryoku 开源实现。
// RE Engine 的 Fast SSSSS 走同一思路（mamoniem 拆解）。
#ifndef SSSS_HLSLI
#define SSSS_HLSLI

#define SSSS_KERNEL_SIZE 11

// —— 皮肤扩散 profile：6 高斯，每通道不同 falloff（R 散得最远 → 偏暖）——
// 这是代表性皮肤 profile；精确拟合值用 Iryoku 仓库的拟合工具标定。
static const float3 gGaussianColor[6] = {
    float3(0.484, 0.550, 0.690),
    float3(0.398, 0.490, 0.660),
    float3(0.450, 0.510, 0.610),
    float3(0.340, 0.390, 0.460),
    float3(0.330, 0.350, 0.420),
    float3(0.400, 0.410, 0.450)
};
static const float gGaussianVariance[6] = { 0.020, 0.050, 0.090, 0.150, 0.250, 0.420 };

// 单高斯权重（r：像素距离，profile 内部按距离求值）
float3 SSSSGaussianWeight(int g, float r)
{
    float3 falloff  = gGaussianColor[g];
    float  variance = gGaussianVariance[g];
    float3 rr = r / max(falloff, 1e-3);
    float3 w  = exp(-(rr * rr) / (2.0 * variance))
              / sqrt(2.0 * 3.14159265 * variance);
    return w;
}

// 6 高斯求和 → 该距离的 profile 权重（RGB，色散）
float3 SSSSProfile(float r)
{
    float3 s = 0;
    [unroll] for (int g = 0; g < 6; ++g) s += SSSSGaussianWeight(g, r);
    return s;
}

// 构建可分离核：N 个对称样本的 uv 偏移 + RGB 权重（已归一化）。
//   directionUV = gDirection * gTexelSize（一步的 uv 增量）
//   radiusPx    = 散射半径（像素，由 scatterRadius*intensity*scale 算出，PS 外算好传入）
void SSSSBuildKernel(float2 directionUV, float radiusPx,
                     out float2 uvOff[SSSS_KERNEL_SIZE], out float3 weight[SSSS_KERNEL_SIZE])
{
    float3 wsum = 0;
    [unroll] for (int i = 0; i < SSSS_KERNEL_SIZE; ++i) {
        float t = (float(i) - (SSSS_KERNEL_SIZE - 1) * 0.5) / ((SSSS_KERNEL_SIZE - 1) * 0.5); // [-1,1]
        float pixelOff = t * radiusPx;
        uvOff[i]  = directionUV * pixelOff;
        weight[i] = SSSSProfile(abs(pixelOff));
        wsum += weight[i];
    }
    // 归一化
    [unroll] for (int j = 0; j < SSSS_KERNEL_SIZE; ++j) weight[j] /= max(wsum, 1e-4);
}

#endif // SSSS_HLSLI
