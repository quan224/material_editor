#pragma once
#include <new>
#include <memory>
#include <vector>
#include <string>
#include <cstdint>

// 内存栈(对照UE FMemStackBase，Misc/MemStack.h)
class MemStack{
public:
    struct Mark{char* pos;};
    
    MemStack(const MemStack& s)=delete;
    MemStack& operator=(const MemStack&)=delete;

    // 构造即开首块（对照 UE：top_ 永不为 null，任何时机的 Mark 都安全）
    MemStack(){
        chunks_.push_back(std::make_unique<char[]>(CHUNK_));
        top_ = chunks_.back().get();
        end_ = top_+CHUNK_;
    }

    // 借n字节裸内存（单词分配永远连续，绝不跨快）
    void* Alloc(size_t n){
        if(top_+n>end_){
            size_t chunk_size = ChunkSizeFor(n);
            chunks_.push_back(std::make_unique<char[]>(chunk_size));
            top_ = chunks_.back().get();
            end_ = top_+chunk_size;
        }
        void* p = top_;
        top_+=n;
        return p;
    }

    const char* AllocateString(const std::string& s){
        char* p = (char*)Alloc(s.size()+1);
        memcpy(p, s.c_str(), s.size());
        p[s.size()] = '\0';
        return p;
    }

    template<typename T, typename... Args>
    T* New(Args... args){
        return new(Alloc(sizeof(T))) T(std::forward<Args>(args)...);
    }

    Mark GetMark(){return Mark{top_};}
    void Pop(Mark m){top_=m.pos;}
    size_t GetNumChunks() const {return chunks_.size();}


private:
    // 分档（按4KB翻倍，找寻最小但能放得下的块）
    static size_t ChunkSizeFor(size_t n){
        size_t chunk_size = CHUNK_;
        while(chunk_size<n) chunk_size*=2;
        return chunk_size;
    }


    static constexpr size_t CHUNK_ = 4*1024;  // 最小4KB块
    std::vector<std::unique_ptr<char []>> chunks_;  // 向系统借的整块清单
    char* top_=nullptr;  // 分配位置
    char* end_=nullptr;  // 分块末尾

};



//   #pragma once
//   #include <new>          // placement new
//   #include <memory>
//   #include <vector>
//   #include <cstdint>

//   // 内存栈（对照 UE FMemStackBase, Misc/MemStack.h）
//   // 借内存不释放，标记弹出批量回收；分档块池（解药1：按请求规格选块，减少内部碎片）
//   class MemStack{
//   public:
//       struct Mark{char* pos;};

//       MemStack()=default;
//       MemStack(const MemStack&)=delete;
//       MemStack& operator=(const MemStack&)=delete;

//       // 借 n 字节裸内存（单次分配永远连续，绝不跨块）
//       void* Alloc(size_t n){
//           if(top_+n>end_){                    // 当前块装不下
//               size_t chunk=ChunkSizeFor(n);   // ★按请求规格选档
//               chunks_.push_back(std::make_unique<char[]>(chunk));
//               top_=chunks_.back().get();
//               end_=top_+chunk;
//           }
//           void* p=top_;
//           top_+=n;
//           return p;
//       }

//       // 在内存栈上构造对象（析祖要调用方手动 p->~T()）
//       template<typename T,typename... Args>
//       T* New(Args&&... args){
//           return new(Alloc(sizeof(T))) T(std::forward<Args>(args)...);
//       }

//       Mark GetMark() const {return Mark{top_};}
//       void Pop(Mark m){top_=m.pos;}           // 弹回：标记后借的全失效

//       size_t GetNumChunks() const {return chunks_.size();}

//   private:
//       // ★分档：请求贴哪档给哪档（档 = 2 的幂，向上取）
//       // 33KB →64KB 档；1KB →2KB 档；700B →1KB 档
//       static size_t ChunkSizeFor(size_t n){
//           size_t chunk=MIN_CHUNK_;
//           while(chunk<n) chunk*=2;            // 翻倍到装得下为止
//           return chunk;
//       }

//       static constexpr size_t MIN_CHUNK_=4*1024;    // 最小 4KB（小块请求不再固定 64KB）
//       std::vector<std::unique_ptr<char[]>> chunks_; // 向系统借的整块清单
//       char* top_=nullptr;                            // 当前分配位置
//       char* end_=nullptr;                            // 当前块末尾
//   };

//   要点三条：
//   - #include <new> 别漏——placemennew 在这
//   - New<T> 返回裸指针，析构自己调（p->~T()）——内存栈只管内存不管生命周期
//   - Pop 之后那片内存“逻辑上死了”，再Alloc 会复用同一地址

//   验证效果（临时塞 main.cpp 跑一次，看完删）

//   #include "Core/Public/MemStack.h"
//   #include <cstdio>

//   MemStack ms;shi
//   auto mark = ms.GetMark();

//   int* a = ms.New<int>(42);
//   float* b = ms.New<float>(3.14f);
//   char* s = (char*)ms.Alloc(100);

//   printf("a=%d b=%f chunks=%zu\n", *a, *b, ms.GetNumChunks());
//   ms.Pop(mark);                                    // a/b/s 同时回收，零 delete

//   int* c = ms.New<int>(7);
//   printf("%s\n", (void*)c == (void*)a ? "地址相同——内存真回收了: "地址不同");

//   预期输出：
//   a=42 b=3.140000 chunks=1        ←64KB 内全装下，只借了一块
//   地址相同——内存真回收了         ←Pop 弹回指针，下次 Alloc 复用

//   注意：你 git 里已经有一个 MemStack.h（上上次提交 e31a337
//   带进去的，就是我之前写好又被你拦下那版+你后来的改动）——写之前先看看现有那个到什么程度，别重复建。