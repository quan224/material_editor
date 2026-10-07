# 课 6 · 表达式树子类族 —— 实现流程稿

> 本文是操作向流程稿（做什么 / 怎么做 / 怎么验证），代码自己写。
> 所有 UE 行号均为本机镜像实测：`E:\UE5_mirror\UE5\UnrealEngine-release\Engine\Source\Runtime\Engine\Private\Materials\MaterialUniformExpressions.h`
> 教案理论底稿：`MaterialEditor/docs/lessons/lesson06.md` 第二部分

---

## 0. 这一步在全局的位置

| | 内容 |
|---|---|
| **输入** | 基类 `MaterialUniformExpression` 已就位（GetType 注册表 / IsConstant / IsIdentical / GetNumbervalue / GetChildren） |
| **产出** | 6 个类 + 1 个枚举：`FMaterialUniformExpressionConstant`、`EFoldedMathOperation` + `FMaterialUniformExpressionFoldedMath`、`FMaterialUniformExpressionSine`（含 cosine）、`FMaterialUniformExpressionRcp`、`FMaterialUniformExpressionAbs`、`FMaterialUniformExpressionNeg` |
| **下游消费者** | 课 6 第六部分 `AddUniformExpression`（IsIdentical 双层去重）、第七部分算子三轨判定（Add 建树 / Mul 折叠 new 这些类）、课 7 `CompileExpression`、课 20 `WriteNumberOpcodes` + 参数叶子 |
| **本步不做**（后续课） | 参数族叶子（课 20 `UniformNumericParameter`）、贴图族叶子（课 14）、`WriteNumberOpcodes` 字节码（课 20） |

## 1. 先知道的一个 UE 5.8 事实（决定课 6 的写法）

**UE 5.8 原文里，中间节点类没有 GetNumberValue**——`FoldedMath`(:1103)、`Sine`(:593)、`Rcp`(:756)、`Abs`(:1839) 只实现了 `WriteNumberOpcodes`（把树编成 preshader 字节码），只有 `Constant`(:296) 这类叶子保留解释求值 `GetNumberValue`。UE 的 CPU 求值已收敛到字节码 VM 一条路。

**教学版顺序反着走**（lesson06 已定稿，不要改）：课 6 先写**解释路径**——每个类实现 `GetNumbervalue`（递归直接算），立刻可测、不依赖 VM；课 20 再补 `WriteNumberOpcodes` 字节码路径，双路径结果一致性由课 20 的测试兜底。

所以本步每个类 = **UE 结构逐字段对齐 + 额外补 GetNumbervalue**（教学版新增部分，教案第二部分有底稿）。

## 2. 动手前的三个前置修正（基类文件）

1. **宏拼写错误**：`DECLARE_MATERIALUNIFROMEXPRESSION_TYPE` → `UNIFROM` 拼错了，应为 `DECLARE_MATERIALUNIFORMEXPRESSION_TYPE`
2. **IMPLEMENT 宏改 inline**（header-only 家族的安全写法）：
   ```cpp
   #define IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(Name) \
       inline MaterialUniformExpressionType Name::static_type(#Name);
   ```
   原因：全部子类放一个 .h，IMPLEMENT 是静态成员的类外定义，非 inline 会被每个 include 它的翻译单元各定义一份 → 链接重定义。C++17 的 inline 变量允许跨 TU 合并。
3. ~~`GetNumbervalue` 基类声明无定义~~ → **已改为纯虚 `= 0`**（比默认实现更好：子类漏写直接编译报错）。

## 3. Step 1：FMaterialUniformExpressionConstant（UE :257-306）

**做什么**：常量叶子——"编译期就知道的数"。三种值形态（纯常量/含参数/纯 GPU）里第一种的载体。

**怎么做**（逐项对照）：

| 项 | UE 原文 | 教学版 |
|---|---|---|
| 字段 | `FLinearColor Value` + `uint8 ValueType`（:304-305） | `Vec4 value_` + `EValueType value_type_` |
| 存储语义 | 4 分量统一存储：float1 填 (x,x,x,x)，float2 填 (x,y,0,0)——**不是 variant**（UE 用 FLinearColor 当容器，教学版 Vec4 同角色） | 同 |
| 类型标签 | 标量用 **MCT_Float**（不是 MCT_Float1）——调用处 `HLSLMaterialTranslator.cpp:5214` 传的就是 MCT_Float（"标量表达式返回类型用 MCT_Float"规则） | 同 |
| 构造 | `Constant(const FLinearColor&, uint8)`（:262） | `FMaterialUniformExpressionConstant(const Vec4&, EValueType)` |
| IsConstant | `return true;`（:267） | 同 |
| IsIdentical | ① `GetType() != Other->GetType()` → false（:271，用**类型对象比较**，不用 dynamic_cast）② 字段比较 `ValueType == && Value ==`（:276，FLinearColor 的 == 是 4 分量逐个比） | ① 同款 GetType 比较 ② value_type_ 相等 && Vec4 四分量逐个相等 |
| GetNumbervalue | `OutValue = Value;`（:296） | `out_value = value_;` |
| 注册 | `DECLARE_..._TYPE(FMaterialUniformExpressionConstant)`（:259） | 类内 DECLARE 宏 + 类外 IMPLEMENT 宏 |

**验证**（写完即可手测，见 §6）：
- `FMaterialUniformExpressionConstant(Vec4(1,1,1,1), MCT_Float)` 求值 → (1,1,1,1)
- 两个同值实例 `IsIdentical` → true；值不同 → false

## 4. Step 2：EFoldedMathOperation + FMaterialUniformExpressionFoldedMath（UE :1092-1158）

**做什么**：二元运算节点——`Add/Sub/Mul/Div/Dot/Cross` 的树形表达。课 6 第七部分 Add 算子"建树不折叠"建的就是它。

**枚举**（UE :1092 原样照抄）：

```cpp
enum EFoldedMathOperation
{
    FMO_Add,
    FMO_Sub,
    FMO_Mul,
    FMO_Div,
    FMO_Dot,
    FMO_Cross
};
```

**类**（UE :1103，逐项）：

| 项 | UE 原文 | 教学版 |
|---|---|---|
| 字段 | `TRefCountPtr A, B` + `uint32 ValueType` + `uint8 Op`（:1154-1157） | `Ref<FMaterialUniformExpression> a_, b_` + `EValueType value_type_` + `uint8_t op_` |
| 默认构造 | `ValueType(MCT_Float)`（:1106） | `value_type_(MCT_Float)` |
| 参数构造 | `(A, B, Op, ValueType=MCT_Float)`（:1108）——裸指针入参，TRefCountPtr 接管 | `(const Ref&, const Ref&, op, type=MCT_Float)`——用 Ref 直接传 |
| value_type_ 的意义 | "操作数维度"标签：Dot/Cross 求值时决定取几个分量；普通四则不用它（4 分量全算） | 同 |
| IsConstant | `A->IsConstant() && B->IsConstant()`（:1134，递归） | 同 |
| IsIdentical | GetType 比 → `A->IsIdentical(Other->A) && B->... && Op== && ValueType==`（:1138-1146，**四元组递归**） | 同 |
| GetChildren | `TArrayView(&A, 2)`（:1151） | `return {a_.get(), b_.get()};` |
| GetNumbervalue | **UE 5.8 无**（只有 WriteNumberOpcodes :1110）——教学版补（教案底稿 lesson06.md 第二部分） | 递归两个孩子 → 按运算组合 |

**GetNumbervalue 的求值规则**（教案底稿 + FLinearColor 运算语义）：

- Add/Sub/Mul/Div：**4 分量全算**（`FLinearColor::operator*` 就是逐分量——维度裁剪交给类型标签，不在求值里做）
- Div 分量为 0：**直接除**（得 inf，不特判）——除零的 Warning/防护在第七部分 `Divide` 算子层做，UE 同款分层
- Dot：前 N 分量点积（`N = GetComponentCount(value_type_)`，用 ValueType.h 已有的谓词），结果填 x，其余 0
- Cross：前 3 分量叉积
- 逐分量访问建议：
  ```cpp
  static_assert(sizeof(Vec4) == 4 * sizeof(float), "Vec4 must be tightly packed");
  // (&v.x)[i] 按分量遍历
  ```

**验证**：
- `FoldedMath(Constant(1), Constant(2), Add)` → IsConstant true，求值 x=3
- 嵌套 `FoldedMath(FoldedMath(1,2,Add), Constant(3), Mul)` → 求值 9
- 同构两棵树 IsIdentical → true；op 不同 → false

## 5. Step 3：一元四类（Rcp :756 / Abs :1839 / Sine :593 / Neg）

**先纠正教案的一个简化**：UE 的 **Cosine 不是独立类**——`FMaterialUniformExpressionSine` 带布尔 `bIsCosine`（:599 构造第二参，:620 参与 IsIdentical）。教学版对齐 UE 用一个类带 bool，不建 UniformCosine。

`Neg`：UE 无独立类（它是 preshader 的 Neg opcode，`Preshader.h:66`）——教学版为统一性建 `FMaterialUniformExpressionNeg`（教案底稿如此）。

**同构模板**（以最短的 Rcp :756-781 为参照）：

```cpp
class FMaterialUniformExpressionRcp : public MaterialUniformExpression {
    DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionRcp)
public:
    FMaterialUniformExpressionRcp() = default;
    explicit FMaterialUniformExpressionRcp(const Ref<MaterialUniformExpression>& x) : x_(x) {}

    bool IsConstant() const override { return x_->IsConstant(); }          // UE :768 转发
    bool IsIdentical(MaterialUniformExpression* other) const override {
        if (GetType() != other->GetType()) return false;                   // UE :773
        auto* o = static_cast<FMaterialUniformExpressionRcp*>(other);
        return x_->IsIdentical(o->x_.get());                               // UE :777 递归
    }
    void GetNumbervalue(const MaterialRenderContext&, Vec4& out) const override {
        x_->GetNumbervalue(ctx, out);
        out = Vec4(1.0f/out.x, 1.0f/out.y, 1.0f/out.z, 1.0f/out.w);        // 教学版补
    }
    std::vector<const MaterialUniformExpression*> GetChildren() const override { return {x_.get()}; }

private:
    Ref<MaterialUniformExpression> x_;
};
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionRcp)
```

四个类的差异只有求值函数一行：

| 类 | UE 行号 | 求值 | 除零/特殊 |
|---|---|---|---|
| FMaterialUniformExpressionRcp | :756 | `1.0f / x` 逐分量 | 分量 0 → inf，不特判（UE 不特判） |
| FMaterialUniformExpressionAbs | :1839 | `std::abs(x)` 逐分量 | — |
| FMaterialUniformExpressionSine | :593 | `b_is_cosine_ ? cos : sin` 逐分量 | `b_is_cosine_` 要进 IsIdentical（UE :620）|
| FMaterialUniformExpressionNeg | （无 UE 类） | `-x` 逐分量 | — |

**验证**：Sine(0)=0、Sine(π/2,bIsCosine=true)=1（即 cos(0)=1）、Abs(-1)=1、Rcp(2)=0.5、Neg(3)=-3。

## 6. 统一验证方式（Demos 模式）

参照 ShaderTypesTest 的挂法：建 `src/Demos/ExprTreeTest/ExprTreeTest.{h,cpp}` 骨架，md 底稿如下，main 挂 `RunExprTreeTest()`：

```cpp
// src/Demos/ExprTreeTest/ExprTreeTest.h
#pragma once
void RunExprTreeTest();
```

```cpp
// src/Demos/ExprTreeTest/ExprTreeTest.cpp —— 内容按本节填
#define _CRT_SECURE_NO_WARNINGS
#include "Demos/ExprTreeTest/ExprTreeTest.h"
#include "Expression/Public/UniformExpression.h"
#include "MaterialTypes/Public/ValueType.h"
#include <cstdio>

static FILE* g_out;
#define P(...) do { fprintf(g_out, __VA_ARGS__); printf(__VA_ARGS__); } while(0)
static bool Eq(float a, float b) { return a == b; }   // 测试值全是精确可表示的，直接比

void RunExprTreeTest() {
    g_out = fopen("expr_tree_test_output.txt", "w");
    MaterialRenderContext ctx;

    // [1] 常量叶子
    auto c1 = std::make_shared<FMaterialUniformExpressionConstant>(Vec4(1,1,1,1), MCT_Float);
    auto c2 = std::make_shared<FMaterialUniformExpressionConstant>(Vec4(2,2,2,2), MCT_Float);
    Vec4 v;
    c1->GetNumbervalue(ctx, v);
    P("Constant(1).value.x = %.1f (expect 1.0)  IsConstant=%d (expect 1)\n", v.x, c1->IsConstant());
    P("IsIdentical(1,1)=%d (expect 1)  IsIdentical(1,2)=%d (expect 0)\n",
      c1->IsIdentical(c1.get()), c1->IsIdentical(c2.get()));

    // [2] 二元：1+2 求值 3；嵌套 (1+2)*3 求值 9
    auto add = std::make_shared<FMaterialUniformExpressionFoldedMath>(c1, c2, FMO_Add, MCT_Float);
    add->GetNumbervalue(ctx, v);
    P("FoldedMath(1,2,Add).x = %.1f (expect 3.0)  IsConstant=%d\n", v.x, add->IsConstant());
    auto c3 = std::make_shared<FMaterialUniformExpressionConstant>(Vec4(3,3,3,3), MCT_Float);
    auto mul = std::make_shared<FMaterialUniformExpressionFoldedMath>(add, c3, FMO_Mul, MCT_Float);
    mul->GetNumbervalue(ctx, v);
    P("FoldedMath((1+2),3,Mul).x = %.1f (expect 9.0)\n", v.x);

    // [3] 同构树 IsIdentical 去重语义
    auto add2 = std::make_shared<FMaterialUniformExpressionFoldedMath>(
        std::make_shared<FMaterialUniformExpressionConstant>(Vec4(1,1,1,1), MCT_Float),
        std::make_shared<FMaterialUniformExpressionConstant>(Vec4(2,2,2,2), MCT_Float),
        FMO_Add, MCT_Float);
    P("IsIdentical(add, add2)=%d (expect 1)  —— 语义去重的根据\n", add->IsIdentical(add2.get()));
    auto sub = std::make_shared<FMaterialUniformExpressionFoldedMath>(c1, c2, FMO_Sub, MCT_Float);
    P("IsIdentical(add, sub)=%d (expect 0)\n", add->IsIdentical(sub.get()));

    // [4] Dot（前 N 分量）：float3(1,0,0)·float3(0,1,0) = 0；(1,0,0)·(1,0,0) = 1
    auto d1 = std::make_shared<FMaterialUniformExpressionConstant>(Vec4(1,0,0,0), MCT_Float3);
    auto d2 = std::make_shared<FMaterialUniformExpressionConstant>(Vec4(0,1,0,0), MCT_Float3);
    auto dot = std::make_shared<FMaterialUniformExpressionFoldedMath>(d1, d2, FMO_Dot, MCT_Float3);
    dot->GetNumbervalue(ctx, v);
    P("Dot((1,0,0),(0,1,0)).x = %.1f (expect 0.0)\n", v.x);

    // [5] 一元四类
    auto c_pi = std::make_shared<FMaterialUniformExpressionConstant>(Vec4(0,0,0,0), MCT_Float);
    auto sin0 = std::make_shared<FMaterialUniformExpressionSine>(c_pi, /*b_is_cosine=*/false);
    auto cos0 = std::make_shared<FMaterialUniformExpressionSine>(c_pi, /*b_is_cosine=*/true);
    sin0->GetNumbervalue(ctx, v);  P("Sine(0).x = %.1f (expect 0.0)\n", v.x);
    cos0->GetNumbervalue(ctx, v);  P("Cosine(0).x = %.1f (expect 1.0)\n", v.x);
    P("IsIdentical(sin,cos)=%d (expect 0，b_is_cosine 参与比较)\n", sin0->IsIdentical(cos0.get()));

    auto cneg = std::make_shared<FMaterialUniformExpressionConstant>(Vec4(-1,3,0,0), MCT_Float2);
    auto aabs = std::make_shared<FMaterialUniformExpressionAbs>(cneg);   aabs->GetNumbervalue(ctx, v);
    P("Abs(-1,3) = %.1f %.1f (expect 1.0 3.0)\n", v.x, v.y);
    auto nneg = std::make_shared<FMaterialUniformExpressionNeg>(c3);     nneg->GetNumbervalue(ctx, v);
    P("Neg(3).x = %.1f (expect -3.0)\n", v.x);
    auto ctwo = std::make_shared<FMaterialUniformExpressionConstant>(Vec4(2,2,2,2), MCT_Float);
    auto rcp2 = std::make_shared<FMaterialUniformExpressionRcp>(ctwo);   rcp2->GetNumbervalue(ctx, v);
    P("Rcp(2).x = %.1f (expect 0.5)\n", v.x);

    // [6] GetChildren（遍历契约，课 20 序列化用）
    P("FoldedMath children = %zu (expect 2)  Sine children = %zu (expect 1)\n",
      add->GetChildren().size(), sin0->GetChildren().size());

    P("\n==== EXPR TREE TEST DONE ====\n");
    fclose(g_out);
}
```

main.cpp 挂调用（两行，同 ShaderTypesTest 模式）。

> 注意：`std::make_shared<FMaterialUniformExpressionConstant>(Vec4(...), MCT_Float)` 直接构造子类——课 6 阶段没有工厂；第六部分 `AddUniformExpression` 落地后，这些裸构造都会经它去重。

## 7. 完成标志（本步可勾）

- [ ] 前置三修正：宏拼写 / IMPLEMENT inline 化 / 基类 GetNumbervalue 默认实现
- [ ] `FMaterialUniformExpressionConstant`（Vec4+标签、GetType 比较、求值直返）
- [ ] `EFoldedMathOperation` 六值枚举 + `FMaterialUniformExpressionFoldedMath`（四元组 IsIdentical、递归 IsConstant、四则 4 分量全算 / Dot 前 N / Cross 前 3、Div 不特判除零）
- [ ] 一元四类（Sine 带 b_is_cosine 对齐 UE / Rcp / Abs / Neg）
- [ ] 每类 DECLARE + IMPLEMENT 宏配套
- [ ] `RunExprTreeTest` 全部 expect 行通过
- [ ] 编译零警告（/W4）

## 附：UE ↔ 教学版对照总表（本机实测行号）

| 教学版 | UE 对应 | UE 行号 | 差异说明 |
|---|---|---|---|
| MaterialUniformExpression 基类 | FMaterialUniformExpression | :56 | 已有 |
| FMaterialUniformExpressionConstant | FMaterialUniformExpressionConstant | :257 | GetNumbervalue 对齐（UE :296）|
| EFoldedMathOperation | 同名 | :1092 | 原样照抄 |
| FMaterialUniformExpressionFoldedMath | FMaterialUniformExpressionFoldedMath | :1103 | **GetNumbervalue 教学版新增**（UE 只有 WriteNumberOpcodes :1110）|
| FMaterialUniformExpressionSine（含 cosine） | FMaterialUniformExpressionSine（bIsCosine） | :593 | GetNumbervalue 新增 |
| FMaterialUniformExpressionRcp | FMaterialUniformExpressionRcp | :756 | GetNumbervalue 新增 |
| FMaterialUniformExpressionAbs | FMaterialUniformExpressionAbs | :1839 | GetNumbervalue 新增 |
| FMaterialUniformExpressionNeg | （无类，Neg 是 opcode） | Preshader.h:66 | 教学版建类统一一元模式 |
| Ref<> | TRefCountPtr | — | shared_ptr 语义等价 |
