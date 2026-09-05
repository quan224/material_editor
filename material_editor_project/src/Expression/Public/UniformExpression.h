#pragma once
#include "Core/Public/RefCounted.h"
#include "Core/Public/MathTypes.h"
#include "MaterialTypes/Public/MiscDefines.h"
#include "Shader/Public/MaterialShader.h"
#include <map>
#include <string>
#include <vector>

// 类型收集
class MaterialUniformExpressionType{
public:
    inline static std::map<std::string, MaterialUniformExpressionType*>& GetTypeMap(){
        static std::map<std::string, MaterialUniformExpressionType*> type_map;
        return type_map;
    }

    MaterialUniformExpressionType(const std::string& n):name(n)
    {
        GetTypeMap()[n] = this;
    }

private:
    std::string name;
};

// 重写
#define DECLARE_MATERIALUNIFROMEXPRESSION_TYPE(Name) \
    public:\
    static MaterialUniformExpressionType static_type; \
    virtual MaterialUniformExpressionType* GetType(){return &static_type;}

// 注册
#define IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(Name) \
    MaterialUniformExpressionType Name::static_type(#Name);

class MaterialUniformExpression{
public:
    virtual ~MaterialUniformExpression()=default;
    virtual MaterialUniformExpressionType* GetType()const =0;
    // virtual MaterialUniformExpressionTexture*  GetTextureUniformExpresion(){return nullptr;}
    // virtual MaterialUniformExpressionExternalTexture* GetExternalTextureUniformExpression(){return nullptr;}
    // virtual MaterialUniformExpressionTextureCollection* GetTextureCollectionUniformExpression(){return nullptr;}
    
    virtual bool IsConstant()const{return false;}
    virtual bool IsIdentical(MaterialUniformExpression* other)const{return false;}

    // virtual void WriteNumberOpcodes(PreshaderData& out_data)const;
    virtual void GetNumbervalue(const MaterialRenderContext& context, Vec4& out_value)const;

    virtual std::vector<const MaterialUniformExpression* > GetChildren() const {  return std::vector<const MaterialUniformExpression*>(); }

    int32_t uniform_offset = INDEX_NONE;
    int32_t uniform_index = INDEX_NONE;

    EShaderFrequencyMask shader_frequency_mask = 0;

};
