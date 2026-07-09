// compiler_test.cpp
// 课6 编译器测试 —— 绕过 Graph，直接调 MaterialCompiler 的 API，看生成的 HLSL
//
// 为什么课6 就能测：编译器的算术 API（Constant/Add/Multiply...）是 public 的，
// 不依赖 Graph/Expression（那是课7、课9 才打通的端到端路径）。我们手动调这些 API
// 构造代码块，再用 GenerateCode 拼成 HLSL，就能验证编译器核心逻辑。
//
// === 使用前提 ===
// 1. MaterialCompiler.cpp 里实现了被测方法（Constant/Add/Multiply/Divide/
//    Constant3/ComponentMask/GenerateCode 等）。没实现的方法会链接报错
//    （undefined reference to MaterialCompiler::XXX），按报错顺序逐个实现即可。
// 2. GenerateCode 原本是 private。测试要看 HLSL，需在 MaterialCompiler.h 里
//    临时把它挪到 public 段（等课9 用 Compile(Graph) 后改回 private）。
// 3. 在 main.cpp 里调用（暂时注释掉课5c 的 QApplication 代码）：
//        extern void RunCompilerTest();
//        int main() { RunCompilerTest(); return 0; }
//
// 用法：边实现 MaterialCompiler.cpp 边跑这个测试，每实现一块就看到对应项变 PASS。
#pragma once
#include "Compiler/Public/MaterialCompiler.h"
#include <iostream>
#include <map>
#include <string>

static void Check(bool cond, const std::string &desc)
{
    std::cout << (cond ? "  [PASS] " : "  [FAIL] ") << desc << "\n";
}

void RunCompilerTest()
{
    std::cout << "\n========== 课6 编译器测试 ==========\n";
    MaterialCompiler c;

    // --- 1. 常量 + 常数折叠 ---
    std::cout << "\n[1] 常量 & 常数折叠\n";
    {
        int32_t a = c.Constant(1.0f);
        int32_t b = c.Constant(2.0f);
        int32_t s = c.Add(a, b); // 两边都是常量 → 折叠成 3.0
        Check(c.IsConstant(s), "Add(1,2) 结果是常量");
        Check(c.GetConstantValue(s) == 3.0f, "1 + 2 折叠 = 3.0");
    }

    // // --- 2. Multiply 优化（乘0归零、乘1直通）---
    // std::cout << "\n[2] Multiply 优化\n";
    // {
    //     int32_t x = c.Constant(5.0f);
    //     int32_t z = c.Constant(0.0f);
    //     int32_t one = c.Constant(1.0f);

    //     int32_t mul0 = c.Multiply(x, z);
    //     Check(c.GetConstantValue(mul0) == 0.0f, "5 * 0 -> 0");

    //     int32_t mul1 = c.Multiply(x, one);
    //     Check(mul1 == x, "5 * 1 直通（返回 x 的索引，不生成新代码块）");
    // }

    // // --- 3. 除零保护（不崩，返回 0）---
    // std::cout << "\n[3] 除零保护\n";
    // {
    //     int32_t x = c.Constant(5.0f);
    //     int32_t z = c.Constant(0.0f);
    //     int32_t r = c.Divide(x, z);
    //     Check(c.GetConstantValue(r) == 0.0f, "5 / 0 安全返回 0");
    // }

    // // --- 4. 向量常量 + 掩码 ---
    // std::cout << "\n[4] 向量 & ComponentMask\n";
    // {
    //     int32_t col = c.Constant3(1.0f, 0.5f, 0.0f); // float3
    //     int32_t rg = c.ComponentMask(col, true, true, false, false); // 取 rg -> float2
    //     std::cout << "  Constant3(1, 0.5, 0).rg 的 code = \""
    //               << c.GetParameterCode(rg) << "\"\n";
    //     Check(c.GetType(rg) == EValueType::Float2, "掩码 2 个分量 -> Float2");
    // }

    // // --- 5. 嵌套运算 ---
    // std::cout << "\n[5] 嵌套运算 2*3 + 4\n";
    // {
    //     int32_t n = c.Add(
    //         c.Multiply(c.Constant(2.0f), c.Constant(3.0f)),
    //         c.Constant(4.0f));
    //     Check(c.GetConstantValue(n) == 10.0f, "2*3 + 4 折叠 = 10");
    // }

    // // --- 6. 组装材质输出 + 生成 HLSL ---
    // std::cout << "\n[6] 生成 HLSL\n";
    // {
    //     // 新建一个干净的编译器，避免上面测试的 chunks 混进来
    //     MaterialCompiler mc;
    //     std::map<std::string, int32_t> outputs;
    //     outputs["BaseColor"] = mc.Constant3(1.0f, 0.5f, 0.0f);
    //     outputs["Metallic"] = mc.Add(mc.Constant(1.0f), mc.Constant(2.0f));
    //     outputs["Roughness"] = mc.Constant(0.5f);

    //     std::string hlsl = mc.GenerateCode(outputs); // <- 需把 GenerateCode 改 public
    //     std::cout << "------ 生成的 HLSL ------\n"
    //               << hlsl
    //               << "------------------------\n";
    // }

    // std::cout << "\n========== 测试结束 ==========\n\n";
}
