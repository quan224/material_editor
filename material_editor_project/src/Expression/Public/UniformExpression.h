#pragma once
#include "Core/Public/RefCounted.h"
#include "Core/Public/MathTypes.h"
#include "MaterialTypes/Public/MiscDefines.h"
#include "MaterialTypes/Public/ValueType.h"
#include "Shader/Public/MaterialShader.h"
#include <cmath>
#include <map>
#include <string>
#include <vector>



// 类型收集
class FMaterialUniformExpressionType{
public:
    inline static std::map<std::string, FMaterialUniformExpressionType*>& GetTypeMap(){
        static std::map<std::string, FMaterialUniformExpressionType*> type_map;
        return type_map;
    }

    FMaterialUniformExpressionType(const std::string& n):name(n)
    {
        GetTypeMap()[n] = this;
    }

private:
    std::string name;
};

// 重写
#define DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(Name) \
    public:\
    static FMaterialUniformExpressionType static_type; \
    virtual FMaterialUniformExpressionType* GetType()const{return &static_type;}

// 注册（inline：子类全在头文件，多翻译单元合并定义）
#define IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(Name) \
    inline FMaterialUniformExpressionType Name::static_type(#Name);

class FMaterialUniformExpression{
public:
    virtual ~FMaterialUniformExpression()=default;
    virtual FMaterialUniformExpressionType* GetType()const =0;
    // virtual FMaterialUniformExpressionTexture*  GetTextureUniformExpresion(){return nullptr;}
    // virtual FMaterialUniformExpressionExternalTexture* GetExternalTextureUniformExpression(){return nullptr;}
    // virtual FMaterialUniformExpressionTextureCollection* GetTextureCollectionUniformExpression(){return nullptr;}
    
    virtual bool IsConstant()const{return false;}
    virtual bool IsIdentical(const FMaterialUniformExpression* other)const{return false;}

    // virtual void WriteNumberOpcodes(PreshaderData& out_data)const;
    virtual void GetNumbervalue(const MaterialRenderContext& context, Vec4& out_value)const = 0;

    virtual std::vector<const FMaterialUniformExpression* > GetChildren() const {  return std::vector<const FMaterialUniformExpression*>(); }

    // 课20好像才有用
    int32_t uniform_offset = INDEX_NONE;
    int32_t uniform_index = INDEX_NONE;
    EShaderFrequencyMask shader_frequency_mask = 0;

};

class FMaterialUniformExpressionConstant:public FMaterialUniformExpression {
    DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionConstant);
public:
    FMaterialUniformExpressionConstant() {}
    FMaterialUniformExpressionConstant(const Vec4& in_value, uint8_t in_value_type):value(in_value), value_type(in_value_type){}
    virtual bool IsConstant()const { return true; }
    virtual bool IsIdentical(const FMaterialUniformExpression* other)const {
        if (GetType() != other->GetType()) {
            return false;
        }
        const FMaterialUniformExpressionConstant* other_constant = (const FMaterialUniformExpressionConstant*)other;
        return value == other_constant->value && value_type == other_constant->value_type;
    }
    virtual void GetNumbervalue(const MaterialRenderContext& context, Vec4& out_value)const {
        out_value = value;
    }

private:
    Vec4 value;
    uint8_t value_type;
};
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionConstant);

enum EFoldedMathOperation
{
    FMO_Add,
    FMO_Sub,
    FMO_Mul,
    FMO_Div,
    FMO_Dot,
    FMO_Cross
};

class FMaterialUniformExpressionFoldedMath : public FMaterialUniformExpression {
    DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionFoldedMath);
public:
    FMaterialUniformExpressionFoldedMath():value_type(MCT_Float){}
    FMaterialUniformExpressionFoldedMath(const Ref<FMaterialUniformExpression>& in_a, const Ref<FMaterialUniformExpression>& in_b, uint8_t in_op, uint32_t in_value_type = MCT_Float):A(in_a), B(in_b), op(in_op), value_type(in_value_type){}
    virtual bool IsConstant()const {
        return A->IsConstant() && B->IsConstant();
    }
    virtual bool IsIdentical(const FMaterialUniformExpression* other)const {
        if (GetType() != other->GetType()) {
            return false;
        }
        const FMaterialUniformExpressionFoldedMath* other_math = (const FMaterialUniformExpressionFoldedMath*)other;
        return A->IsIdentical(other_math->A.get()) && B->IsIdentical(other_math->B.get()) && value_type == other_math->value_type && op == other_math->op;
    }
    virtual void GetNumbervalue(const MaterialRenderContext& context, Vec4& out_value)const {
        static_assert(sizeof(Vec4) == 4 * sizeof(float), "Vec4 must be tightly packed");
        Vec4 a, b;
        A->GetNumbervalue(context, a);
        B->GetNumbervalue(context, b);
        switch (op) {
        case FMO_Add:out_value = a + b; break;
        case FMO_Sub:out_value = a - b; break;
        case FMO_Mul:out_value = a * b; break;
        case FMO_Div:out_value = a / b; break;
        case FMO_Dot: {
            const int32_t n = GetComponentCount((EMaterialValueType)value_type);
            float d = 0.0f;
            for (int32_t i = 0; i < n; i++)d += (&a.x)[i] * (&b.x)[i];
            out_value = Vec4(d, 0.f, 0.f, 0.f);
            break;
        }
        case FMO_Cross:
            out_value = Vec4(
                a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x,
                0.f);
            break;
        }
    }

    virtual std::vector<const FMaterialUniformExpression* > GetChildren() const {
        return { A.get(), B.get() };
    }


private:
    Ref<FMaterialUniformExpression> A;
    Ref<FMaterialUniformExpression> B;
    uint32_t value_type;
    uint8_t op;
};
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionFoldedMath);

class FMaterialUniformExpressionRcp :public FMaterialUniformExpression {
    DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionRcp);
public:
    FMaterialUniformExpressionRcp(){}
    FMaterialUniformExpressionRcp(const Ref<FMaterialUniformExpression>& in_x ):X(in_x){}
    virtual bool IsConstant()const {
        return X->IsConstant();
    }
    virtual bool IsIdentical(const FMaterialUniformExpression* other)const {
        if (GetType() != other->GetType()) {
            return false;
        }
        const FMaterialUniformExpressionRcp* other_rcp = (const FMaterialUniformExpressionRcp*)other;
        return X->IsIdentical(other_rcp->X.get());
    }
    virtual void GetNumbervalue(const MaterialRenderContext& context, Vec4& out_value)const {
        X->GetNumbervalue(context, out_value);
        out_value = Vec4(1 / out_value.x, 1 / out_value.y, 1 / out_value.z, 1 / out_value.w);
    }
    virtual std::vector<const FMaterialUniformExpression* > GetChildren() const {
        return { X.get()};
    }

private:
    Ref<FMaterialUniformExpression> X;
};
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionRcp);

class FMaterialUniformExpressionAbs :public FMaterialUniformExpression {
    DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionAbs);
public:
    FMaterialUniformExpressionAbs() {}
    FMaterialUniformExpressionAbs(const Ref<FMaterialUniformExpression>& in_x) :X(in_x) {}
    virtual bool IsConstant()const {
        return X->IsConstant();
    }
    virtual bool IsIdentical(const FMaterialUniformExpression* other)const {
        if (GetType() != other->GetType()) {
            return false;
        }
        const FMaterialUniformExpressionAbs* other_rcp = (const FMaterialUniformExpressionAbs*)other;
        return X->IsIdentical(other_rcp->X.get());
    }
    virtual void GetNumbervalue(const MaterialRenderContext& context, Vec4& out_value)const {
        X->GetNumbervalue(context, out_value);
        out_value = Vec4(std::abs(out_value.x) , std::abs(out_value.y), std::abs(out_value.z), std::abs(out_value.w));
    }
    virtual std::vector<const FMaterialUniformExpression* > GetChildren() const {
        return { X.get() };
    }

private:
    Ref<FMaterialUniformExpression> X;
};
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionAbs);

class FMaterialUniformExpressionSine :public FMaterialUniformExpression {
    DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionSine);
public:
    FMaterialUniformExpressionSine(){}
    FMaterialUniformExpressionSine(const Ref<FMaterialUniformExpression>& in_x, bool in_is_cosine):X(in_x), is_cosine(in_is_cosine){}
    virtual bool IsConstant()const {
        return X->IsConstant();
    }
    virtual bool IsIdentical(const FMaterialUniformExpression* other)const {
        if (GetType() != other->GetType()) {
            return false;
        }
        const FMaterialUniformExpressionSine* other_sine = (const FMaterialUniformExpressionSine*)other;
        return X->IsIdentical(other_sine->X.get()) && is_cosine == other_sine->is_cosine;
    }
    virtual void GetNumbervalue(const MaterialRenderContext& context, Vec4& out_value)const {
        X->GetNumbervalue(context, out_value);
        if (is_cosine) {
            out_value = Vec4(std::cos(out_value.x), std::cos(out_value.y), std::cos(out_value.z), std::cos(out_value.w));
        }
        else {
            out_value = Vec4(std::sin(out_value.x), std::sin(out_value.y), std::sin(out_value.z), std::sin(out_value.w));
        }
        
    }
    virtual std::vector<const FMaterialUniformExpression* > GetChildren() const {
        return { X.get() };
    }

private:
    Ref<FMaterialUniformExpression> X;
    bool is_cosine;
};
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionSine);

class FMaterialUniformExpressionNeg :public FMaterialUniformExpression {
    DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionNeg);
public:
    FMaterialUniformExpressionNeg() {}
    FMaterialUniformExpressionNeg(const Ref<FMaterialUniformExpression>& in_x) :X(in_x) {}
    virtual bool IsConstant()const {
        return X->IsConstant();
    }
    virtual bool IsIdentical(const FMaterialUniformExpression* other)const {
        if (GetType() != other->GetType()) {
            return false;
        }
        const FMaterialUniformExpressionNeg* other_rcp = (const FMaterialUniformExpressionNeg*)other;
        return X->IsIdentical(other_rcp->X.get());
    }
    virtual void GetNumbervalue(const MaterialRenderContext& context, Vec4& out_value)const {
        X->GetNumbervalue(context, out_value);
        out_value = -out_value;

    }
    virtual std::vector<const FMaterialUniformExpression* > GetChildren() const {
        return { X.get() };
    }

private:
    Ref<FMaterialUniformExpression> X;
};
IMPLEMENT_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionNeg);
