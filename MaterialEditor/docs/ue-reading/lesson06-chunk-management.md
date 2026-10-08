# 课 6 · ⑤ 代码块管理（AddCodeChunk / AddUniformExpression / 双层去重）—— 实现流程稿

> 操作向流程稿（做什么 / 怎么做 / 怎么验证），代码自己写。
> UE 原文均在本机镜像实测：
> - `Engine/Source/Runtime/Engine/Private/Materials/HLSLMaterialTranslator.cpp:3337-3405`（AddCodeChunkInner）
> - 同文件 `:3610-3723`（AddUniformExpressionInner）
> - 同文件 `:3500-3540` 附近（CreateSymbolName / AddCodeChunk / AddInlinedCodeChunk 外壳）
> 教案理论底稿：`MaterialEditor/docs/lessons/lesson06.md` 第六部分
> 前序：FShaderCodeChunk 全字段版已完成（ue-reading/lesson06-codechunk.md）

---

## 0. 这一步在全局的位置

| | 内容 |
|---|---|
| **输入** | FShaderCodeChunk 全字段版 + 表达式树六类（Constant/FoldedMath/Sine/Rcp/Abs/Neg）都已完成 |
| **产出** | 装配函数族：`CreateSymbolName`、`AddCodeChunk`、`AddInlinedCodeChunk`、`AddUniformExpression`（双层去重）、`ConstResultValue`、查询四件套（`GetParameterCode`/`GetType`/`IsConstant`/`GetParameterUniformExpression`）+ `IsExpressionConstantValue` |
| **落点** | `HLSLTranslator`（若 ④ 两层拆分未做，先在现有 MaterialCompiler 上写，④ 时平移——函数体不变） |
| **下游消费者** | ⑥ 算子（Constant/2/3/4 走 AddUniformExpression；Add/Mul 走 AddInlinedCodeChunk/AddCodeChunk）、课 7 CompileExpression、课 8 生成器读 chunk 表 |
| **本步不做** | 算子三轨（⑥）、编译器两层拆分（④，可后平移）、作用域（CurrentScopeChunks 先用单个 vector 顶——`chunks_`） |
| **量级** | UE 原文 ~250 行 → 教学版约 200 行（本步 + ⑥ 合计就是"还剩 500 行"的主体） |

## 1. 动手前的两个简化裁决（对照 UE 差异，明说）

| UE 原文 | 教学版 | 理由 |
|---|---|---|
| `CurrentScopeChunks`（作用域数组，TArray*）| `chunks_`（单 vector）| 无 Custom 作用域前只有一个作用域；字段已在你现有 MaterialCompiler 里 |
| `new(*CurrentScopeChunks) FShaderCodeChunk(...)`（MemStack 版 placement new）| `chunks_.push_back(FShaderCodeChunk(...))` | 你的 chunks_ 是 vector 非 MemStack；你刚写好的两个构造直接用 |
| UE 的 `Errorf`（在函数内报错）| `EmitError`（你已有的收集器）| 命名对齐你的错误系统 |
| `Hash` 由调用方（外层 AddCodeChunk）算好传入 | 同样保持传入 | ⑤ 不管哈希怎么算（⑥ 算子负责）|

## 2. UE 侧结构：两层壳 + 一个核

UE 的调用链（先看清楚再动手）：

```
算子（⑥）
  └─ AddCodeChunk(type, "code...", ...)          ← 外壳 .cpp:3500 附近
       │  职责：算 hash（HashString(code)）、格式化类型名
       └─ AddCodeChunkInner(hash, type_str, code, type, deriv, bInlined)   ← 核 :3337
            职责：装 chunk、去重、报错

算子（⑥ 常量/树路径）
  └─ AddUniformExpression(hash, expr, type, "code...")  ← 外壳
       └─ AddUniformExpressionInner(...)                  ← 核 :3610
            职责：IsIdentical 双层去重、装 chunk
```

教学版可以合并壳与核（少一层转发，行为等价）——**每个函数直接收最终参数**。

## 3. 逐函数：UE 行为 → 你的版本

### 3.1 CreateSymbolName（对照 UE CreateSymbolName，搜 `CreateSymbolName(TEXT("Local"))`）

```
UE：FString FHLSLMaterialTranslator::CreateSymbolName(const TCHAR* Prefix)
    → Prefix + 递增计数器（Local0/Local1/...）
你：已有 MakeSymbolName()——留用，或改名对齐 CreateSymbolName（命名裁决：方法对齐 UE）
```

### 3.2 AddCodeChunk（核 = UE :3337-3405）

**UE 行为逐段**（原文在手，照抄这段判断顺序）：

```
① Type == MCT_Unknown            → return INDEX_NONE
② bInlined                        → 直接追加：FShaderCodeChunk(hash, code, code, "", type, deriv, true)
                                   （内联块不去重！UE 原文如此——内联串太短，去重得不偿失）
③ 数值类可建临时变量（MCT_Float|LWCType|UInt... 或 ShadingModel/MaterialAttributes/Substrate）
   → 先线性扫 hash 去重（命中 → 复用索引）
   → 未命中 → symbol_name = CreateSymbolName("Local")
              definition = "\t类型名 Local0 = code;\n"（含声明！）
              追加 FShaderCodeChunk(hash, def_finite, def_analytic, symbol, type, deriv, false)
④ 其余（纹理等）                   → Errorf("Operation not supported on a Texture")
```

**教学版签名建议**（合壳核）：

```cpp
int32_t AddCodeChunk(EMaterialValueType type, const std::string& code,
                     bool b_inlined, EDerivativeStatus deriv = EDerivativeStatus::NotAware);
// 内部：hash = HashString(code)；③ 的类型判定用你的 IsNumericType/MCT_Float 等谓词
```

**③ 的类型白名单**照抄 UE 那行（用你的位名）：
`type & (MCT_Float|MCT_LWCType|MCT_UInt) || type==MCT_ShadingModel || type==MCT_MaterialAttributes || type==MCT_Substrate`

### 3.3 AddInlinedCodeChunk（UE :3500 附近外壳）

一行：`return AddCodeChunk(type, code, true, deriv);`

### 3.4 AddUniformExpression（核 = UE :3610-3723，本步最难）

**UE 行为分五段**：

```
① Type==Unknown / 纹理类型但树非贴图表达式 / StaticBool → Errorf + INDEX_NONE
② chunk 级去重：线性扫 CurrentScopeChunks，
   existing->IsIdentical(新树) 命中 → delete 新树（UE :3662 语义）→ 复用旧索引
③ 材质级去重：扫 UniformExpressions 表（跨属性共享表），
   命中 → 新树作废 → expr = 登记树（仍建新 chunk，symbol 复用）
④ 装配：FShaderCodeChunk(hash, expr, code, code_analytic, type, deriv)
   （带表达式构造：symbol_name 空、b_inline 恒 false）
⑤ 登记：新树加入材质级 UniformExpressions 表
```

**教学版注意三点**：
- 你的树是 `Ref<>`（shared_ptr）——UE 的 `delete 新树` 在你这边就是「Ref 离开作用域自动释放」，不用手写
- 材质级表：`std::vector<Ref<FMaterialUniformExpression>> uniform_expressions_` 成员（对照 UE 的 `UniformExpressions`）
- ②③ 的 IsIdentical 调用链已在你的树基类实现——这是它们第一次被真正使用

### 3.5 ConstResultValue（UE 搜同名）

```
UE：int32 FHLSLMaterialTranslator::ConstResultValue(EMaterialValueType type, const FLinearColor& value)
    → new FMaterialUniformExpressionConstant(value, type) → AddUniformExpression
你：MakeRef<FMaterialUniformExpressionConstant>(Vec4, type) → AddUniformExpression
（⑥ 算子折叠完成时的出口，本步先备好）
```

### 3.6 查询四件套（你已有三个，重写语义 + 补一个）

| 函数 | 新语义（对照 UE） |
|---|---|
| `GetParameterCode(index)` | **三段判断**：`uniform_expression ? definition_finite : (b_inline ? definition_finite : symbol_name)`（UE GetParameterCode 同序）|
| `GetType(index)` | 返回 `type_`（已有，字段名跟着你的改）|
| `IsConstant(index)` | `uniform_expression && uniform_expression->IsConstant()`（树递归）|
| `GetParameterUniformExpression(index)` | 新增：返回 `uniform_expression.get()`（⑥ 算子取树用）|
| `IsExpressionConstantValue(index, compare)` | 新增：常量且全分量 == compare（UE IsExpressionConstantValue——Mul 的 x*0/x*1 化简全靠它，UE `.cpp` 搜同名）|

## 4. 旧代码退役清单（MaterialCompiler.cpp）

| 旧函数 | 处置 |
|---|---|
| `AddCodeChunk`（旧版：手动拼字段）| 被 3.2 替换（签名相近，直接改写）|
| `AddConstantChunk`（variant 版）| **删除**——职责被 3.4+3.5 取代 |
| `IsConstant/GetConstantValue/FormatConstantCode` | IsConstant 改树语义；GetConstantValue/FormatConstantCode 删除（⑥ 折叠走树求值 + ConstResultValue）|
| `ConstFolding`（include 与调用）| 本步脱钩，文件可删（⑥ 不再用它）|

## 5. 验证（写进 ExprTreeTest 或新建 ChunkTest，沿用现有 Demos 测试流）

```cpp
// ① 纯代码块 + hash 去重
int32 a = c.AddCodeChunk(MCT_Float, "SomeTexture.Sample(uv)", false);
int32 b = c.AddCodeChunk(MCT_Float, "SomeTexture.Sample(uv)", false);   // 同串
assert(a == b);                                   // 命中去重
assert(c.GetParameterCode(b) == "Local0");        // 非内联 → 变量名
assert(!c.GetParameterUniformExpression(a));      // 纯代码块无树

// ② 内联不去重
int32 i1 = c.AddInlinedCodeChunk(MCT_Float, "(A + B)");
int32 i2 = c.AddInlinedCodeChunk(MCT_Float, "(A + B)");
assert(i1 != i2);                                 // UE 行为：内联不去重
assert(c.GetParameterCode(i1) == "(A + B)");      // 内联 → 定义串

// ③ 表达式块 + 双层去重（树第一次上岗）
int32 e1 = c.AddUniformExpression(
    MakeRef<FMaterialUniformExpressionConstant>(Vec4(2,2,2,2), MCT_Float).get(),
    MCT_Float, "2.0");
int32 e2 = c.AddUniformExpression(
    MakeRef<FMaterialUniformExpressionConstant>(Vec4(2,2,2,2), MCT_Float).get(),
    MCT_Float, "2.0");                            // 语义相同的另一棵树
assert(e1 == e2);                                 // IsIdentical 命中
assert(c.GetParameterCode(e1) == "2.0");          // 表达式块 → 定义串直接用
assert(c.IsConstant(e1));                         // 树递归：纯常量
assert(c.GetParameterUniformExpression(e1));      // 树挂上了

// ④ ConstResultValue + IsExpressionConstantValue
int32 f = c.ConstResultValue(MCT_Float, Vec4(0,0,0,0));
assert(c.IsExpressionConstantValue(f, 0.0f));     // 零常量判定（⑥ x*0 的依据）

// ⑤ 类型拦截
assert(c.AddCodeChunk(MCT_Texture2D, "xxx", false) < 0);   // 纹理不能建临时变量 → 报错路径
```

## 6. 完成标志（本步可勾）

- [ ] `CreateSymbolName`（或保留 MakeSymbolName 名）+ `AddCodeChunk`（Unknown/内联/白名单/纹理报错四段）
- [ ] `AddInlinedCodeChunk` + 内联不去重行为
- [ ] `AddUniformExpression`：chunk 级 IsIdentical 去重 + 材质级表 + 登记树
- [ ] `ConstResultValue`（树出口）
- [ ] 查询四件套新语义 + `GetParameterUniformExpression` + `IsExpressionConstantValue`
- [ ] 旧 variant 体系退役（AddConstantChunk/GetConstantValue/ConstFolding 引用清零）
- [ ] §5 验证全过（尤其 e1==e2 的双层去重——树 + chunk 第一次合体运转）

## 7. 附：UE ↔ 教学版函数对照

| 教学版 | UE | UE 位置 |
|---|---|---|
| AddCodeChunk（合壳核）| AddCodeChunk + AddCodeChunkInner | .cpp:3500 / :3337 |
| AddInlinedCodeChunk | 同名 | 同上附近 |
| AddUniformExpression（合壳核）| AddUniformExpression + AddUniformExpressionInner | .cpp:3610 |
| ConstResultValue | 同名 | 算子区（搜同名）|
| GetParameterCode（三段）| 同名 | 搜 `GetParameterCode` |
| IsExpressionConstantValue | 同名 | 搜同名（Mul 化简处）|
| uniform_expressions_（成员）| UniformExpressions | HLSLMaterialTranslator.h:346 附近 |

## 8. 之后的路线（"500 行"的下半场 = ⑥）

⑤ 完成 → ⑥ 算子（Constant/2/3/4 → Add → Mul → Div → 其余），⑥ 完成即课 6 收尾（compiler_test 跑教案第九部分全断言）。
