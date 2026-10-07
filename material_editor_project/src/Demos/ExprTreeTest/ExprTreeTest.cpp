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
	// 常量叶子
	auto c1 = MakeRef<FMaterialUniformExpressionConstant>(Vec4(1, 1, 1, 1), MCT_Float);
	auto c2 = MakeRef<FMaterialUniformExpressionConstant>(Vec4(2, 2, 2, 2), MCT_Float);
	Vec4 v;
	c1->GetNumbervalue(ctx, v);
	P("Constant(1).value.x = %.1f (expect 1.0)  IsConstant=%d (expect 1)\n", v.x, c1->IsConstant());
	P("IsIdentical(1,1)=%d (expect 1)  IsIdentical(1,2)=%d (expect 0)\n", c1->IsIdentical(c1.get()), c1->IsIdentical(c2.get()));

	// [2] 二元：1+2 求值 3；嵌套 (1+2)*3 求值 9
	auto add = MakeRef<FMaterialUniformExpressionFoldedMath>(c1, c2, FMO_Add, MCT_Float);
	add->GetNumbervalue(ctx, v);
	P("FoldedMath(1,2,Add).x = %.1f (expect 3.0)  IsConstant=%d\n", v.x, add->IsConstant());
	auto c3 = MakeRef<FMaterialUniformExpressionConstant>(Vec4(3, 3, 3, 3), MCT_Float);
	auto mul = MakeRef<FMaterialUniformExpressionFoldedMath>(add, c3, FMO_Mul, MCT_Float);
	mul->GetNumbervalue(ctx, v);
	P("FoldedMath((1+2),3,Mul).x = %.1f (expect 9.0)\n", v.x);

	// [3] 同构树 IsIdentical 去重语义
	auto add2 = std::make_shared<FMaterialUniformExpressionFoldedMath>(
		std::make_shared<FMaterialUniformExpressionConstant>(Vec4(1, 1, 1, 1), MCT_Float),
		std::make_shared<FMaterialUniformExpressionConstant>(Vec4(2, 2, 2, 2), MCT_Float),
		FMO_Add, MCT_Float);
	P("IsIdentical(add, add2)=%d (expect 1)  —— 语义去重的根据\n", add->IsIdentical(add2.get()));
	auto sub = std::make_shared<FMaterialUniformExpressionFoldedMath>(c1, c2, FMO_Sub, MCT_Float);
	P("IsIdentical(add, sub)=%d (expect 0)\n", add->IsIdentical(sub.get()));

	// [4] Dot（前 N 分量）：float3(1,0,0)·float3(0,1,0) = 0；(1,0,0)·(1,0,0) = 1
	auto d1 = std::make_shared<FMaterialUniformExpressionConstant>(Vec4(1, 0, 0, 0), MCT_Float3);
	auto d2 = std::make_shared<FMaterialUniformExpressionConstant>(Vec4(0, 1, 0, 0), MCT_Float3);
	auto dot = std::make_shared<FMaterialUniformExpressionFoldedMath>(d1, d2, FMO_Dot, MCT_Float3);
	dot->GetNumbervalue(ctx, v);
	P("Dot((1,0,0),(0,1,0)).x = %.1f (expect 0.0)\n", v.x);

	// [5] 一元四类
	auto c_pi = std::make_shared<FMaterialUniformExpressionConstant>(Vec4(0, 0, 0, 0), MCT_Float);
	auto sin0 = std::make_shared<FMaterialUniformExpressionSine>(c_pi, /*b_is_cosine=*/false);
	auto cos0 = std::make_shared<FMaterialUniformExpressionSine>(c_pi, /*b_is_cosine=*/true);
	sin0->GetNumbervalue(ctx, v);  P("Sine(0).x = %.1f (expect 0.0)\n", v.x);
	cos0->GetNumbervalue(ctx, v);  P("Cosine(0).x = %.1f (expect 1.0)\n", v.x);
	P("IsIdentical(sin,cos)=%d (expect 0，b_is_cosine 参与比较)\n", sin0->IsIdentical(cos0.get()));

	auto cneg = std::make_shared<FMaterialUniformExpressionConstant>(Vec4(-1, 3, 0, 0), MCT_Float2);
	auto aabs = std::make_shared<FMaterialUniformExpressionAbs>(cneg);   aabs->GetNumbervalue(ctx, v);
	P("Abs(-1,3) = %.1f %.1f (expect 1.0 3.0)\n", v.x, v.y);
	auto nneg = std::make_shared<FMaterialUniformExpressionNeg>(c3);     nneg->GetNumbervalue(ctx, v);
	P("Neg(3).x = %.1f (expect -3.0)\n", v.x);
	auto ctwo = std::make_shared<FMaterialUniformExpressionConstant>(Vec4(2, 2, 2, 2), MCT_Float);
	auto rcp2 = std::make_shared<FMaterialUniformExpressionRcp>(ctwo);   rcp2->GetNumbervalue(ctx, v);
	P("Rcp(2).x = %.1f (expect 0.5)\n", v.x);
}

