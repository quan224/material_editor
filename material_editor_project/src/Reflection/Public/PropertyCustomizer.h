#pragma once
#include "Reflection/Public/Property.h"
#include <functional>
#include <map>
#include <typeindex>

class QWidget;

class PropertyCustomizerRegistry{
public:
    using EditorCreationFn = std::function<QWidget*(Property*)>;

    // 注册某种property 类型对应的编辑器创建器
    template<typename PropType>
    void Register(EditorCreationFn creator){
        creators[ std::type_index(typeid(PropType))] = std::move(creator);
    }

    // 为某个Property创建对应的编辑器
    QWidget* CreateEditor(Property* prop)const {
        auto it = creators.find( std::type_index(typeid(*prop)));
        if (it != creators.end()){
            return it->second(prop);
        } 
        return nullptr;
    }

private:
    std::map<std::type_index, EditorCreationFn> creators;
};