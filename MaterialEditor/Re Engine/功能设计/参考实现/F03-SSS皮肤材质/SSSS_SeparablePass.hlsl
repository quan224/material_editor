// SSSS_SeparablePass.hlsl
// 可分离 SSS 的一个 pass（水平/垂直通用，靠 gDirection 切换）。
// 跑两次：先 gDirection=(1,0) 写到临时 RT，再 gDirection=(0,1) 写最终 RT。
#include "SSSS.hlsli"

cbuffer cbSSS : register(b0)
{
    float2 gDirection;     // (1,0)=水平 / (0,1)=垂直
    float  gRadiusScale;   // 半径缩放（artist 调，约 0.1~1.0）
    float3 gScatterRadius; // 来自材质反射（像素量级概念）
    float3 gScatterColor;  // 散射颜色（肤色，可乘进权重做色染）
    float  gIntensity;     // 强度
    float  gDetailStrength;// 细节保持（0=全模糊, 1=几乎不模糊，建议 0.3~0.7）
    float2 gTexelSize;     // (1/width, 1/height)
};

Texture2D     texColor : register(t0); // 输入：延迟光照后的场景色
Texture2D<float> texMask : register(t1); // SSS mask（皮肤=1，否则 0）
SamplerState  samLinear : register(s0);

struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

// 全屏三角形顶点（标准 fullscreen tri，渲染器用或自带 VS）
VSOut VSMain(uint vid : SV_VertexID)
{
    VSOut o;
    float2 p = float2((vid << 1) & 2, vid & 2);
    o.pos = float4(p * 2 - 1, 0, 1);
    o.uv  = p;
    // 翻转 V 以匹配 D3D 纹理坐标约定（按你的渲染器约定调整）
    o.uv.y = 1 - o.uv.y;
    return o;
}

float4 PSMain(VSOut i) : SV_Target
{
    float2 uv = i.uv;
    float mask = texMask.Sample(samLinear, uv);
    float3 center = texColor.Sample(samLinear, uv).rgb;

    // 非皮肤直通（mask 门控——PWYNO：只在需要的地方散射）
    if (mask <= 0.0) return float4(center, 1.0);

    // 像素半径 = scatterRadius 均值 × 强度 × 缩放（artist 可调）
    float radiusPx = ((gScatterRadius.r + gScatterRadius.g + gScatterRadius.b) / 3.0)
                   * gIntensity * gRadiusScale;

    // 方向步进向量
    float2 dirStep = gDirection * gTexelSize;

    // 构建核
    float2 uvOff[SSSS_KERNEL_SIZE];
    float3 w[SSSS_KERNEL_SIZE];
    SSSSBuildKernel(dirStep, radiusPx, uvOff, w);

    // 加权求和
    float3 sum = 0;
    [unroll] for (int k = 0; k < SSSS_KERNEL_SIZE; ++k) {
        float2 suv = uv + uvOff[k];
        float  sm  = texMask.Sample(samLinear, suv);   // 只在皮肤范围内散射
        float3 sc  = texColor.Sample(samLinear, suv).rgb;
        sum += w[k] * sc * sm;
    }

    // 细节保持：在模糊结果上按 gDetailStrength 混回中心原色，保留毛孔/纹理高频
    float3 result = lerp(sum, center, saturate(gDetailStrength) * mask);

    // 可选：散射颜色色染（让散射带肤色暖调）
    result *= lerp(1.0, gScatterColor, 0.5 * mask);

    return float4(result, 1.0);
}
