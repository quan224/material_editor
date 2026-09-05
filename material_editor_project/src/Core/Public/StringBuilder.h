#pragma once
#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <string>

// 栈缓冲字符串拼装机（对照 UE TStringBuilder<N>, Misc/StringBuilder.h）
// 缓冲在对象内，未超容量前零堆分配；超容量退化到堆（对照 UE 的 ResizeToGrow）
class StackStringBuilder {
public:
    explicit StackStringBuilder(size_t capacity = 256)
        : capacity_(capacity) {
        if (capacity_ > FIXED_) {          // 大容量开堆
            heap_ = new char[capacity_];
            data_ = heap_;
        } else {
            data_ = fixed_;                // 常用容量走栈内缓冲
        }
        data_[0] = '\0';
    }

    ~StackStringBuilder() { delete[] heap_; }

    StackStringBuilder(const StackStringBuilder&) = delete;
    StackStringBuilder& operator=(const StackStringBuilder&) = delete;

    StackStringBuilder& Append(const char* s) {
        size_t n = std::strlen(s);
        Ensure(len_ + n + 1);
        std::memcpy(data_ + len_, s, n + 1);
        len_ += n;
        return *this;
    }

    StackStringBuilder& Append(char c) {
        Ensure(len_ + 2);
        data_[len_++] = c;
        data_[len_] = '\0';
        return *this;
    }

    StackStringBuilder& Append(const std::string& s) { return Append(s.c_str()); }

    StackStringBuilder& Appendf(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        va_list copy;
        va_copy(copy, args);
        int need = vsnprintf(nullptr, 0, fmt, copy);   // 先测需要多少字节
        va_end(copy);
        if (need > 0) {
            Ensure(len_ + (size_t)need + 1);
            vsnprintf(data_ + len_, (size_t)need + 1, fmt, args);
            len_ += (size_t)need;
        }
        va_end(args);
        return *this;
    }

    template<typename T>
    StackStringBuilder& operator<<(const T& v) { return Append(v); }

    const char* GetData() const { return data_; }
    size_t Len() const { return len_; }
    bool IsEmpty() const { return len_ == 0; }
    void Reset() { len_ = 0; data_[0] = '\0'; }   // 复用缓冲（零再分配）

    std::string ToString() const { return std::string(data_, len_); }

private:
    void Ensure(size_t need) {
        if (need <= capacity_) return;
        while (capacity_ < need) capacity_ *= 2;
        char* bigger = new char[capacity_];
        std::memcpy(bigger, data_, len_ + 1);
        delete[] heap_;
        heap_ = bigger;
        data_ = bigger;
    }

    static const size_t FIXED_ = 256;   // 栈内固定缓冲大小
    char fixed_[FIXED_];
    char* heap_ = nullptr;              // 超容量后才有
    char* data_;
    size_t capacity_;
    size_t len_ = 0;
};
