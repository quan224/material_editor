#include "Shader/Public/ShaderTypes.h"

namespace shader{

const FValueTypeDescription& GetValueTypeDescription(EValueType t){
    return GValueTypeDescriptions[(int32_t)t];
}

EValueType MakeValueType(EValueComponentType c_t, int8_t nums){
    if (c_t == EValueComponentType::Void || nums == 0){
        return EValueType::Void;
    }
    switch(c_t){
    case EValueComponentType::Float:
        switch(nums){
            case 1: return EValueType::Float1;
            case 2: return EValueType::Float2;
            case 3: return EValueType::Float3;
            case 4: return EValueType::Float4;
            case 16: return EValueType::Float4x4;
            default: break;
        } 
        break;
    case EValueComponentType::Double:
        switch(nums){
            case 1: return EValueType::Double1;
            case 2: return EValueType::Double2;
            case 3: return EValueType::Double3;
            case 4: return EValueType::Double4;
            case 16: return EValueType::Double4x4;
            default: break;
        } 
        break;
    case EValueComponentType::Numeric:
        switch(nums){
            case 1: return EValueType::Numeric1;
            case 2: return EValueType::Numeric2;
            case 3: return EValueType::Numeric3;
            case 4: return EValueType::Numeric4;
            case 16: return EValueType::Numeric4x4;
            default: break;
        } 
        break;
    case EValueComponentType::Int:
        switch(nums){
            case 1: return EValueType::Int1;
            case 2: return EValueType::Int2;
            case 3: return EValueType::Int3;
            case 4: return EValueType::Int4;
            case 16: return EValueType::Float4x4;
            default: break;
        } 
        break;
    case EValueComponentType::Bool:
        switch(nums){
            case 1: return EValueType::Bool1;
            case 2: return EValueType::Bool2;
            case 3: return EValueType::Bool3;
            case 4: return EValueType::Bool4;
            case 16: return EValueType::Float4x4;
            default: break;
        } 
        break;
    }


    return EValueType::Void;
}

EValueType MakeValueType(EValueType t, int8_t nums){
    return MakeValueType(GetValueTypeDescription(t).comp_type, nums); 
}

EValueType MakeDerivativeType(EValueType t){
    const FValueTypeDescription& type_desc = GetValueTypeDescription(t);
    if (IsNumericType(type_desc.comp_type)){
        return MakeValueType(EValueComponentType::Float, type_desc.num_components);
    }
    return EValueType::Void;

}

EValueType MakeNonLWCType(EValueType t){
    FValueTypeDescription type_desc = GetValueTypeDescription(t);
    if (type_desc.comp_type == EValueComponentType::Double){
        return MakeValueType( MakeNonLWCType(type_desc.comp_type), type_desc.num_components);
    }
    return t;
}

EValueType MakeConcreteType(EValueType t){
    FValueTypeDescription type_desc = GetValueTypeDescription(t);
    if (type_desc.comp_type == EValueComponentType::Numeric){
        return MakeValueType( MakeConcreteType(type_desc.comp_type), type_desc.num_components);
    }
    return t; 
}

EValueComponentType CombineComponentTypes(EValueComponentType l, EValueComponentType r){
    if(l == r){
        return l;
    }
    else if(l==EValueComponentType::Void){
        return r;
    }
    else if(r==EValueComponentType::Void){
        return l;
    }
    else if(l==EValueComponentType::Numeric&&r==EValueComponentType::Numeric){
        return EValueComponentType::Numeric;
    }
    // 有double用double
    else if(l==EValueComponentType::Double||r==EValueComponentType::Double){
        return EValueComponentType::Double;
    }
    // 有float用float
    else if(l==EValueComponentType::Float||r==EValueComponentType::Float){
        return EValueComponentType::Float;
    }
    // 实在不行用int
    else if(IsNumericType(l)&&IsNumericType(r)){
        return EValueComponentType::Int;
    }
    else{
        return EValueComponentType::Void;
    }
}

FType CombineTypes(const FType& l, const FType& r, bool b_merge_matrix_types){
    if(l.IsVoid() || l.IsAny()){
        return r;
    }
    if(r.IsVoid() || r.IsAny()){
        return l;
    }
    if((l.IsNumericVector()&&r.IsNumericVector())||(b_merge_matrix_types&&l.IsNumericMatrix()&&r.IsNumericMatrix())){
        FValueTypeDescription l_desc = GetValueTypeDescription(l);
        FValueTypeDescription r_desc = GetValueTypeDescription(r);
        const EValueComponentType c_type = CombineComponentTypes(l_desc.comp_type, r_desc.comp_type);
        if(c_type==EValueComponentType::Void){
            return EValueType::Void;
        }
        const int8_t counts = std::max(l_desc.num_components, r_desc.num_components);
        return MakeValueType(c_type, counts);
    }
    if(l==r){
        return r;
    }
    return EValueType::Void;

}

namespace Private{

void SetFieldType(EValueType* field_types, EValueComponentType* component_types, int32_t field_index, int32_t component_index, const FType& f_type){
    if(f_type.IsStruct()){
        for (const FStructField& field: f_type.struct_type->fields){
            SetFieldType(field_types, component_types, field_index+field.flat_field_index, component_index+field.component_index, field.type);
        }
    }
    else{
        field_types[field_index] = f_type.value_type;
        const FValueTypeDescription& type_desc = GetValueTypeDescription(f_type.value_type);
        for(int32_t i=0;i<type_desc.num_components; ++i){
            component_types[component_index+i] = type_desc.comp_type;
        }
    }
}

}



// ===================↑ 都是工具函数===================



// ===================↓ FType===================
const char* FType::GetName() const{
    if(IsStruct()){
        return struct_type->name;
    }
    FValueTypeDescription description = GetValueTypeDescription(value_type);
    return description.name;
}

// derivative 求导
FType FType::GetDerivativeType() const{
    if (IsStruct()){
        return struct_type->derivative_type;
    }
    else if(IsObject()){
        return *this;
    }

    return MakeDerivativeType(value_type);
}

int32_t FType::GetNumComponents() const{
    if(IsStruct()){
        return struct_type->component_types.size();
    }
    if(IsObject()){
        return 1;
    }
    return GetValueTypeDescription(value_type).num_components;
}

int32_t FType::GetNumFlatFields() const{
    if(IsStruct()){
        return struct_type->flat_field_types.size();
    }
    return 1;
}

EValueComponentType FType::GetComponentType(int32_t index) const{
    if (index<0){
        return EValueComponentType::Void;
    }

    if(IsStruct()){
        if(index<struct_type->component_types.size()){
            return struct_type->component_types[index];
        }
    }
    else if(IsNumeric()){
        FValueTypeDescription type_desc = GetValueTypeDescription(value_type);
        // 标量的特权， 标量在获取其2，3，4号位的类型时依旧返回自己类型，防止类型广播时出错
        if((type_desc.num_components == 1 && index<=3 )|| index<type_desc.num_components){
            return type_desc.comp_type;
        }
    }
    return EValueComponentType::Void;

}

EValueType FType::GetFlatFieldType(int32_t index) const{
    if(index<0){
        return EValueType::Void;
    }

    if(IsStruct()){
        return index<struct_type->flat_field_types.size()? struct_type->flat_field_types[index]:EValueType::Void;
    }
    return index==0 ? value_type:EValueType::Void;
}


// ===================↓ FStructType===================
const FStructField* FStructType::FindFieldByName(const char* in_name) const{
    for(const auto& f:fields){
        if (std::strcmp(f.name, in_name)==0) return &f;
    }
    return nullptr;
}

// ===================↓ FStructTypeRegistry===================

void FStructTypeRegistry::EmitDeclarationsCode(FStringBuilderBase& out_code) const{
    for(const auto& it:types){
        const FStructType* struct_type = it.second;
        if(!struct_type->IsExternal()){
            out_code.Appendf("struct %s\n", struct_type->name);
            out_code.Appendf("{\n");
            for (const FStructField& struct_field:struct_type->fields){
                out_code.Appendf("\t%s %s;\n", struct_field.type.GetName(), struct_field.name);
            }
            out_code.Appendf("}\n");
            for (const FStructField& struct_field:struct_type->fields){
                out_code.Appendf("%s %s_Set%s(%s self, %s value) {self.%s = value; return self;}",
                struct_type->name, struct_type->name, struct_field.name, struct_type->name, struct_field.type.GetName(), struct_field.name);
            }
            out_code.Appendf("\n");

        }
    }
}

const FStructType* FStructTypeRegistry::NewType(const FStructTypeInitializer& initializer){
    std::vector<FStructFieldInitializer> derivate_fields;
    const int32_t num_fields = initializer.fields.size();
    std::vector<FStructField> fields;
    fields.reserve(num_fields);
    int32_t component_index = 0;
    int32_t flat_field_index = 0;
    uint64_t hash = 0u;
    {
        hash = HashString(initializer.name);

        for (const FStructFieldInitializer& ini_field:initializer.fields){
            hash = HashCombine(hash, HashString(ini_field.name));
            if(ini_field.type.IsStruct()){
                hash = HashCombine(hash, ini_field.type.struct_type->hash);
            }
            else{
                hash = HashCombine(hash, (uint64_t)ini_field.type.value_type);
            }
            FStructField field;
            field.name = allocator->AllocateString(ini_field.name);
            field.type = ini_field.type;
            field.component_index = component_index;
            field.flat_field_index = flat_field_index;
            component_index += field.type.GetNumComponents();
            flat_field_index += field.type.GetNumFlatFields();
            if(!initializer.b_is_derivative_type){
                const FType field_derivative_type = field.type.GetDerivativeType();
                if(!field_derivative_type.IsVoid()){
                    derivate_fields.push_back({field.name, field_derivative_type});
                }
            }
            fields.push_back(field);
        }
    }
    const auto it = types.find(hash);
    if(it != types.end()){
        return it->second;
    }
    std::vector<EValueComponentType> component_types(component_index);
    std::vector<EValueType> flat_field_types(flat_field_index);
    for(int32_t field_index=0; field_index<num_fields; ++field_index){
        const FStructField& field = fields[field_index];
        Private::SetFieldType(flat_field_types.data(), component_types.data(), field.flat_field_index, field.component_index, field.type);
    }
    FStructType* struct_type = new(allocator->Alloc(sizeof(FStructType))) FStructType();
    struct_type->name = allocator->AllocateString(initializer.name);
    struct_type->hash = hash;
    struct_type->fields = std::move(fields);
    struct_type->component_types = std::move(component_types);
    struct_type->flat_field_types = std::move(flat_field_types);

    types[hash] = struct_type;

    // 建导数结构（防递归：b_is_derivative_type=true）
    if(!initializer.b_is_derivative_type && !derivate_fields.empty()){
        FStructTypeInitializer derivative_initializer;
        derivative_initializer.name = initializer.name + "_Derivative";
        derivative_initializer.fields = derivate_fields;
        derivative_initializer.b_is_derivative_type = true;
        struct_type->derivative_type = NewType(derivative_initializer);
    }

    return struct_type;
}

const FStructType* FStructTypeRegistry::NewExternalType(std::string name){
    uint64_t hash = HashString(name);
    FStructType* struct_type = new(allocator->Alloc(sizeof(FStructType))) FStructType();
    struct_type->name = allocator->AllocateString(name);
    struct_type->hash = hash;
    types[hash] = struct_type;
    return struct_type;
}

const FStructType* FStructTypeRegistry::FindType(uint64_t hash) const{
    const auto& it = types.find(hash);
    return it != types.end() ? it->second : nullptr;
}



// ===================↓ FValue===================

namespace Private {
struct FCastFloat {
    using FComponentType = float;
    inline float operator()(EValueComponentType t, const FValueComponent& component)const {
        switch (t) {
        case EValueComponentType::Float: return component._float;
        case EValueComponentType::Double: return (float)component._double;
        case EValueComponentType::Int: return (float)component._int;
        case EValueComponentType::Bool: return (float)component._bool;
        default:return 0.0f;
        }
    }
};

struct FCastDouble {
    using FComponentType = double;
    inline double operator()(EValueComponentType t, const FValueComponent& component)const {
        switch (t) {
        case EValueComponentType::Float: return (double)component._float;
        case EValueComponentType::Double: return component._double;
        case EValueComponentType::Int: return (double)component._int;
        case EValueComponentType::Bool: return (double)component._bool;
        default:return 0.0f;
        }
    }
};

struct FCastInt {
    using FComponentType = int32_t;
    inline int32_t operator()(EValueComponentType t, const FValueComponent& component)const {
        switch (t) {
        case EValueComponentType::Float: return (int32_t)component._float;
        case EValueComponentType::Double: return (int32_t)component._double;
        case EValueComponentType::Int: return component._int;
        case EValueComponentType::Bool: return component._bool ? 1 : 0;
        default:return 0;
        }
    }
};

struct FCastBool {
    using FComponentType = bool;
    inline bool operator()(EValueComponentType t, const FValueComponent& component)const {
        switch (t) {
        case EValueComponentType::Float: return component._float != 0.0f;
        case EValueComponentType::Double: return component._double != 0.0;
        case EValueComponentType::Int: return component._int != 0;
        case EValueComponentType::Bool: return component.AsBool();
        default:return false;
        }
    }
};

template<typename Operator, typename ResultType>
void AsType(const Operator& op, const FValue& value, ResultType& out_result) {
    using FComponentType = typename Operator::FComponentType;
    const FValueTypeDescription& type_desc = GetValueTypeDescription(value.type_);
    if (type_desc.num_components == 1) {
        const FComponentType component = op(type_desc.comp_type, value.component[0]);
        for (int32_t i = 0; i < 4; i++) {
            out_result[i] = component;
        }
    }
    else {
        const int32_t num_components = std::min<int32_t>(type_desc.num_components, 4);
        for (int32_t i = 0; i < num_components; ++i) {
            out_result[i] = op(type_desc.comp_type, value.component[i]);
        }
        for (int32_t i = num_components; i < 4; ++i) {
            out_result[i] = (FComponentType)0;
        }
    }
}

template<typename Operation, typename ResultType>
void AsTypeInPlace(const Operation& op, EValueType Type, std::vector<FValueComponent> component, ResultType& out_result) {
    using FComponentType = typename Operation::FComponentType;
    const FValueTypeDescription& type_desc = GetValueTypeDescription(Type);
    if (type_desc.num_components == 1) {
        const FComponentType component_cast = op(type_desc.comp_type, component[0]);
        for (int32_t i = 0; i < 4; i++) {
            out_result[i] = component_cast;
        }
    }
    else {
        const int32_t num_components = std::min<int32_t>(component.size(), 4);
        for (int32_t i = 0; i < num_components; i++) {
            out_result[i] = op(type_desc.comp_type, component[i]);
        }
        for (int32_t i = num_components; i < 4; i++) {
            out_result[i] = (FComponentType)0;
        }
    }
}

template<typename Operation>
void Cast(const Operation& op, const FValue& value, FValue& out_result) {
    ME_CHECK(out_result.component.empty());
    using FComponentType = typename Operation::FComponentType;
    const FValueTypeDescription& value_type_desc = GetValueTypeDescription(value.type_);
    const FValueTypeDescription& result_type_desc = GetValueTypeDescription(out_result.type_);
    const int32_t num_copy_components = std::min(value_type_desc.num_components, result_type_desc.num_components);
    for (int32_t i = 0; i < num_copy_components; i++) {
        out_result.component.push_back(op(value_type_desc.comp_type, value.component[i]));
    }
    if (num_copy_components < result_type_desc.num_components) {
        if (num_copy_components == 1) {
            const FValueComponent component = out_result.component[0];
            for (int i = 1; i < result_type_desc.num_components; i++) {
                out_result.component.push_back(component);
            }
        }
        else {
            for (int32_t i = num_copy_components; i < result_type_desc.num_components; i++) {
                out_result.component.emplace_back();
            }
        }
    }
}

void FormatComponent_Double(double value, int32_t num_components, EValueStringFormat format, FStringBuilderBase& out_result) {
    if (format == EValueStringFormat::HLSL) {
        out_result.Appendf("%0.8f", value);
    } else {
        // Shorter format for more components
        switch (num_components) {
        default: out_result.Appendf("%.2g", value); break;
        case 3: out_result.Appendf("%.3g", value); break;
        case 2: out_result.Appendf("%.3g", value); break;
        case 1: out_result.Appendf("%.4g", value); break;
        }
    }
}


}// namespace Private



FValue FValue::FromMemoryImage(EValueType t, const void* data, uint32_t* out_size_in_bytes){
    ME_CHECK(IsNumericType(t));
    const FValueTypeDescription& type_desc = GetValueTypeDescription(t);
    FValue result(type_desc.comp_type, type_desc.num_components);
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    const uint32_t component_size_in_bytes = type_desc.component_size_in_bytes;
    if (component_size_in_bytes > 0u){
        for (int32_t i=0u;i<type_desc.num_components; i++){
            memcpy(&result.component[i].packed, bytes, component_size_in_bytes);
            bytes+=component_size_in_bytes;
        }
    }
    if(out_size_in_bytes){
        *out_size_in_bytes = (uint32_t)(bytes-static_cast<const uint8_t*>(data));
    }
    return result;
}

FMemoryImageValue FValue::AsMemoryImage() const{
    ME_CHECK(type_.IsNumeric());
    const FValueTypeDescription& type_desc = GetValueTypeDescription(type_);
    FMemoryImageValue result;
    uint8_t* bytes = result.bytes;
    const uint32_t component_size_in_bytes = type_desc.component_size_in_bytes;
    if (component_size_in_bytes > 0u){
        for(int32_t i=0u; i<type_desc.num_components;i++){
            memcpy(bytes, &component[i].packed, component_size_in_bytes);
            bytes += component_size_in_bytes;
        }
    }
    result.size = (uint32_t)(bytes-result.bytes);
    ME_CHECK(result.size<=FMemoryImageValue::max_size);
    return result;
}

FFloatValue FValue::AsFloat() const {
    FFloatValue result;
    Private::AsType(Private::FCastFloat(), *this, result);
    return result;
}

FDoubleValue FValue::AsDouble() const {
    FDoubleValue result;
    Private::AsType(Private::FCastDouble(), *this, result);
    return result;
}

FIntValue FValue::AsInt() const {
    FIntValue result;
    Private::AsType(Private::FCastInt(), *this, result);
    return result;
}

FBoolValue FValue::AsBool() const {
    FBoolValue result;
    Private::AsType(Private::FCastBool(), *this, result);
    return result;
}

DVec4 FValue::AsVector4d() const {
    FDoubleValue result = AsDouble();
    return DVec4(result[0], result[1], result[2], result[3]);
}

float FValue::AsFloatScalar() const {
    FFloatValue result;
    Private::AsType(Private::FCastFloat(), *this, result);
    return result[0];
}

bool FValue::AsBoolScalar() const {
    FBoolValue result = AsBool();
    for (int32_t i = 0; i < 4; i++) {
        if (result[i]) {
            return true;
        }
    }
    return false;

}

bool FValue::IsZero() const {
    bool is_zero = type_.IsNumeric();
    if (is_zero) {
        for (const FValueComponent& comp: component) {
            if (comp.packed) {
                is_zero = false;
                break;
            }
        }
    }
    return is_zero;
}

const char* FValueComponent::ToString(EValueComponentType type, FStringBuilderBase& out_string) const {
    switch (type) {
    case EValueComponentType::Int: out_string.Appendf("%d", _int); break;
    case EValueComponentType::Bool: out_string.Append(AsBool() ? "true" : "false"); break;
    case EValueComponentType::Float: out_string.Appendf("%#.9gf", _float); break;
    default: ME_CHECK(false); break; // TODO - double, Numeric
    }
    return out_string.GetData();
}

const char* FValue::ToString(EValueStringFormat format, FStringBuilderBase& out_string) const {
    const int32_t num_components = type_.GetNumComponents();
    const char* closing_suffix = nullptr;

    if (format == EValueStringFormat::HLSL) {
        if (type_.IsStruct()) {
            out_string.Append("{ ");
            closing_suffix = " }";
        } else {
            const FValueTypeDescription& type_desc = GetValueTypeDescription(type_.value_type);
            ME_CHECK(type_desc.comp_type != EValueComponentType::Numeric);
            if (type_desc.comp_type != EValueComponentType::Double) {
                out_string.Appendf("%s(", type_desc.name);
                closing_suffix = ")";
            }
        }
    }

    for (int32_t index = 0; index < num_components; ++index) {
        if (index > 0) {
            out_string.Append(", ");
        }
        const EValueComponentType component_type = type_.GetComponentType(index);
        switch (component_type) {
        case EValueComponentType::Int: out_string.Appendf("%d", component[index]._int); break;
        case EValueComponentType::Bool: out_string.Append(component[index]._bool ? "true" : "false"); break;
        case EValueComponentType::Float: Private::FormatComponent_Double((double)component[index]._float, num_components, format, out_string); break;
        case EValueComponentType::Double:
        case EValueComponentType::Numeric:
            Private::FormatComponent_Double(component[index]._double, num_components, format, out_string); break;
        default: ME_CHECK(false); break;
        }
    }

    if (closing_suffix) {
        out_string.Append(closing_suffix);
    }

    return out_string.GetData();
}


}


