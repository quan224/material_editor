#pragma once
#include <memory>
#include <vector>
#include <cstdint>

// 内存栈(对照UE FMemStackBase，Misc/MemStack.h)
class MemStack{
public:
    struct Mark{char* pos;};
    
    MemStack()=default;

};