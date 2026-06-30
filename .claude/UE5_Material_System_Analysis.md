# UE5 材质系统全景分析

> 基于 UE5 源码的材质模块深度分析，涵盖编辑器、运行时、编译系统、渲染管线等全链路。

---

## 目录

- [一、整体架构概览](#一整体架构概览)
- [二、核心类继承体系](#二核心类继承体系)
- [三、材质属性体系](#三材质属性体系)
- [四、材质表达式系统](#四材质表达式系统200-种)
- [五、材质编译系统](#五材质编译系统)
- [六、着色器模板系统](#六着色器模板系统)
- [七、渲染时材质系统](#七渲染时材质系统)
- [八、材质编辑器架构](#八材质编辑器架构)
- [九、材质函数与材质层](#九材质函数与材质层)
- [十、特殊材质子系统](#十特殊材质子系统)
- [十一、性能优化机制](#十一性能优化机制)
- [十二、关键源码路径索引](#十二关键源码路径索引)

---

## 一、整体架构概览

UE5 材质系统是一个从**编辑器可视化编程**到 **GPU 着色器执行**的完整管线，核心流程：

```
材质表达式图 (Editor)
  → 材质编译器 (FMaterialCompiler / FHLSLMaterialTranslator)
    → HLSL 代码生成 (MaterialTemplate.ush + 生成代码)
      → 平台着色器编译 (D3D / Vulkan / Metal)
        → GPU 执行 (Renderer / RHI)
```

---

## 二、核心类继承体系

### 2.1 继承层次

```
UObject
└── UMaterialInterface (抽象基类，定义材质接口)
    ├── UMaterial (材质资产本体，包含表达式图、属性定义)
    ├── UMaterialInstance (材质实例，支持参数覆盖)
    │   ├── UMaterialInstanceConstant (MIC - 编辑器创建，编译时固定)
    │   └── UMaterialInstanceDynamic (MID - 运行时创建，可动态修改参数)
    └── UMaterialFunction (可复用的材质函数片段)
```

### 2.2 UMaterialInterface

纯虚接口，所有材质类型的基类：

```cpp
class UMaterialInterface : public UObject {
    // 核心纯虚方法
    virtual UMaterial* GetMaterial() PURE_VIRTUAL;
    virtual FMaterialRenderProxy* GetRenderProxy() const PURE_VIRTUAL;
    virtual void GetMaterialInheritanceChain(FMaterialInheritanceChain& OutChain) const PURE_VIRTUAL;

    // 配置
    USubsurfaceProfile* SubsurfaceProfile;
    USpecularProfile* SpecularProfiles;
    UNeuralProfile* NeuralProfile;
};
```

### 2.3 UMaterial

材质资产主体，包含完整的材质定义：

```cpp
class UMaterial : public UMaterialInterface {
    // 材质域
    TEnumAsByte<EMaterialDomain> MaterialDomain;

    // 混合模式
    TEnumAsByte<EBlendMode> BlendMode;

    // 着色模型
    TEnumAsByte<EMaterialShadingModel> ShadingModel;

    // 使用标志 (StaticMesh, SkeletalMesh, ParticleSprites, Nanite, HairStrands 等)
    EMaterialUsageFlags UsageFlags;

    // 核心材质输入
    FExpressionInput BaseColor;
    FExpressionInput Metallic;
    FExpressionInput Specular;
    FExpressionInput Roughness;
    FExpressionInput Normal;
    FExpressionInput EmissiveColor;
    FExpressionInput Opacity;
    FExpressionInput OpacityMask;
    FExpressionInput WorldPositionOffset;
    FExpressionInput AmbientOcclusion;
    FExpressionInput Refraction;
    FExpressionInput PixelDepthOffset;

    // 表达式列表
    TArray<TObjectPtr<UMaterialExpression>> Expressions;

    // 核心方法
    UMaterial* GetMaterial() override;
    FMaterialRenderProxy* GetRenderProxy() const;
    void CacheShaders();
    void GetUsedTextures(TArray<UTexture*>& OutTextures);
};
```

### 2.4 UMaterialInstance

通过参数覆盖实现材质变体：

```cpp
class UMaterialInstance : public UMaterialInterface {
    // 父材质引用（形成继承链）
    TObjectPtr<UMaterialInterface> Parent;

    // 参数覆盖数组
    TArray<FScalarParameterValue> ScalarParameterValues;
    TArray<FDoubleVectorParameterValue> DoubleVectorParameterValues;
    TArray<FVectorParameterValue> VectorParameterValues;
    TArray<FTextureParameterValue> TextureParameterValues;
    TArray<FParameterCollectionParameterValue> ParameterCollectionValues;
    TArray<FRuntimeVirtualTextureParameterValue> RuntimeVirtualTextureParameterValues;

    // 物理材质
    UPhysicalMaterial* PhysMaterial;
    UPhysicalMaterialMask* PhysMaterialMask;

    // 渲染优化
    bool bHasStaticPermutationResource;
    bool bOverrideBlendableLocation;
    bool bOverrideBlendablePriority;
};
```

### 2.5 UMaterialInstanceConstant (MIC)

- 在编辑器中创建的静态材质实例
- 参数在编译时固定，不支持运行时修改
- 提供编辑器专用 API：`SetScalarParameterValueEditorOnly()` 等
- 使用 `ParameterStateId` 跟踪参数变更

### 2.6 UMaterialInstanceDynamic (MID)

运行时动态材质实例：

```cpp
// 创建
UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(ParentMaterial, Outer);

// 运行时参数修改
MID->SetScalarParameterValue(FName("Roughness"), 0.5f);
MID->SetVectorParameterValue(FName("Color"), FLinearColor::Red);
MID->SetTextureParameterValue(FName("Albedo"), Texture);

// 性能优化：索引参数设置（避免名称查找）
int32 Index = MID->InitializeScalarParameterAndGetIndex(FName("Roughness"), 0.5f);
MID->SetScalarParameterByIndex(Index, 0.8f);

// 参数插值
MID->K2_InterpolateMaterialInstanceParams(OtherMID, Alpha);

// 参数复制
MID->CopyInterpParameters(OtherMID);     // 快速复制
MID->CopyMaterialUniformParameters(OtherMID); // 完整复制
```

---

## 三、材质属性体系

### 3.1 材质域 (EMaterialDomain)

| 值 | 说明 |
|----|------|
| `MD_Surface` | 标准表面材质 |
| `MD_DeferredDecal` / `MD_Decal` | 贴花材质 |
| `MD_Volume` | 体积渲染材质 |
| `MD_PostProcess` | 后处理材质 |
| `MD_Sky` | 天空材质 |
| `MD_Particle` | 粒子材质 |
| `MD_UI` | UI 材质 |

### 3.2 混合模式 (EBlendMode)

| 值 | 说明 |
|----|------|
| `BLEND_Opaque` | 不透明（默认） |
| `BLEND_Masked` | 遮罩（二值透明度） |
| `BLEND_Translucent` | 半透明 |
| `BLEND_Additive` | 叠加 |
| `BLEND_Modulate` | 调制 |
| `BLEND_AlphaComposite` | Alpha 预乘合成 |

### 3.3 着色模型 (EMaterialShadingModel)

| 值 | 说明 |
|----|------|
| `MSM_DefaultLit` | 默认光照（PBR） |
| `MSM_Unlit` | 无光照 |
| `MSM_Subsurface` | 次表面散射 |
| `MSM_ClearCoat` | 清漆 |
| `MSM_SubsurfaceProfile` | 次表面散射（精确配置） |
| `MSM_TwoSidedFoliage` | 双面植被 |
| `MSM_Hair` | 头发 |
| `MSM_Cloth` | 布料 |
| `MSM_Eye` | 眼睛 |
| `MSM_SingleLayerWater` | 单层水 |
| `MSM_ThinTranslucent` | 薄半透明 |
| `MSM_Strata` / Substrate | 新一代材质系统 |

### 3.4 核心材质输入引脚

| 引脚 | 类型 | 说明 |
|------|------|------|
| BaseColor | float3 | 基础颜色 |
| Metallic | float | 金属度 [0,1] |
| Specular | float | 高光强度 [0,1] |
| Roughness | float | 粗糙度 [0,1] |
| Normal | float3 | 切线空间法线 |
| EmissiveColor | float3 | 自发光颜色 |
| Opacity | float | 不透明度 |
| OpacityMask | float | 遮罩透明度（二值） |
| WorldPositionOffset | float3 | 世界位置偏移（顶点动画） |
| AmbientOcclusion | float | 环境遮蔽 |
| Refraction | float | 折射率 |
| PixelDepthOffset | float | 像素深度偏移 |
| TessellationMultiplier | float | 曲面细分倍数 |

### 3.5 使用标志 (EMaterialUsageFlags)

| 标志 | 说明 |
|------|------|
| `MATUSAGE_StaticMesh` | 静态网格 |
| `MATUSAGE_SkeletalMesh` | 骨骼网格 |
| `MATUSAGE_ParticleSprites` | 粒子精灵 |
| `MATUSAGE_BeamTrails` | 光束拖尾 |
| `MATUSAGE_ParticleSubUV` | 粒子子 UV |
| `MATUSAGE_SplineMesh` | 样条网格 |
| `MATUSAGE_GSInstanced` | 几何体实例化 |
| `MATUSAGE_Nanite` | Nanite 网格 |
| `MATUSAGE_HairStrands` | 头发 |
| `MATUSAGE_VolumeRendering` | 体积渲染 |

---

## 四、材质表达式系统（200+ 种）

所有表达式继承自 `UMaterialExpression`。

**头文件位置**：`Engine/Source/Runtime/Engine/Public/Materials/MaterialExpression*.h`
**实现位置**：`Engine/Source/Runtime/Engine/Private/Materials/MaterialExpression*.cpp`

### 4.1 数学运算

| 表达式 | 说明 |
|--------|------|
| Add, Subtract, Multiply, Divide | 基础四则运算 |
| Power, Logarithm, SquareRoot | 幂/对数/开方 |
| Sine, Cosine, Tan, ArcSine, ArcCosine, ArcTangent | 三角函数 |
| Abs, Floor, Ceil, Round, Frac, Sign | 取整/取绝对值 |
| Min, Max, Clamp, Saturate | 范围控制 |
| Lerp, SmoothStep, Step | 插值/步进 |
| Fmod, OneMinus, Exp, Exp2 | 取模/补数/指数 |
| Dot, Cross, Reflect | 向量运算 |
| Noise, Voronoi | 噪声函数 |

### 4.2 常量与参数

| 表达式 | 说明 |
|--------|------|
| Constant | 单浮点常量 |
| Constant2Vector, Constant3Vector, Constant4Vector | 向量常量 |
| ScalarParameter | 标量参数（可被实例覆盖） |
| VectorParameter | 向量参数 |
| ColorParameter | 颜色参数 |
| DoubleVectorParameter | 双精度向量参数 |
| TextureObjectParameter | 纹理对象参数 |
| RuntimeVirtualTextureParameter | 虚拟纹理参数 |

### 4.3 纹理采样

| 表达式 | 说明 |
|--------|------|
| TextureSample | 基础纹理采样 |
| TextureSampleParameter2D | 2D 纹理参数采样 |
| TextureSampleParameterCube | 立方体纹理参数采样 |
| TextureSampleParameterVolume | 体积纹理参数采样 |
| TextureSampleParameterArray | 纹理数组参数采样 |
| RuntimeVirtualTextureSample | 运行时虚拟纹理采样 |
| SparseVolumeTextureSample | 稀疏体积纹理采样 |
| TextureObject | 纹理对象（不采样） |

### 4.4 向量操作

| 表达式 | 说明 |
|--------|------|
| AppendVector | 向量拼接 |
| ComponentMask | 分量掩码 |
| BreakOutFloat2/3/4 | 向量分解 |
| Swizzle | 分量重排 |
| Normalize | 归一化 |
| Transform | 坐标空间变换（Tangent/World/Local/View） |

### 4.5 几何与场景数据

| 表达式 | 说明 |
|--------|------|
| WorldPosition | 世界空间位置 |
| ObjectPositionWS | 对象世界位置 |
| ObjectRadius / ObjectBounds | 对象半径/包围盒 |
| CameraPositionWS | 相机世界位置 |
| CameraVectorWS | 相机方向向量 |
| PixelDepth | 像素深度 |
| SceneDepth | 场景深度 |
| SceneColor | 场景颜色 |
| ScreenPosition | 屏幕空间位置 |
| ViewSize | 视口大小 |
| VertexNormalWS | 顶点世界法线 |
| PrecomputedAOMask | 预计算 AO 遮罩 |

### 4.6 控制流

| 表达式 | 说明 |
|--------|------|
| If | 条件判断 |
| StaticSwitch | 静态开关（编译时分支） |
| Switch / SwitchParameter | 动态开关 |
| QualitySwitch | 质量级别开关 |
| Reroute | 重路由（整理连线） |
| FoldVMask | 折叠向量遮罩 |

### 4.7 函数调用

| 表达式 | 说明 |
|--------|------|
| MaterialFunctionCall | 调用材质函数 |
| FunctionInput | 函数输入接口 |
| FunctionOutput | 函数输出接口 |
| MaterialAttributeLayers | 材质属性层 |

### 4.8 高级功能

| 表达式 | 说明 |
|--------|------|
| Custom | 自定义 HLSL 代码 |
| Substrate* | Substrate 材质系统表达式 |
| MaterialFunctionCall | 材质函数调用 |
| MakeMaterialAttributes | 组合材质属性 |
| BreakMaterialAttributes | 分解材质属性 |
| LandscapePhysicalMaterialBlend | 地形物理材质混合 |
| LightmassReplace | Lightmass 替换 |

---

## 五、材质编译系统

### 5.1 编译管线全流程

```
UMaterial 表达式图
  → FMaterialCompiler (抽象编译器接口)
    → FHLSLMaterialTranslator (主要实现)
      → 遍历表达式图，类型检查 + 常数折叠
        → 生成 FShaderCodeChunk 序列
          → 拼接 MaterialTemplate.ush 模板
            → 输出完整 HLSL 源码
              → IShaderFormat 平台编译 (D3D/Vulkan/Metal/OpenGL)
                → 缓存到 DDC (Derived Data Cache)
```

### 5.2 FMaterialCompiler 抽象接口

```cpp
class FMaterialCompiler {
public:
    // 编译器类型
    virtual EMaterialCompilerType GetCompilerType() const;

    // 当前着色器频率（VS/PS/CS）
    virtual EShaderFrequency GetCurrentShaderFrequency() const;

    // 目标平台
    virtual const ITargetPlatform* GetTargetPlatform() const;

    // 材质属性切换
    virtual void SetMaterialProperty(EMaterialProperty InProperty,
                                      EShaderFrequency OverrideShaderFrequency = SF_NumFrequencies);
    virtual void PushMaterialAttribute(const FGuid& InAttributeID);
    virtual FGuid PopMaterialAttribute();

    // 算术编译
    virtual int32 Add(int32 A, int32 B);
    virtual int32 Sub(int32 A, int32 B);
    virtual int32 Mul(int32 A, int32 B);
    virtual int32 Div(int32 A, int32 B);

    // 纹理采样
    virtual int32 TextureSample(int32 Texture, int32 Coordinate,
                                 EMaterialSamplerType SamplerType);

    // 类型转换
    virtual int32 ValidCast(int32 Code, EMaterialValueType DestType);
    virtual int32 ForceCast(int32 Code, EMaterialValueType DestType, uint32 ForceCastFlags = 0);

    // Substrate BSDF
    virtual int32 SubstrateSlabBSDF(int32 DiffuseAlbedo, int32 F0, int32 F90,
                                     int32 Roughness, int32 Anisotropy, ...);
};
```

### 5.3 FHLSLMaterialTranslator — 核心翻译器

`FHLSLMaterialTranslator` 是 `FMaterialCompiler` 的主要实现，负责将材质表达式图翻译为 HLSL 代码。

```cpp
class FHLSLMaterialTranslator : public FMaterialCompiler {
private:
    // 代码块管理（按着色器频率分组）
    TArray<FShaderCodeChunk> SharedPropertyCodeChunks[SF_NumFrequencies];
    uint64 CurrentScopeID;
    uint64 NextTempScopeID;

    // 统一表达式管理
    TArray<FShaderCodeChunk> UniformExpressions;
    TArray<TRefCountPtr<FMaterialUniformExpressionTexture>> UniformTextureExpressions;

    // 材质属性跟踪
    TArray<FGuid> MaterialAttributesStack;
    TArray<FMaterialParameterInfo> ParameterOwnerStack;

public:
    // 主翻译入口
    EHLSLMaterialTranslatorResult Translate(bool bForceDisableDDCQuery);

    // 内部翻译流程
    void TranslateMaterial();
};
```

### 5.4 FShaderCodeChunk — 代码块数据结构

每个编译结果对应一个代码块：

```cpp
struct FShaderCodeChunk {
    uint64 Hash;                              // 哈希值，用于去重
    FString DefinitionFinite;                  // 有限差分导数的定义
    FString DefinitionAnalytic;                // 解析导数的定义
    FString SymbolName;                        // 符号名称（如 "Local0"）
    TRefCountPtr<FMaterialUniformExpression> UniformExpression;  // 统一表达式
    EMaterialValueType Type;                   // 值类型
    EDerivativeStatus DerivativeStatus;        // 导数状态
    bool bInline;                              // 是否内联
    TArray<int32> ReferencedCodeChunks;        // 引用的代码块索引
};
```

### 5.5 类型系统

```cpp
enum class EMaterialValueType : uint32 {
    MCT_Unknown   = 0,
    MCT_Float1    = 1 << 0,
    MCT_Float2    = 1 << 1,
    MCT_Float3    = 1 << 2,
    MCT_Float4    = 1 << 3,
    MCT_Double1   = 1 << 4,
    MCT_Double2   = 1 << 5,
    MCT_Double3   = 1 << 6,
    MCT_Double4   = 1 << 7,
    MCT_Int1      = 1 << 8,
    MCT_Int2      = 1 << 9,
    MCT_Int3      = 1 << 10,
    MCT_Int4      = 1 << 11,
    MCT_UInt1     = 1 << 12,
    MCT_UInt2     = 1 << 13,
    MCT_UInt3     = 1 << 14,
    MCT_UInt4     = 1 << 15,
    MCT_Bool      = 1 << 16,
};
```

编译器自动处理类型提升和转换（如 Float1 + Float3 → Float3）。

### 5.6 编译优化技术

#### 常数折叠

如果两个操作数都是常数，直接计算结果，不生成 HLSL 代码：

```cpp
int32 FHLSLMaterialTranslator::Add(int32 A, int32 B) {
    // 如果都是常数，直接算出结果
    if (IsExpressionConstantValue(A) && IsExpressionConstantValue(B)) {
        return ConstResultValue(GetType(A), GetConstValue(A) + GetConstValue(B));
    }
    // 否则生成代码
    return AddCodeChunk(ResultType, TEXT("%s + %s"), GetParameterCode(A), GetParameterCode(B));
}
```

#### 代码块哈希去重

```cpp
int32 FHLSLMaterialTranslator::AddCodeChunkInner(uint64 Hash, ...) {
    // 检查是否已存在相同代码块
    for (int32 i = 0; i < CurrentScopeChunks->Num(); ++i) {
        if ((*CurrentScopeChunks)[i].Hash == Hash) {
            return i;  // 复用已有代码块
        }
    }
    // 创建新代码块
    new(*CurrentScopeChunks) FShaderCodeChunk(Hash, ...);
    return CurrentScopeChunks->Num() - 1;
}
```

#### 导数处理

支持两种导数计算方式：
- **有限差分** — 使用硬件 DDX/DDY 指令
- **解析导数** — 通过解析公式计算，更精确

```cpp
FString GetParameterCodeDeriv(int32 Index, ECompiledPartialDerivativeVariation Variation) {
    const FShaderCodeChunk& Chunk = (*CurrentScopeChunks)[Index];
    return Chunk.AtDefinition(Variation);  // 根据变体返回对应导数定义
}
```

### 5.7 HLSL 代码生成

```cpp
FString FHLSLMaterialTranslator::GetParameterCode(int32 Index, const TCHAR* Default) {
    if (Index == INDEX_NONE) {
        return Default ? FString(Default) : TEXT("0");
    }
    const FShaderCodeChunk& Chunk = (*CurrentScopeChunks)[Index];
    if (Chunk.bInline) {
        return Chunk.DefinitionFinite;  // 内联模式：直接返回表达式
    }
    return Chunk.SymbolName;  // 非内联：返回符号名（如 "Local0"）
}
```

### 5.8 平台着色器编译

通过 `IShaderFormat` 接口支持多平台：

```cpp
class IShaderFormat {
    virtual uint32 GetVersion(FName Format) const = 0;
    virtual void GetSupportedFormats(TArray<FName>& OutFormats) const = 0;
    virtual bool PreprocessShader(...) const = 0;
    virtual void CompilePreprocessedShader(...) const = 0;
};
```

| 实现类 | 平台 |
|--------|------|
| D3DShaderFormat | Windows DirectX (SM5.0 / SM6.0) |
| VulkanShaderFormat | Vulkan |
| MetalShaderFormat | Apple Metal |
| ShaderFormatOpenGL | OpenGL |

### 5.9 Substrate 材质编译

Substrate 是 UE5 的新一代材质系统，使用树状 BSDF 结构：

```cpp
struct FSubstrateOperator {
    int32 OperatorType;
    FGuid SubstrateExpressionGuid;
    FGuid ChildMaterialExpressionGuid;
    TArray<int32> Inputs;
};
```

编译器提供 Substrate 专用接口：

```cpp
virtual int32 SubstrateSlabBSDF(
    int32 DiffuseAlbedo, int32 F0, int32 F90,
    int32 Roughness, int32 Anisotropy, ...,
    FSubstrateOperator* PromoteToOperator) override;
```

---

## 六、着色器模板系统

### 6.1 MaterialTemplate.ush

位于 `Engine/Shaders/` 目录，是材质着色器的**骨架模板**：

```
MaterialTemplate.ush 定义了：
  ├── 材质参数结构体 (FMaterialPixelParameters, FMaterialVertexParameters 等)
  ├── 采样函数框架
  ├── 光照计算管线 (GBuffer 填充、光照衰减等)
  ├── 各渲染通道的入口点
  └── [注入点] 编译器生成的用户代码
```

编译器生成的 HLSL 代码被注入到模板的特定位置，与模板框架共同形成最终着色器。

### 6.2 着色器基类

```cpp
// 所有网格材质着色器的基类
class FMeshMaterialShader : public FShader {
    // 管理顶点/像素着色器
    // 处理材质参数绑定
    // 绑定 Vertex Factory (网格数据布局)
};

// 材质到着色器的映射，按特性级别缓存
class FMaterialShaderMap {
    // 缓存不同渲染路径的着色器
    // 按 EShaderPlatform (SM5/ES3.1/Vulkan 等) 分组
    // 支持异步编译
};
```

---

## 七、渲染时材质系统

### 7.1 线程模型

```
游戏线程                    渲染线程
UMaterialInterface ──────→ FMaterialResource
                              └── FMaterialRenderProxy (轻量级代理)
                                    ├── FUniformExpressionCache (参数缓存)
                                    ├── 纹理引用
                                    └── 着色器绑定
```

### 7.2 FMaterialRenderProxy

渲染线程使用的轻量级材质接口：

```cpp
class FMaterialRenderProxy {
    // 获取材质参数的运行时快照
    // 管理材质参数缓存
    // 处理虚拟纹理资源
    // 统一缓冲区管理
};
```

### 7.3 FUniformExpressionCache

```cpp
// 统一表达式缓存
struct FUniformExpressionCache {
    // 按 FeatureLevel 分组的缓存容器
};

struct FUniformExpressionCacheContainer {
    // 参数集合 ID 追踪
    // 异步缓存更新支持
};

// 异步更新作用域
class FUniformExpressionCacheAsyncUpdateScope {
    // 允许在渲染线程异步更新参数缓存
};
```

每帧通过 `CacheUniformExpressions()` 将参数从 CPU 更新到 GPU。

### 7.4 渲染管线中的材质流程

```
BasePass (GBuffer 填充)
  → FBasePassMeshProcessor 收集 mesh draw command
    → FMeshMaterialShader 绑定材质着色器
      → MaterialTemplate.ush 中计算材质属性
        → 写入 GBuffer (BaseColor, Normal, Metallic, Roughness, ...)

Lighting Pass
  → 读取 GBuffer，结合光照信息进行着色
    → 间接光照 (Lumen GI / Ray Tracing)
    → 直接光照 (方向光、点光、聚光灯等)
    → 反射 (Lumen Reflection / SSR)
    → 阴影 (Virtual Shadow Map / CASCADE)
```

### 7.5 FMaterialResource

位于 `Private/Materials/MaterialShader.cpp`：

- 管理材质着色器映射
- 处理材质排列（不同 FeatureLevel / QualityLevel 的变体）
- 管理材质资源生命周期

---

## 八、材质编辑器架构

模块位置：`Engine/Source/Editor/MaterialEditor/`

### 8.1 核心文件

| 文件 | 职责 |
|------|------|
| MaterialEditor.h/cpp | 编辑器主类，管理预览、视图、编译触发 |
| IMaterialEditor.h | 编辑器接口定义 |
| MaterialEditorModule.h/cpp | 模块入口，初始化和注册编辑器功能 |
| MaterialEditingLibrary.h/cpp | 蓝图可调用的材质编辑函数库 |
| MaterialEditorDetailCustomization.h/cpp | 详情面板的自定义布局 |
| MaterialEditorUtilities.cpp | 工具函数集合 |

### 8.2 视图和预览

| 文件 | 职责 |
|------|------|
| SMaterialEditorViewport.h/cpp | 材质预览视口，支持多种预览模型 |
| SMaterialEditorStatsWidget.h/cpp | 统计信息（着色器指令数、纹理采样数等） |
| SMaterialParametersOverviewWidget.h/cpp | 材质参数总览面板 |
| SMaterialLayersFunctionsTree.h/cpp | 材质层/函数树视图 |
| SMaterialPalette.h/cpp | 节点调色板 |
| SMaterialEditorSubstrateWidget.h/cpp | Substrate 材质编辑器面板 |

### 8.3 节点 UI 系统

位于 `MaterialNodes/` 和 `MaterialPins/` 子目录：

| 文件 | 职责 |
|------|------|
| SGraphNodeMaterialBase.h/cpp | 所有材质节点的 UI 基类 |
| SGraphNodeMaterialResult.h/cpp | 主结果节点（连接各材质属性引脚） |
| SGraphNodeMaterialOperator.h/cpp | 运算符节点 UI |
| SGraphNodeMaterialComposite.h/cpp | 复合材质节点 |
| SGraphNodeMaterialConvert.h/cpp | 类型转换节点 |
| SGraphSubstrateMaterial.cpp | Substrate 材质节点 |
| SGraphPinMaterialInput.cpp | 材质引脚 UI |

基于 Unreal 的 **EdGraph** 图表编辑系统实现。

### 8.4 编辑器工作流

1. **创建材质** — 创建 UMaterial 资产
2. **编辑节点图表** — 通过 EdGraph 拖拽表达式节点、连接引脚
3. **参数调整** — 详情面板中实时调整参数
4. **实时预览** — SMaterialEditorViewport 显示预览效果
5. **自动编译** — 修改后自动触发 FHLSLMaterialTranslator 编译
6. **统计查看** — SMaterialEditorStatsWidget 显示性能指标

### 8.5 编辑器专用数据

```cpp
// 仅编辑器使用的数据
class UMaterialEditorOnlyData {
    // 图节点位置信息
    // 表达式集合管理
    // 参数分组
    // 预览网格设置
    // 缩略图渲染信息
    // 导入设置
};
```

---

## 九、材质函数与材质层

### 9.1 UMaterialFunction

可复用的材质表达式集合：

```cpp
class UMaterialFunction : public UMaterialInterface {
    // 表达式集合
    TArray<TObjectPtr<UMaterialExpression>> FunctionExpressions;

    // 预览材质
    UMaterial* PreviewMaterial;

    // 库分类
    FString LibraryCategoriesText;

    // 函数输入/输出接口
    // 通过 FunctionInput / FunctionOutput 表达式定义
};
```

**使用方式**：
- 在材质图中通过 `MaterialFunctionCall` 节点调用
- 支持输入/输出参数接口
- 可嵌套调用其他材质函数
- 适合封装常用计算逻辑（如腐蚀效果、水波纹等）

### 9.2 Material Layers 系统

- 通过 `MaterialAttributeLayers` 表达式支持材质层堆叠
- 每层可独立控制混合权重
- `MakeMaterialAttributes` — 将各属性组合为一个材质属性块
- `BreakMaterialAttributes` — 将材质属性块分解为各独立属性
- 支持层混合模式（覆盖、叠加、插值等）

---

## 十、特殊材质子系统

### 10.1 MaterialParameterCollection

全局参数集合，多个材质可共享：

```cpp
class UMaterialParameterCollection {
    TArray<FCollectionScalarParameter> ScalarParameters;
    TArray<FCollectionVectorParameter> VectorParameters;
    // 使用 GPU Constant Buffer 存储
    // 每个参数有唯一 ID 标识
    // 支持运行时高效更新
};
```

**典型用途**：全局时间、风向、天气参数等需要多材质同步访问的值。

### 10.2 PhysicalMaterial

物理交互属性，位于 `Engine/Source/Runtime/PhysicsCore/`：

```cpp
class UPhysicalMaterial : public UObject {
    // 基础物理属性
    float Friction;          // 摩擦力
    float Restitution;       // 弹性

    // 强度属性（用于破坏系统）
    float TensileStrength;       // 抗拉强度
    float CompressionStrength;   // 抗压强度
    float ShearStrength;         // 抗剪强度

    // 损伤修正
    FPhysicalMaterialDamageModifier DamageModifier;
    float DamageThresholdMultiplier;

    // 软碰撞模式
    EPhysicalMaterialSoftCollisionMode SoftCollisionMode;
};
```

**用途**：碰撞音效、脚步粒子、破坏效果、脚步印等。

### 10.3 PhysicalMaterialMask

按颜色通道映射不同物理材质：

```cpp
// 遮罩颜色通道映射
EPhysicalMaterialMaskColor {
    Red, Green, Blue, Alpha
};
// UMaterialInstance 中:
PhysicalMaterialMap[EPhysicalMaterialMaskColor::MAX]
```

### 10.4 Substrate 材质系统

UE5 的新一代材质框架，取代传统着色模型：

- 基于物理的 BSDF 树结构
- 支持多材质层的自由组合
- 更精确的能量守恒
- 独立的 Specular/Diffuse 传输模型
- 编辑器中有专用的 Substrate 面板

### 10.5 SubsurfaceProfile

精确的次表面散射配置：

```cpp
class USubsurfaceProfile : public UObject {
    // 皮肤散射参数
    // 光线追踪支持
    // 轮廓散射控制
};
```

### 10.6 SpecularProfile

高光反射配置：

```cpp
class USpecularProfile : public UObject {
    // BRDF 参数定制
    // 各向异性支持
};
```

### 10.7 Landscape Physical Material

地形专用物理材质系统：

- GPU 计算物理材质分布
- `FLandscapePhysicalMaterialRenderTask` 管理渲染任务
- 支持地形组件的物理材质映射
- 通过 `LandscapePhysicalMaterialBlend` 表达式混合

---

## 十一、性能优化机制

### 11.1 材质实例共享

- Material Instance 共享父材质的着色器代码
- 只覆盖参数值，**无需重新编译着色器**
- 大幅减少着色器变体数量

### 11.2 静态参数 / StaticSwitch

- 编译时分支裁剪，消除不可能的代码路径
- 每个 StaticSwitch 组合产生一个着色器变体
- 适合不可变的功能开关（如"使用法线贴图"）

### 11.3 DDC 缓存 (Derived Data Cache)

- 着色器编译结果缓存在 DDC
- 避免重复编译相同材质
- 支持本地缓存和共享缓存服务器

### 11.4 异步编译

- 着色器编译在后台线程（ShaderCompileWorker 进程）
- 支持分布式编译（多台机器协同）
- 不阻塞游戏/编辑器主线程

### 11.5 延迟编译

- 非可见材质延迟到需要时才编译
- 减少启动时的编译量

### 11.6 UniformExpressionCache

- 渲染线程参数缓存
- 只更新变化的参数
- 支持异步更新

### 11.7 代码块哈希去重

- 相同表达式自动共享代码块
- 减少生成的 HLSL 代码量

### 11.8 虚拟纹理

- RuntimeVirtualTexture 按需加载纹理数据
- 大幅减少纹理内存占用
- 适合地形等大面积材质

### 11.9 常数折叠与死代码消除

- 编译时计算常量表达式
- 移除未引用的表达式
- 减少最终着色器指令数

---

## 十二、关键源码路径索引

### 运行时核心

| 路径 | 内容 |
|------|------|
| `Engine/Source/Runtime/Engine/Classes/Materials/` | 材质 UCLASS 头文件（Material.h, MaterialInstance.h 等） |
| `Engine/Source/Runtime/Engine/Public/Materials/` | 公共材质头文件（表达式声明、MaterialRenderProxy.h 等） |
| `Engine/Source/Runtime/Engine/Private/Materials/` | 材质核心实现（编译器、翻译器、缓存、表达式实现） |

### 编译系统

| 路径 | 内容 |
|------|------|
| `Engine/Source/Runtime/Engine/Private/Materials/MaterialCompiler.cpp` | 材质编译器基类 |
| `Engine/Source/Runtime/Engine/Private/Materials/HLSLMaterialTranslator.cpp` | HLSL 翻译器主实现 |
| `Engine/Source/Runtime/Engine/Private/Materials/HLSLTree.h/cpp` | HLSL AST 树结构 |
| `Engine/Source/Runtime/Engine/Private/Materials/ShaderGenerationUtil.cpp` | 着色器生成工具 |

### 着色器

| 路径 | 内容 |
|------|------|
| `Engine/Shaders/Private/MaterialTemplate.ush` | 材质着色器骨架模板 |
| `Engine/Shaders/Private/BasePass*` | BasePass 着色器 |
| `Engine/Shaders/Private/DeferredShading*` | 延迟着色 |

### 渲染管线

| 路径 | 内容 |
|------|------|
| `Engine/Source/Runtime/Renderer/Private/Material*` | 渲染器中的材质处理 |
| `Engine/Source/Runtime/Renderer/Private/MeshMaterialShader.h/cpp` | 网格材质着色器基类 |
| `Engine/Source/Runtime/Renderer/Private/BasePassRendering.h/cpp` | BasePass 渲染 |

### 编辑器

| 路径 | 内容 |
|------|------|
| `Engine/Source/Editor/MaterialEditor/` | 材质编辑器完整模块 |
| `Engine/Source/Editor/MaterialEditor/Private/MaterialNodes/` | 节点 UI 实现 |
| `Engine/Source/Editor/MaterialEditor/Private/MaterialPins/` | 引脚 UI 实现 |

### 物理材质

| 路径 | 内容 |
|------|------|
| `Engine/Source/Runtime/PhysicsCore/Public/PhysicalMaterials/` | 物理材质定义 |

### 平台着色器编译

| 路径 | 内容 |
|------|------|
| `Engine/Source/Developer/ShaderFormatD3D/` | DirectX 着色器格式 |
| `Engine/Source/Developer/VulkanShaderFormat/` | Vulkan 着色器格式 |
| `Engine/Source/Developer/ShaderFormatOpenGL/` | OpenGL 着色器格式 |

---

> 文档生成时间：2026-05-20
> 基于 UE5 源码 `E:\UE5` 分析整理
