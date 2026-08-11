#pragma once
#include <map>
#include <vector>
#include <string>
#include "MaterialGraph/Public/Graph.h"
#include "Compiler/Public/CodeChunk.h"
#include "Compiler/Public/ConstantFolding.h"
#include "MaterialGraph/Public/Types.h"
#include "Core/Public/Hash.h"
#include "Compiler/Public/TypeSystem.h"


class MaterialCompiler{

public:
    struct CompileResult {
        bool success = false;
        std::string hlsl_code;
        std::string error_message;
    };

    CompileResult Compile(Graph* graph);

    // === 算子 API （图节点 Compile里调用）===
    int32_t CompileInputPin(Node* node, const std::string& pin_name);

    // 算术(三段判定 + 向量折叠)
    int32_t Add(int32_t a, int32_t b);
    int32_t Subtract(int32_t a, int32_t b);
    int32_t Multiply(int32_t a, int32_t b);
    int32_t Divide(int32_t a, int32_t b);
    int32_t Power(int32_t base, int32_t exp);
    int32_t Lerp(int32_t a, int32_t b, int32_t alpha);
    int32_t Clamp(int32_t x, int32_t min_val, int32_t max_val);
    int32_t Abs(int32_t x);
    int32_t Negative(int32_t x);

    // 三角函数
    int32_t Sine(int32_t x);
    int32_t Cosine(int32_t x);

    // 向量运算
    int32_t Dot(int32_t a, int32_t b);
    int32_t Cross(int32_t a, int32_t b);
    int32_t Normalize(int32_t x);
    int32_t Length(int32_t x);

    // 常量
    int32_t Constant(float x);
    int32_t Constant2(float x, float y);
    int32_t Constant3(float x, float y, float z);
    int32_t Constant4(float x, float y, float z, float w);

    // 向量操作/ 纹理 / 控制 / 类型转换
    int32_t ComponentMask(int32_t input, bool r, bool g, bool b, bool a);
    int32_t AppendVector(int32_t a, int32_t b);
    int32_t TextureCoordinate();
    int32_t TextureSample(int32_t texture, int32_t coordinate);
    int32_t If(int32_t condition, int32_t true_val, int32_t false_val);
    int32_t Cast(int32_t code, EValueType target_type);


    // === 查询（常量值返回variant）===
    std::string GetParameterCode(int32_t index) const;
    EValueType GetType(int32_t index) const;
    bool IsConstant(int32_t index) const;
    ConstValue GetConstantValue(int32_t index) const;


private:
    int32_t AddCodeChunk(EValueType type, const std::string& code, bool is_inline = false);
    int32_t AddInlineCodeChunk(EValueType type, const std::string& code);
    int32_t AddConstantChunk(EValueType type, const ConstValue& value);
    std::string FormatConstantCode(const ConstValue& value);

    // 接收variant
    std::string MakeSymbolName();
    int32_t ParseDefaultValue(const nlohmann::json& default_value, EValueType type);
    std::vector<int32_t> CompilerExpression(Node* node);
    std::string GenerateCode(const std::map<std::string, int32_t>& outputs);


    // 状态
    std::vector<CodeChunk> chunks_;
    std::map<uint64_t, int32_t> hash_to_chunk_;
    std::map<std::string,std::vector<int32_t>> node_cache_;
    int32_t next_symbol_index_ = 0;
    Graph* current_graph_ = nullptr;
    std::string error_message_;
};