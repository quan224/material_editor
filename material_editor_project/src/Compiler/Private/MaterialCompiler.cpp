#include "Compiler/Public/MaterialCompiler.h"
#include "Compiler/Public/TypeSystem.h"
#include "MaterialGraph/Public/Graph.h"
#include "MaterialGraph/Public/NodeFactory.h"
#include "Expression/Public/Expression.h"
#include "Core/Public/Hash.h"
#include "Core/Public/Logger.h"
#include <sstream>
#include <cstdarg>

// ===================== 主编译入口 =====================

MaterialCompiler::MaterialCompiler(){}


// ===================== 代码块管理 =====================

std::string MaterialCompiler::MakeSymbolName(){
    return "Local" + std::to_string(next_symbo_index_++);
}


int32_t MaterialCompiler::AddCodeChunk(EValueType type, const std::string& code, bool is_inline){
    uint64_t hash = HashString(code);
    auto it = hash_to_chunk_.find(hash);
    if(it != hash_to_chunk_.end()) return it->second;  // 去重

    CodeChunk chunk;
    chunk.hash = hash;
    chunk.code = code;
    chunk.type = type;
    chunk.is_inline = is_inline;
    if (! is_inline){
        chunk.symbol_name = MakeSymbolName();
    }

    chunks_.push_back(chunk);
    int32_t index = chunks_.size() - 1;
    hash_to_chunk_[hash] = index;
    return index;

}

int32_t MaterialCompiler::AddConstantChunk(EValueType type, float value)
{
    std::string code;
    if (value == 0.0f)
        code = "0.0";
    else if (value == 1.0f)
        code = "1.0";
    else
        code = std::to_string(value);
    uint64_t hash = HashString("const_" + code);
    auto it = hash_to_chunk_.find(hash);
    if (it != hash_to_chunk_.end())
        return it->second;
    CodeChunk chunk;
    chunk.hash = hash;
    chunk.code = code;
    chunk.type = type;
    chunk.is_inline = true;
    chunk.is_constant = true;
    chunk.constant_value = value;
    chunks_.push_back(chunk);
    int32_t index = chunks_.size() - 1;
    hash_to_chunk_[hash] = index;
    return index;
}

// ===================== 常量 =====================

int32_t MaterialCompiler::Constant(float value){
    return AddConstantChunk(EValueType::Float1, value);
}

int32_t MaterialCompiler::Constant2(float x, float y)
{
    return AddCodeChunk(EValueType::Float2, "float2(" + std::to_string(x) + "," + std::to_string(y) + ")", false);
}