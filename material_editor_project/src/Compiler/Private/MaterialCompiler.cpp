#include "Compiler/Public/MaterialCompiler.h"

// === Add:三段判定的模板 === 
int32_t MaterialCompiler::Add(int32_t a, int32_t b){
    if(a<0 || b<0){
        ME_LOG_ERROR("MaterialCompiler::Add错误, a索引:%d, b索引:%d", a, b);
        assert(false);
        return -1;
    }
    EValueType result_type = TypeSystem::GetArithmeticResultType(GetType(a), GetType(b));
    if(IsConstant(a)||IsConstant(b)){
        auto folded = ConstFolding::FoldBinary("+", GetConstantValue(a), GetConstantValue(b));
        if (folded){
            return AddConstantChunk(result_type, *folded);
        }
        else{
            ME_LOG_ERROR("MaterialCompiler::Add折叠失败, a索引:%d, b索引:%d", a, b);
            assert(false);
            return -1;
        }
    }
    return AddInlineCodeChunk(result_type, GetParameterCode(a)+ "+" + GetParameterCode(b));
}





// 生成局部变量名 Local0,Local1...
std::string MaterialCompiler::MakeSymbolName(){
    return "Local" + std::to_string(next_symbol_index_++);
}

// 通用代码块: 产生真实指令的表达式用（函数调用、纹理采样、复杂算数）
// 非内嵌时产生 “float3 Local0 = ...;” 声明；内嵌时不生命，直接嵌入使用处

int32_t MaterialCompiler::AddCodeChunk(EValueType type, const std::string& code, bool is_inline){
    uint64_t hash = HashString(code);
    auto it = hash_to_chunk_.find(hash);
    if(it != hash_to_chunk_.end()) return it->second;

    CodeChunk chunk;
    chunk.hash = hash;
    chunk.code = code;
    chunk.type = type;                                    // ← 原来漏了，导致 chunk 类型全是 Unknown
    chunk.is_inline = is_inline;
    if (!is_inline) chunk.symbol_name = MakeSymbolName(); // ← 非内联才需符号（声明 LocalN）；内联直接嵌入
    chunks_.emplace_back(chunk);
    int32_t index = chunks_.size() - 1;
    hash_to_chunk_[hash] = index;
    return index;
}

int32_t MaterialCompiler::AddInlineCodeChunk(EValueType type, const std::string& code){
    return AddCodeChunk(type, code, /*is_inline*/true);
}

int32_t MaterialCompiler::AddConstantChunk(EValueType type, const ConstValue& value){
    std::string const_code = FormatConstantCode(value);
    
    uint64_t hash = HashString("const_" + const_code);
    auto it = hash_to_chunk_.find(hash);
    if (it != hash_to_chunk_.end()) return it->second;

    CodeChunk chunk;
    chunk.hash = hash;
    chunk.code = const_code;
    chunk.type = type;
    chunk.is_inline = true;
    chunk.is_constant = true;
    chunk.constant_value = value;
    chunks_.emplace_back(chunk);
    int32_t index = chunks_.size() - 1;
    hash_to_chunk_[hash] = index;
    return index;
}


std::string MaterialCompiler::FormatConstantCode(const ConstValue& value){
    if (std::holds_alternative<float>(value)){
        if (ConstFolding::IsScalarZero(value)) return "0.0";
        else if (ConstFolding::IsScalarOne(value)) return "1.0";
        else return std::to_string(std::get<float>(value)); 
    }
    else if (std::holds_alternative<Vec2>(value)){
        Vec2 _v = std::get<Vec2>(value);
        return "float2(" + std::to_string(_v.x) + "," + std::to_string(_v.y) + ")";
    }
    else if (std::holds_alternative<Vec3>(value)){
        Vec3 _v = std::get<Vec3>(value);
        return "float3(" + std::to_string(_v.x) + "," + std::to_string(_v.y) + "," + std::to_string(_v.z) + ")";
    }
    else if (std::holds_alternative<Vec4>(value)){
        Vec4 _v = std::get<Vec4>(value);
        return "float4(" + std::to_string(_v.x) + "," + std::to_string(_v.y) + "," + std::to_string(_v.z) + "," + std::to_string(_v.w) + ")";
    }
    else{
        ME_LOG_ERROR("FormatConstantCode unvalid");
        assert(false);
        return "0.0";
    }
}

std::string MaterialCompiler::GetParameterCode(int32_t index) const {
    if (index < 0 || index >= chunks_.size()) {
        ME_LOG_ERROR("GetParameterCode错误,index超出限制 %d", index);
        assert(false);
        return "0.0";
    }
    return chunks_[index].is_inline ? chunks_[index].code : chunks_[index].symbol_name;
}
EValueType MaterialCompiler::GetType(int32_t index)const {
    if (index < 0 || index >= chunks_.size()) {
        ME_LOG_ERROR("GetType错误,index超出限制 %d", index);
        assert(false);
        return EValueType::Unknown;
    }
    return chunks_[index].type;
}
bool MaterialCompiler::IsConstant(int32_t index) const {
    if (index < 0 || index >= chunks_.size()) {
        ME_LOG_ERROR("IsConstant错误,index超出限制 %d", index);
        assert(false);
        return false;
    }
    return chunks_[index].is_constant;
}
ConstValue MaterialCompiler::GetConstantValue(int32_t index) const {
    if (index < 0 || index >= chunks_.size()) {
        ME_LOG_ERROR("GetConstantValue错误,index超出限制 %d", index);
        assert(false);
        return 0.0f;
    }
    if (!chunks_[index].is_constant) {
        ME_LOG_ERROR("GetConstantValue错误,index并非常量 %d", index);
        assert(false);
        return 0.0f;
    }
    return chunks_[index].constant_value;
}



void MaterialCompiler::EmitError(const std::string& error_message,
    EErrorSeverity severity,
    const Node* override_node,
    const std::string& override_pin_name
){
    CompilerError error;
    error.error_message = error_message;
    error.severity = severity;

    error.node_id = override_node ? override_node->id : (current_node_ ? current_node_->id : UUID::Invalid());
    error.pin_name = override_pin_name.empty() ? current_pin_name_ : override_pin_name;

    for (const auto& m : errors_) {
        if (m.SameAs(error)) return;
    }
    errors_.emplace_back(error);
}




