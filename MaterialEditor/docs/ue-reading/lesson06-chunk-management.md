# 课 6 · ⑤ 代码块管理 —— UE 导读

> 只列抄什么、在哪。省略什么自己判断。

## 抄这些（本机路径 + 行号）

`Engine/Source/Runtime/Engine/Private/Materials/HLSLMaterialTranslator.cpp`

| 函数 | 行号 |
|---|---|
| `CreateSymbolName` | 搜 `CreateSymbolName(TEXT("Local"))` |
| `AddCodeChunk`（外壳）| 搜 `int32 FHLSLMaterialTranslator::AddCodeChunk(` |
| `AddCodeChunkInner`（核）| :3337 |
| `AddInlinedCodeChunk` | AddCodeChunk 附近 |
| `AddUniformExpression`（外壳）| 搜 `int32 FHLSLMaterialTranslator::AddUniformExpression(` |
| `AddUniformExpressionInner`（核，双层去重）| :3610 |
| `ConstResultValue` | 搜 `ConstResultValue` |
| `GetParameterCode` | 搜 `GetParameterCode`（三段判断）|
| `IsExpressionConstantValue` | 搜同名（Mul 的 x*0/x*1 化简处）|
| `GetParameterUniformExpression` | 搜同名 |

`Engine/Source/Runtime/Engine/Private/Materials/HLSLMaterialTranslator.h`

| 成员 | 行号 |
|---|---|
| `UniformExpressions`（材质级表达式表，AddUniformExpressionInner 去重用）| :346 附近 |

## 理解案例

### 例1：AddCodeChunk 是「代码入库登记处」

算子算出一段 HLSL 代码后交给它登记，换回**索引号**——之后引用这段代码的地方都拿索引说话。

参数（`AddCodeChunkInner :3337` 原文）：

| 参数 | 作用 |
|---|---|
| `Hash` | 代码串指纹，去重钥匙——同代码二次入库直接返回旧索引 |
| `FormattedCode` | 代码本体，如 `"(Local0 + Local1)"` |
| `FormattedType` | 类型名字符串（`"float3"`）——非内联时拼变量声明用 |
| `Type` | 值类型——后续算子据此判断怎么对待它 |
| `DerivativeStatus` | 导数状态标记（双轨用）|
| `bInlined` | 登记方式：true=只存串用的时候嵌入；false=建局部变量存结果 |

材质图 `TextureSample × 0.5` 的入库过程：

```
① TextureSample → AddCodeChunk(type=Float4, code="T0.Sample(S0, uv)", bInlined=false)
   无同 hash → 登记，code 被包装成变量声明：
      float4 Local0 = T0.Sample(S0, uv);
   返回索引 0

② Constant(0.5) → 走另一条路（AddUniformExpression，表达式块）→ 索引 1

③ Multiply → AddInlinedCodeChunk(
      code = "(" + GetParameterCode(0) + " * " + GetParameterCode(1) + ")")
   GetParameterCode(0) → "Local0"   ← 非内联块：变量名引用
   GetParameterCode(1) → "0.5"     ← 表达式块：定义串直接用
   登记串 = "(Local0 * 0.5)"，内联 → 索引 2
```

| 索引 | 内容 | 方式 |
|---|---|---|
| 0 | `float4 Local0 = T0.Sample(S0, uv);` | 变量（采样有真实开销，值得占名）|
| 1 | `0.5`（挂常量树）| 表达式块 |
| 2 | `(Local0 * 0.5)` | 内联（乘法零成本，嵌入比建变量干净）|

原文四段分支（:3337 判断顺序）：

```
Type == Unknown       → 不收，返回 -1（哨兵传播）
bInlined              → 只存串（不去重——串太短，查重不划算）
数值类型(Float/UInt..) → 建变量版 + hash 查重（同代码只建一次）
纹理等                 → 报错（纹理对象不能声明局部变量）
```

「变量 vs 内联」取舍（UE .cpp:3530 注释）：产生真实指令的用变量（shader 优化器效果好），零成本的直接嵌。

### 例2：AddUniformExpression 双层去重——两棵同值的树只活一棵

两个不同材质属性（BaseColor 和 Emissive）都写了 `Param("Tint") × Constant(2)`：

```
BaseColor 编译 → 建树 FoldedMath(Param(Tint), Const(2)) → 入库
   第一层：扫 chunk 表，IsIdentical 无命中 → 继续
   第二层：扫材质级 UniformExpressions 表，无命中
   → 登记：chunk 挂树 + 树进材质级表，返回索引 5

Emissive 编译 → 又建了一棵语义相同的树（编译器不知道上次的事）
   第一层：IsIdentical 扫 chunk 表——无命中（那个 chunk 挂在别的属性下，当前表没有）
   第二层：扫材质级表——IsIdentical 命中！
   → 新树丢弃（Ref 落作用域自动释放），复用登记树，仍建新 chunk
   → 返回索引 9（新 chunk，挂的是同一棵树对象）
```

关键：两个 chunk 共享**同一个树对象**（`chunk[5].uniform_expression == chunk[9].uniform_expression`）——运行时 preshader 只求值一次。这就是 `UniformExpressions`（.h:346 材质级表）存在的意义：**hash 只能发现字符串相同，IsIdentical 能发现结构相同**——`(1+2)` 和 `3` 代码串不同，但树折叠后语义相等。

### 例3：查询家族——GetParameterCode 的三段判断

同一个函数，对三种 chunk 给出不同答案（消费 ⑤ 合同的出口）：

```
GetParameterCode(0)  // 变量块   → "Local0"     （引用声明，代码不重复）
GetParameterCode(1)  // 表达式块 → "0.5"        （定义串直接用）
GetParameterCode(2)  // 内联块   → "(Local0 * 0.5)"（原文嵌入）
```

### 例4：ConstResultValue + IsExpressionConstantValue——Mul 化简的后场

`Mul(AnyChunk, Constant(0))`（x*0 化简）的判定链：

```
Mul 算子（⑥）先问：IsExpressionConstantValue(B, 0.0f)
   → 取 B 的树 → IsConstant() 递归全真？
   → GetNumbervalue(ctx) 求值 → 全分量 == 0？
   → 真 → ConstResultValue(MCT_Float, Vec4(0,0,0,0))
          → 内部：建 UniformConstant 树 → AddUniformExpression 入库
   → 化简成立：不生成 "(A * 0)"，直接得到常量 0 的索引
```

ConstResultValue 是**所有折叠算子共同的出口**（Mul/Div/Add 折完都调它入库）；IsExpressionConstantValue 是化简判定的眼睛——两个函数合起来，代数化简才跑得通。

## ⑤ 之后

⑥ 算子（同文件）：`Constant/2/3/4` 搜 `.cpp:5211`；`Add/Sub/Mul/Div` 搜 `.cpp:9484` 起。⑥ 完成即课 6 收尾（compiler_test 按教案 lesson06.md 第九部分）。
