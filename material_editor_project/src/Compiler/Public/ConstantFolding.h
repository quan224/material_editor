#pragma once
#include <cmath>
#include <optional>
#include <string>

class ConstantFolding
{
public:
    // 尝试二元运算折叠  binary(二元)
    static std::optional<float> FoldBinary(const std::string &op, float a, float b)
    {
        if (op == "+")
            return a + b;
        if (op == "-")
            return a - b;
        if (op == "*")
            return a * b;
        if (op == "/")
            return b != 0.0f ? a / b : std::optional<float>{};
        if (op == "pow")
            return std::pow(a, b);
        return {};
    }

    // 尝试一元运算折叠  unary(一元)
    static std::optional<float> FoldUnary(const std::string &op, float a)
    {
        if (op == "abs")
            return abs(a);
        if (op == "neg")
            return -(a);
        if (op == "sin")
            return std::sin(a);
        if (op == "cos")
            return std::cos(a);
        return {};
    }
};