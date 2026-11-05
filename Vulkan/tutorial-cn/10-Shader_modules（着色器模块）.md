# 10 着色器模块（Shader modules）

> 原文：https://vulkan-tutorial.com/Drawing_a_triangle/Graphics_pipeline_basics/Shader_modules

## 着色器与 SPIR-V

与早期 API 不同，Vulkan 里的着色器代码必须用**字节码格式**指定，而不是 GLSL、HLSL 这类人类可读的语法。这个字节码格式叫 **SPIR-V**，为 Vulkan 和 OpenCL（两个 Khronos API）设计，可用于编写图形和计算着色器，本教程聚焦 Vulkan 图形管线里的着色器。

用字节码的好处：GPU 厂商把着色器代码编译成本机代码的编译器**显著更简单**。历史证明，对 GLSL 这类人类可读语法，某些厂商对标准的解读相当随意——如果你在某个厂商的卡上写了不简单的着色器，很可能换个厂商的驱动就因语法错误拒收，更糟的是因为编译器 bug 而运行结果不同。用 SPIR-V 这种直白的字节码有望避免这些。

但这不意味着我们要手写字节码。Khronos 发布了厂商无关的编译器把 GLSL 编译成 SPIR-V。这个编译器会验证你的着色器代码完全符合标准，产出一个可以随程序分发的 SPIR-V 二进制。你也可以把它作为库嵌入、在运行时产出 SPIR-V，但本教程不这么做。虽然可以直接用 `glslangValidator.exe`，但我们将改用 Google 的 **`glslc.exe`**——它的优势是参数格式和 GCC/Clang 这类知名编译器一致，还支持 _include_ 等额外功能。两个都已在 Vulkan SDK 里，无需额外下载。

GLSL 是 C 风格语法的着色器语言。程序有一个对每个对象调用的 `main` 函数。输入输出**不走参数和返回值**，而是用**全局变量**。语言内置许多图形编程辅助：向量和矩阵原语、叉积/矩阵-向量乘积/向量反射等函数。向量类型叫 `vec` 加元素数量——3D 位置存 `vec3`。可以用 `.x` 这样的成员访问单个分量，也可以一次从多个分量构造新向量：`vec3(1.0, 2.0, 3.0).xy` 得到 `vec2`。向量构造器还可以混合向量和标量：`vec3(vec2(1.0, 2.0), 3.0)`。

如上一章所说，要把三角形放上屏幕需要顶点着色器和片元着色器各一个。接下来两节分别讲它们的 GLSL 代码，然后讲怎么产出两个 SPIR-V 二进制并加载进程序。

## 顶点着色器

顶点着色器处理每个进来的顶点。输入是它的属性（模型空间位置、颜色、法线、纹理坐标），输出是**裁剪坐标（clip coordinates）下的最终位置**，以及需要传给片元着色器的属性（颜色、纹理坐标等）。这些值随后会被光栅器在片元之间**插值**，产生平滑渐变。

裁剪坐标是从顶点着色器出来的四维向量，随后通过**整向量除以最后一个分量**变成**归一化设备坐标（NDC）**。NDC 是把帧缓冲映射到 [-1, 1] × [-1, 1] 的齐次坐标系【原文此处有一张 NDC 坐标系图（normalized_device_coordinates.svg）】。

学过图形学的应该熟悉。用过 OpenGL 的会注意到 **Y 坐标方向翻了**；Z 坐标的范围则和 Direct3D 一致——0 到 1。

---

## 【译注·详解】坐标流水线：一个顶点从进来到变成像素

> 上面两段信息密度很高，把涉及的每个概念展开讲透。

### 1. 顶点着色器在流水线里的位置

顶点着色器是**流水线的第一站**，逐顶点调用——提交 3 个顶点就执行 3 次。它是你**完全可编程**的第一段代码，任务只有两类：

```
输入（每个顶点一份，来自顶点缓冲）        输出
─────────────────────────              ─────────────────────────
位置   vec3  模型空间坐标               gl_Position  vec4  裁剪坐标（必输出）
法线   vec3  朝向                      （其他任意属性：颜色、UV……
UV     vec2  纹理坐标                   统称 varyings，光栅化时插值
颜色   vec4                            传给片元着色器）
（要什么属性，顶点布局里声明什么）
```

它干活的方式就是一串矩阵乘法把位置从"模型自己的坐标系"一路搬运到"屏幕上的位置"——这也是下面整条坐标流水线。

### 2. 坐标流水线全景（一个顶点的一生）

```
模型空间 (x,y,z,1)            顶点自己坐标系里的位置，建模时的原始值
   │  × 模型矩阵 M            （把这个模型摆进世界：旋转/平移/缩放）
   ▼
世界空间                      场景里所有物体共用的坐标系
   │  × 观察矩阵 V            （把世界搬到"摄像机眼里"：摄像机放到原点朝 -Z 看）
   ▼
观察空间                      摄像机视角下的位置
   │  × 投影矩阵 P            （把视锥捏成一个正方体，见下节）
   ▼
裁剪坐标 (x,y,z,w)  ◀────── gl_Position 输出的就是它！
   │  ÷ w（透视除法，硬件自动做，不由你控制）
   ▼
NDC 归一化设备坐标             x,y ∈ [-1,1]，z ∈ [0,1]（Vulkan）
   │  视口变换（按 vkViewport 设置拉伸平移，硬件自动）
   ▼
窗口像素坐标 (px, py)          真正对应帧缓冲里的某个像素
```

教程第一章三角形"不做任何变换"的含义：**跳过 M/V/P 三个矩阵，直接手写 NDC 当答案**。

### 2½. 【数字实演】同一个顶点，六个数走完全程

**场景设定**（数字故意选得能手算）：

- 屏幕 800×600，视口铺满全屏
- 模型整体放在世界 (0, 0, -7) 处（① 步之后，追踪顶点的世界坐标是 (0, 1, -7)）
- 相机位于 (5, 0, -5)，朝 **-X 方向**看——椅子落在它**右前方**（前方 5 米、画面偏右 2 米），这样 V 含真实的平移+旋转，且世界读数和观察读数天然不同
- 投影用**真实透视投影矩阵**（fov 90°、aspect 1、近平面 2 m、远平面 12 m——系数由此算出，全部数字一位小数可手算）

**① 模型 → 世界**（平移矩阵 M，把模型挪到 z=-7）：

```
M = [1 0 0  0]        (0)   (0)
    [0 1 0  0]   ×    (1) = (1)
    [0 0 1 -7]        (0)   (-7)
    [0 0 0  1]        (1)   (1)          → 世界坐标 (0, 1, -7)
```

**② 世界 → 观察**：V 的任务是"把相机标准化"——把相机从 (5,0,-5) 挪回原点、把它的朝向从 -X 转回 -Z，世界上的点跟着做**相对运动**。先平移（相机回原点）再旋转（朝向回正）：

```
平移 T(-5, 0, +5)：            旋转 R_y(-90°)（把 -X 转到 -Z）：
[1 0 0 -5]                     [0 0 -1 0]
[0 1 0  0]                     [0 1  0 0]
[0 0 1  5]                     [1 0  0 0]
[0 0 0  1]                     [0 0  0 1]

椅子顶点的世界坐标是 (0, 1, -7)，w=1 省略未写（M/V 阶段 w 恒为 1，投影时才变成距离）。让它依次经过两个矩阵：

第一步：乘平移矩阵 T
    (0, 1, -7) 变成 (-5, 1, -2)
    含义：等效于把相机搬回原点（椅子跟着世界整体平移）

第二步：再乘旋转矩阵 R
    (-5, 1, -2) 变成 (2, 1, -5)
    含义：等效于把相机朝向转正（朝 -Z）——椅子在相机前方 5 米、画面偏右 2 米 ✓
```

三步空间关系（俯视，● 是椅子、📷 是相机）：

```
第0步 世界的原始局面            第1步 ×T 后（相机等效回原点）    第2步 ×R 后（朝向等效转正）

      -Z                             椅子(-5,1,-2)                  -Z 视线
       ↑   ●椅子(0,1,-7)               ●                             ↑    ●椅子(2,1,-5)
       │   │                           │                             │    │前方5米、右2米
 ←─────┼───┼─────────→ +X     ←────────●────────→            ←──────●────●→
       │   │                       相机在原点、仍朝-X             相机在原点、朝-Z（标准姿态）
       │                       📷◀ 相机(5,0,-5)朝-X
```

读法关键：矩阵乘在**点**身上，含义是"**相机**回原点、回正"——世界（点的读数）反向移动 = 相机正向移动，两者等效。

**点题**：世界读数 (0,1,-7)，观察读数 (2,1,-5)——**数字不同，因为参照系换了**：前者相对世界原点，后者相对相机（前方 5 米、画面右 2 米）。观察变换的全部含义：无论相机怎么摆，V 把世界整个"反过来动"，让相机永远等效于站在原点朝 -Z——这样投影矩阵永远只需对付同一种标准情形。

**一个矩阵，两种讲法**：V 乘在椅子坐标上得到的 (2,1,-5)，同时是——

- 讲法一（动作）：相机被搬回原点、转正后，椅子"落在" (2,1,-5)（世界等效地动了）
- 讲法二（换算）：椅子原地没动，(2,1,-5) 是它"在相机坐标系里的地址"（只是换了参照系描述）

整套 MVP 流水线里每个矩阵都有这两重身份——动作视角适合**构造**矩阵（lookAt 怎么写出来），换算视角适合**理解**矩阵在算什么（一串地址翻译）：

| 变换 | 动作讲法 | 换算讲法 |
|------|---------|---------|
| 模型矩阵 M | 模型（带坐标系）被摆进世界 | 模型系读数 → 世界系读数 |
| 观察矩阵 V | 相机被收回标准位（世界反向动） | 世界系读数 → 相机系读数 |

**③ 观察 → 裁剪**（真实透视投影 P：fov 90° 时 x/y 系数恰为 1；z 行由 near=2、far=12 算出；w 行取深度）：

```
P = [1  0   0    0]       (2)     (2)
    [0  1   0    0]   ×   (1)  =  (1)
    [0  0  -1.2  -2.4]    (-5)    (3.6)
    [0  0  -1    0]       (1)     (5)    → 裁剪坐标 (2, 1, 3.6, 5)  ◀── gl_Position

z_clip = -1.2×(-5) + (-2.4) = 6 - 2.4 = 3.6
```

**④ 透视除法**（÷w，即 ÷5）：

```
(2/5, 1/5, 3.6/5) = (0.4, 0.2, 0.72)     → NDC（z=0.72 存进深度缓冲）
```

**⑤ 视口变换**（[-1,1] 拉伸到 800×600）：

```
px = (0.4 + 1)/2 × 800 = 560             → 屏幕中线偏右（x 为正 → 右半屏）
py = (0.2 + 1)/2 × 600 = 360             → 中线偏 20%（垂直方向见下方 Y 翻转）
```

**⑥ 同一个顶点放到 10 米远**（把模型改放到相机正前方 10 米处，即世界 (-5, 0, -5)）——近大远小的数字实锤：

```
顶点世界坐标 (-5, 1, -5)
  ×V：平移 → (-10, 1, 0)；旋转 → (0, 1, -10)     观察空间：正前方 10 米
裁剪 (0, 1, -10, 10)：z_clip = -1.2×(-10) + (-2.4) = 9.6 → ÷10 → NDC (0, 0.1, 0.96)
py = (0.1+1)/2 × 600 = 330
```

同样高的东西（y 都是 1），5 米处占屏幕偏移 0.2、10 米处只占 0.1——**距离翻倍、投影高度减半**，透视感全部来自 ÷w 这一步。

**Y 翻转在数字上长什么样**。先补一个前提：两个 API 的**像素原点位置不同**——OpenGL 的像素 y 从屏幕**底边**往**上**数，Vulkan 的从**顶边**往**下**数。然后拿两个例子各算一遍（屏幕高 600 像素，视口公式 `py = (y + 1) / 2 × 600`）：

```
例 1：椅子顶点，NDC y = +0.2
   py = (0.2 + 1) / 2 × 600 = 360
   OpenGL：从底边往上数 360 → 过中线（300）→ 屏幕上半
   Vulkan ：从顶边往下数 360 → 过中线（300）→ 屏幕下半

例 2：教程三角形顶点 vec2(0.0, -0.5)，NDC y = −0.5
   py = (−0.5 + 1) / 2 × 600 = 150
   OpenGL：从底边往上数 150 → 没到中线 → 屏幕下半
   Vulkan ：从顶边往下数 150 → 没到中线 → 屏幕上半
```

同一个 y、算出**完全相同的 py**，但因为数像素的起点相反，落在屏幕**相反的一半**——这就是 Y 翻转的全部内容。

附注：z 行系数 `A = far/(near−far)`、`B = far·near/(near−far)`，代入 near=2、far=12 得 −1.2 和 −2.4。验算端点：近平面 z=−2 → z_clip = 2.4−2.4 = **0** ✓；远平面 z=−12 → z_clip = 14.4−2.4 = **12 = w** ✓——近平面恰好落在下界 0、远平面恰好贴上界 w。**深度值近密远疏的铁证**：z=−3 → 0.4、z=−5 → 0.72、z=−10 → 0.96——前 5 米占掉了 0..0.72，后 7 米只挤在 0.72..1（1/z 型分布），这就是深度缓冲精度前重后轻、远处易 z-fighting 的根源。

### 3. 为什么位置是四维？——齐次坐标

三个数 (x,y,z) 明明够表示位置，为什么全程 vec4？因为第 4 个分量 **w** 让矩阵乘法无所不能：

- **平移**需要加法，3×3 矩阵乘法只能旋转/缩放、做不了平移；升到 4 维后平移也变成一次乘法——**所有变换统一成"乘一个 4×4 矩阵"**
- w 本身有含义：**w=1 表示点，w=0 表示方向**（方向不受平移影响，乘平移矩阵正好不变——数学上自洽）
- 投影矩阵会把"离眼睛多远"存进 w——这正是下一步透视除法的原料

### 4. 裁剪坐标与透视除法

**裁剪空间的核心设计目标：把“和锥的斜边比”变成“和自己的 w 比”。**

```
 观察空间（视锥侧剖）                裁剪空间（逐点判定）

 ┌──────────────────────┐ 远平面     ┌───────────┐ +w
  ╲                    ╱             │           │
   ╲   近平面┄┄┄┄┄┄┄┄┄╱              │  内→留下   │
    ╲    只有锥内     ╱     ×P       │           │
     ╲    可见       ╱   ────▶      │  外→裁掉   │
      ╲            ╱                │           │
       ╲          ╱                 └───────────┘ -w
        ╲        ╱             判可见 = 每个分量跟【这个点自己的 w】比：
         ╲      ═                    -w ≤ x,y,z ≤ w
          ╲   ═                       六个减法比较，不用除法
           ╲═
            ● 眼睛（锥尖）
```

注意：**w 逐点不同**（= 各自深度的负值），所以裁剪阶段并没有一个公共的正方体——每个点用自己的 w 当尺子。这个"逐点尺子"的判定边界画在观察空间里恰好就是视锥（容许宽度随深度线性增长）。之所以这么设计：**六个比较就完成剔除，不用做除法**——不可见的点在昂贵的除法之前就被砍掉了。

真正公共的立方体要等 ÷w 之后：透视除法把每个点归一，所有幸存点共享 [-1,1]³（z 为 [0,1]）。

**透视除法**（÷w）做两件事：

1. 数学上：把齐次坐标归一，(x,y,z,w) → (x/w, y/w, z/w)，正方体从 [-w,w] 变成 [-1,1]
2. 视觉上：**近大远小就是这么来的**——离眼睛越远的顶点 w 越大，除完坐标越小（越往屏幕中心收缩）。透视感不靠魔法，靠这一除

### 5. NDC：三个 API 的差异对照

NDC 是"与分辨率无关的标准屏幕坐标系"。**同样一个 vec2，不同 API 解释不同**：

```
   OpenGL（+Y 朝上）              Vulkan / D3D（+Y 朝下）
   (-1,1)────────(1,1)           (-1,-1)───────(1,-1)
      │    上半屏 +Y │              ↑    上半屏 -Y   │
      │             │              │               │
      │    下半屏 -Y │              │    下半屏 +Y   │
   (-1,-1)───────(1,-1)           (-1,1)────────(1,1)

   Z 范围：OpenGL [-1,1]          Vulkan / D3D [0,1]（0=近平面，1=远平面）
```

教程那个顶点 `vec2(0.0, -0.5)` 的实际验证：**在 OpenGL 里它显示在屏幕上半部分，在 Vulkan 里显示在下半部分**。沿用 OpenGL 习惯坐标画 Vulkan，画面会**上下颠倒**——这是 Vulkan 新手最经典的第一个"我的三角形怎么是倒的"。根源：屏幕像素坐标从上往下数（原点在左上角），Vulkan 的 NDC 干脆与之同向，省掉一次翻转。

### 6. 视口变换：NDC → 像素

```
px = (x_ndc + 1) / 2 × 视口宽  + 视口左上 x
py = (y_ndc + 1) / 2 × 视口高  + 视口左上 y
```

把 [-1,1] 拉伸到实际像素数。这是我们指定 `vkViewport` 的意义（也是后面固定功能一章的设置项之一）。

### 7. varyings 的插值：顶点级数据怎么变成片元级数据

顶点着色器**每个顶点**跑一次，片元着色器**每个像素**跑一次——中间数量差了几个数量级，桥就是光栅器的**插值**：

```
 三个顶点各带一个颜色：红 ●─────────● 绿
                          ╲  ──  ╱
                           ╲    ╱      光栅器：三角形覆盖了哪些像素，
                            ╲  ╱       每个像素（片元）按"离三个顶点
                          蓝 ●        的远近"加权平均三个顶点的值
内部每个片元拿到的 = 各自不同的混合色 → 从红到绿到蓝的平滑渐变
```

- 权重叫**重心坐标**——硬件对 varyings 里**每一个**字段都这么做（颜色、UV、法线……）
- 你在片元着色器里读到的 UV，已经是"本像素该用的那个插值结果"，不是任何顶点的原值
- 细节：硬件实际做的是**透视校正插值**（先除 w 插值再乘回），保证贴图在透视下不扭曲——知道有这回事即可，行为自动正确

---

第一个三角形我们不做任何变换，直接把三个顶点位置写成 NDC，拼出这样的形状【原文此处有三角形坐标图（triangle_coordinates.svg）】。

把 NDC 当作裁剪坐标输出（最后一个分量设 `1`）就可以直接输出 NDC——除法不会改变任何东西。

这些坐标通常存在顶点缓冲里，但在 Vulkan 里创建顶点缓冲并填充数据并不简单，所以我决定推迟到看到三角形弹出来之后再做。先做点非常规的事：**把坐标直接写死在顶点着色器里**：

```glsl
#version 450

vec2 positions[3] = vec2[](
    vec2(0.0, -0.5),
    vec2(0.5, 0.5),
    vec2(-0.5, 0.5)
);

void main() {
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
```

`main` 对每个顶点调用。内置变量 `gl_VertexIndex` 含当前顶点的索引——通常是顶点缓冲里的索引，但在我们这里是对着色器内硬编码数组的索引。每个顶点的位置从常量数组取出，配上占位的 `z`、`w` 分量组成裁剪坐标。内置变量 `gl_Position` 是输出。

## 片元着色器

顶点着色器给出的位置围成的三角形在屏幕上覆盖一片片元。片元着色器在这些片元上调用，为帧缓冲产出颜色和深度。整个三角形输出红色的简单片元着色器：

```glsl
#version 450

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(1.0, 0.0, 0.0, 1.0);
}
```

和顶点着色器的 `main` 对每个顶点调用一样，这里对每个片元调用。GLSL 里颜色是 R、G、B、alpha 四分量、各在 [0, 1] 范围的向量。与 `gl_Position` 不同，**片元颜色没有内置输出变量**——你要为每个帧缓冲自己声明输出变量，`layout(location = 0)` 指定帧缓冲的索引。红色写进链接到第一个（也是唯一一个，索引 0）帧缓冲的 `outColor`。

## 逐顶点颜色

整个三角形都是红色没什么意思，下面这样的不是好看得多吗【原文此处有彩色三角形图（triangle_coordinates_colors.png）】？

为此要改两个着色器。首先给三个顶点各指定一种颜色。顶点着色器像位置一样加一个颜色数组：

```glsl
vec3 colors[3] = vec3[](
    vec3(1.0, 0.0, 0.0),
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, 0.0, 1.0)
);
```

再把这些逐顶点颜色传给片元着色器，让它输出插值后的值。给顶点着色器加一个颜色输出，在 `main` 里写它：

```glsl
layout(location = 0) out vec3 fragColor;

void main() {
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    fragColor = colors[gl_VertexIndex];
}
```

接着在片元着色器里加对应的输入：

```glsl
layout(location = 0) in vec3 fragColor;

void main() {
    outColor = vec4(fragColor, 1.0);
}
```

输入变量的名字不必相同——靠 `location` 指定的**索引**配对链接。`main` 改为输出颜色加 alpha 值。如上图所示，`fragColor` 的值会在三个顶点之间的片元上自动插值，产生平滑渐变。

## 编译着色器

在项目根目录建一个 `shaders` 目录，顶点着色器存 `shader.vert`、片元着色器存 `shader.frag`。GLSL 着色器没有官方扩展名，但这两个是常用的区分约定。

`shader.vert` 的内容：

```glsl
#version 450

layout(location = 0) out vec3 fragColor;

vec2 positions[3] = vec2[](
    vec2(0.0, -0.5),
    vec2(0.5, 0.5),
    vec2(-0.5, 0.5)
);

vec3 colors[3] = vec3[](
    vec3(1.0, 0.0, 0.0),
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, 0.0, 1.0)
);

void main() {
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    fragColor = colors[gl_VertexIndex];
}
```

`shader.frag` 的内容：

```glsl
#version 450

layout(location = 0) in vec3 fragColor;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(fragColor, 1.0);
}
```

现在用 `glslc` 把它们编译成 SPIR-V 字节码。

**Windows**

建一个 `compile.bat`：

```bat
C:/VulkanSDK/x.x.x.x/Bin/glslc.exe shader.vert -o vert.spv
C:/VulkanSDK/x.x.x.x/Bin/glslc.exe shader.frag -o frag.spv
pause
```

把 `glslc.exe` 的路径换成你安装 Vulkan SDK 的实际路径（本机是 `C:/VulkanSDK/1.4.363.0/Bin/glslc.exe`）。双击运行。

**Linux**

建一个 `compile.sh`：

```sh
/home/user/VulkanSDK/x.x.x.x/x86_64/bin/glslc shader.vert -o vert.spv
/home/user/VulkanSDK/x.x.x.x/x86_64/bin/glslc shader.frag -o frag.spv
```

同样替换路径，`chmod +x compile.sh` 后运行。

**平台差异说明结束**

两条命令让编译器读 GLSL 源文件，用 `-o`（输出）标志输出 SPIR-V 字节码文件。

着色器有语法错误时编译器会告诉你行号和问题。试试删掉一个分号再跑。也可以不带参数运行编译器看它支持哪些标志——比如还能把字节码输出成人类可读格式，看清你的着色器到底在干什么、这一阶段做了哪些优化。

命令行编译着色器是最直接的选择，也是本教程用的方式；但也可以在你自己的代码里直接编译——Vulkan SDK 包含 libshaderc 库，可以在程序内把 GLSL 编成 SPIR-V。

## 加载着色器

有了产出 SPIR-V 着色器的办法，接下来把它们加载进程序，好插进图形管线。先写一个简单的辅助函数从文件读二进制数据：

```cpp
#include <fstream>

...

static std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("failed to open file!");
    }
}
```

`readFile` 函数会读指定文件的全部字节，装进 `std::vector` 管理的字节数组返回。打开文件用两个标志：

- `ate`：从文件**末尾**开始读
- `binary`：按二进制读（避免文本转换）

从末尾开始的好处：可以用读位置确定文件大小、据此分配缓冲：

```cpp
size_t fileSize = (size_t) file.tellg();
std::vector<char> buffer(fileSize);
```

然后 seek 回文件开头，一次性读完所有字节：

```cpp
file.seekg(0);
file.read(buffer.data(), fileSize);
```

最后关闭文件并返回字节：

```cpp
file.close();

return buffer;
```

现在在 `createGraphicsPipeline` 里调用它加载两个着色器的字节码：

```cpp
void createGraphicsPipeline() {
    auto vertShaderCode = readFile("shaders/vert.spv");
    auto fragShaderCode = readFile("shaders/frag.spv");
}
```

打印缓冲大小、对照实际文件字节数来确认加载正确。注意代码不需要以空字符结尾——它是二进制代码，稍后我们会显式传它的大小。

## 创建着色器模块

把代码交给管线之前，得先把它包进一个 `VkShaderModule` 对象。建辅助函数 `createShaderModule`：

```cpp
VkShaderModule createShaderModule(const std::vector<char>& code) {

}
```

函数接收字节码缓冲，从中创建 `VkShaderModule`。

创建着色器模块很简单，只需指定字节码缓冲的指针和长度，装进 `VkShaderModuleCreateInfo` 结构。唯一的坑：大小以**字节**计，但字节码指针是 `uint32_t*` 而不是 `char*`，所以要用 `reinterpret_cast` 转换指针。做这种转换时还得确保数据满足 `uint32_t` 的**对齐**要求——幸好数据存在 `std::vector` 里，默认分配器已保证数据满足最坏情况的对齐要求。

```cpp
VkShaderModuleCreateInfo createInfo{};
createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
createInfo.codeSize = code.size();
createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
```

然后调用 `vkCreateShaderModule` 创建：

```cpp
VkShaderModule shaderModule;
if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
    throw std::runtime_error("failed to create shader module!");
}
```

参数和之前的对象创建函数一样：逻辑设备、create info 指针、可选的自定义分配器指针、句柄输出变量。创建模块后代码缓冲可以立即释放。别忘了返回创建的模块：

```cpp
return shaderModule;
```

着色器模块只是之前从文件加载的字节码及其函数定义的**薄包装**。SPIR-V 字节码到 GPU 机器码的编译和链接**直到图形管线创建时才发生**。这意味着管线创建完成后就可以销毁着色器模块——所以它们是 `createGraphicsPipeline` 里的**局部变量**而不是类成员：

```cpp
void createGraphicsPipeline() {
    auto vertShaderCode = readFile("shaders/vert.spv");
    auto fragShaderCode = readFile("shaders/frag.spv");

    VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);
```

清理放在函数末尾，加两个 `vkDestroyShaderModule` 调用。本章余下的代码都插在这两行之前：

```cpp
    ...
    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);
}
```

## 着色器阶段创建

要真正使用着色器，得通过 `VkPipelineShaderStageCreateInfo` 结构把它们分配给管线的特定阶段——这是实际创建管线过程的一部分。

先在 `createGraphicsPipeline` 里填顶点着色器的结构：

```cpp
VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
```

除了必填的 `sType`，第一步是告诉 Vulkan 着色器用在管线的**哪个阶段**——上一章描述的每个可编程阶段都有对应的枚举值。

```cpp
vertShaderStageInfo.module = vertShaderModule;
vertShaderStageInfo.pName = "main";
```

接着两个成员指定含代码的着色器模块，和要调用的函数——即**入口点（entrypoint）**。这意味着可以把多个片元着色器合进一个着色器模块，用不同入口点区分行为。不过我们用标准的 `main`。

还有一个（可选）成员 `pSpecializationInfo`，这里不用但值得一提。它允许你为**着色器常量**指定值——同一个着色器模块，在管线创建时通过指定不同的常量值来配置行为。这比运行时用变量配置更高效，因为编译器可以做诸如消除依赖这些值的 `if` 语句之类的优化。没有这类常量就设 `nullptr`，结构体初始化已自动处理。

改成片元着色器的版本很容易：

```cpp
VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
fragShaderStageInfo.module = fragShaderModule;
fragShaderStageInfo.pName = "main";
```

最后定义一个装这两个结构的数组，稍后在实际创建管线的步骤里引用它：

```cpp
VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};
```

管线可编程阶段的描述就这么多。下一章看固定功能阶段。
