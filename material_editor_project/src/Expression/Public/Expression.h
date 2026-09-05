#pragma once
#include "MaterialTypes/Public/ValueType.h"
#include "Reflection/Public/ClassDesc.h"
#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

// 前向声明
class MaterialCompiler;
class Node;  // 双参数 Compile 用到

// 引脚描述（Expression 用来声明自己的引脚布局）
struct ExpressionPinDesc
{
    std::string name;
    EValueType type;
    nlohmann::json default_value;
};

class Expression
{
public:
    virtual ~Expression() = default;

    // === 反射入口（纯虚：子类必须通过宏重写）===
    // 返回的 ClassDesc 包含：typeName / displayName / category / categoryColor / fields[]
    virtual const ClassDesc *GetClassDesc() const = 0;

    // === 引脚布局（结构化声明，与"可编辑参数"分开）===
    virtual std::vector<ExpressionPinDesc> GetInputPins() const = 0;
    virtual std::vector<ExpressionPinDesc> GetOutputPins() const = 0;

    // === 编译核心方法 — 返回每个输出引脚对应的代码块索引 ===
    // compiler 参数提供编译 API（Add, Mul 等）
    // ownerNode 是当前节点，表达式通过 compiler->CompileInputPin(ownerNode, "A")
    // 递归编译上游（比把 ownerNode_ 存成成员变量更安全：避免状态泄漏 / 多线程问题）
    virtual std::vector<int32_t> Compile(MaterialCompiler *compiler, Node *ownerNode) const = 0;

    // 参数读写，反射实现，子类无需重写
    // 获取或有参数描述
    std::vector<const Property*> GetParameters() const;
    // 写入参数值到类对象中
    void SetParameter(const std::string& name, const nlohmann::json& value);
    // 从类对象中恢复参数值为json
    nlohmann::json GetParameter(const std::string& name)const;

};
