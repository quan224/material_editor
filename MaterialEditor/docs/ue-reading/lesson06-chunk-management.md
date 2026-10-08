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

## ⑤ 之后

⑥ 算子（同文件）：`Constant/2/3/4` 搜 `.cpp:5211`；`Add/Sub/Mul/Div` 搜 `.cpp:9484` 起。⑥ 完成即课 6 收尾（compiler_test 按教案 lesson06.md 第九部分）。
