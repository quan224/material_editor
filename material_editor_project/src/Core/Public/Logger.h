#pragma once
#include <QDebug>
#include <cstdio>
#include <fstream>

// 日志写入文件（Windows 下 qDebug 输出走 OutputDebugString，普通终端看不到）
// 相对路径 "debug_log.txt" 写到程序的工作目录。
inline void LogToFile(const char* msg) {
    // 静态标志：程序每次启动首次调用时清空文件(trunc)，之后追加(app)。
    // static 变量在新进程启动时重置，所以每次运行都先覆盖前次日志，
    // 多次运行的日志互不干扰，不会续写混在一起。
    static bool firstCall = true;
    std::ofstream logFile("debug_log.txt",
        firstCall ? (std::ios::out | std::ios::trunc) : (std::ios::out | std::ios::app));
    if (logFile.is_open()) {
        logFile << msg << std::endl;
        logFile.close();
    }
    firstCall = false;
}

#define ME_LOG_INFO(msg, ...) \
    do { \
        qDebug("[INFO] " msg, ##__VA_ARGS__); \
        char _logBuf[512]; \
        snprintf(_logBuf, sizeof(_logBuf), "[INFO] " msg "\n", ##__VA_ARGS__); \
        LogToFile(_logBuf); \
    } while(0)

#define ME_LOG_WARNING(msg, ...) \
    do { \
        qWarning("[WARN] " msg, ##__VA_ARGS__); \
        char _logBuf[512]; \
        snprintf(_logBuf, sizeof(_logBuf), "[WARN] " msg "\n", ##__VA_ARGS__); \
        LogToFile(_logBuf); \
    } while(0)

#define ME_LOG_ERROR(msg, ...) \
    do { \
        qCritical("[ERROR] " msg, ##__VA_ARGS__); \
        char _logBuf[512]; \
        snprintf(_logBuf, sizeof(_logBuf), "[ERROR] " msg "\n", ##__VA_ARGS__); \
        LogToFile(_logBuf); \
    } while(0)


#define ME_CHECK(expr)\
    do{\
        if(!(expr)){\
            ME_LOG_ERROR("CHECK failed: %s\n  file:%s\n  line:%d", #expr, __FILE__, __LINE__);\
            assert(false&&#expr);\
        }\
    } while(0)

// ME_CHECK 可选附加信息版（待需要时启用，现有 ME_CHECK 调用零影响）：
//   ME_CHECK(expr)                          → 原样
//   ME_CHECK(expr, " | ctx:%d", someVar)    → 失败时追加一行上下文
// 原理：__VA_ARGS__ 原样文本替换；" " 与用户格式串拼接成完整格式串（相邻字面量自动连接），
//       变量跟在逗号后按占位符填充；## 在空参时吞掉悬空逗号（GNU 扩展，MinGW/GCC 支持）
// #define ME_CHECK(expr, ...)\
//     do{\
//         if(!(expr)){\
//             ME_LOG_ERROR("CHECK failed: %s\n  file:%s\n  line:%d", #expr, __FILE__, __LINE__);\
//             ME_LOG_ERROR(" " ##__VA_ARGS__);\
//             assert(false&&#expr);\
//         }\
//     } while(0)
