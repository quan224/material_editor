#pragma once
#include "Compiler/Public/CodeChunk.h"
#include "Compiler/Public/ConstantFolding.h"
#include "MaterialGraph/Public/Types.h"
#include <vector>
#include <map>
#include <string>
#include <set>
#include <nlohmann/json.hpp>

class Graph;
class Node;
class Expression;

class MaterialCompiler
{
public:
    struct CompileResult
    {
        bool success = false;
        std::string hlsl_code; // 生成HLSL代码(DX12直接使用)
        std::string error_message;
    };

    MaterialCompiler();

    // 主编译入口
    CompileResult Compile(Graph *graph);

    // 表达式调用的编译API

    // 编译一个输入引脚（递归编译上游节点）
    int32_t CompileInputPin(Node *node, const std::string &pin_name);

    // 算数运算, 返回的int32_t实际指的是chunks_数组的下标，而不是具体指值
    int32_t Add(int32_t a, int32_t b);
    int32_t Subtract(int32_t a, int32_t b);
    int32_t Multiply(int32_t a, int32_t b);
    int32_t Divide(int32_t a, int32_t b);
    int32_t Power(int32_t base, int32_t exp);
    int32_t Lerp(int32_t a, int32_t b, int32_t alpha);
    int32_t Clamp(int32_t x, int32_t minVal, int32_t maxVal);
    int32_t Abs(int32_t x);
    int32_t Negate(int32_t x);

    // 三角函数
    int32_t Sine(int32_t x);
    int32_t Cosine(int32_t x);

    // 向量运算
    int32_t Dot(int32_t a, int32_t b);
    int32_t Cross(int32_t a, int32_t b);
    int32_t Normalize(int32_t x);
    int32_t Length(int32_t x);

    // 常量
    int32_t Constant(float value);
    int32_t Constant2(float x, float y);
    int32_t Constant3(float x, float y, float z);
    int32_t Constant4(float x, float y, float z, float w);

    // 向量操作
    int32_t ComponentMask(int32_t input, bool r, bool g, bool b, bool a);
    int32_t AppendVector(int32_t a, int32_t b);

    // 纹理
    int32_t TextureCoordinate();
    int32_t TextureSample(int32_t texture, int32_t coordinate);

    // 控制
    int32_t If(int32_t condition, int32_t trueVal, int32_t falseVal);

    // 类型转换
    int32_t Cast(int32_t code, EValueType targetType);

    // 查询代码块信息
    std::string GetParameterCode(int32_t index) const;
    EValueType GetType(int32_t index) const;
    bool IsConstant(int32_t index) const;
    float GetConstantValue(int32_t index) const;

// private:
    // 代码块管理
    int32_t AddCodeChunk(EValueType type, const std::string& code, bool is_inline=false);
    int32_t AddConstantChunk(EValueType type, float value);
    std::string MakeSymbolName();

    // 解析 json 形式的默认值（如 0.5 或 [1,0,0]）为代码块索引
    int32_t ParseDefaultValue(const nlohmann::json& val, EValueType type);

    // 编译单个表达式节点
    std::vector<int32_t> CompileExpression(Node* node);

    // 最终代码生成（课8 完善）
    std::string GenerateCode(const std::map<std::string, int32_t>& outputs);

private:
    // 状态
    std::vector<CodeChunk> chunks_;
    std::map<uint64_t, int32_t> hash_to_chunk_;              // 哈希去重
    std::map<std::string, std::vector<int32_t>> node_cache_; // node_id -> 输出索引
    int32_t next_symbo_index_ = 0;
    Graph *current_graph_ = nullptr;
    std::string error_message_;
};