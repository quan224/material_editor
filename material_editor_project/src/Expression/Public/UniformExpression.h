#pragma once
#include "Core/Public/RefCounted.h"
#include <map>
#include <string>
#include <vector>


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

#define DECLARE_MATERIALUNIFROMEXPRESSION_TYPE(Name) \
    public:\
    static MaterialUniformExpressionType static_type;

class MaterialUniformExpression{
public:
    virtual ~MaterialUniformExpression()=default;
    virtual MaterialUniformExpressionType* GetType()const =0;
    virtual bool IsConstant()const{return false;}
    virtual bool IsIdentical(MaterialUniformExpression* other)const{return false;}

    virtual std::vector<const MaterialUniformExpression* > GetChildren() const {  return std::vector<const MaterialUniformExpression*>(); }

};
