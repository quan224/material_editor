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

EValueType MakeNoneLWCType(EValueType t){
    FValueTypeDescription type_desc = GetValueTypeDescription(t);
    if (type_desc.comp_type == EValueComponentType::Double){
        return MakeValueType( MakeNoneLWCType(type_desc.comp_type), type_desc.num_components);
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



// ===================↑ FType===================
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


// ===================↑ FStructType===================
const FStructField* FStructType::FindFieldByName(const char* in_name) const{
    for(const auto& f:fields){
        if (f.name == in_name) return &f;
    }
    return nullptr;
}

// ===================↑ FStructTypeRegistry===================

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


}


