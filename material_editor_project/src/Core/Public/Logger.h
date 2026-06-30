#pragma once
#include <QDebug>
#include <cstdio>
#include <fstream>

// 日志写入文件（Windows 下 qDebug 输出走 OutputDebugString，普通终端看不到）
  inline void LogToFile(const char* msg) {
      std::ofstream logFile("E:/UE5_mirror/material_editor_project/build/Debug/debug_log.txt", std::ios::app);                                 
      if (logFile.is_open()) {
          logFile << msg << std::endl;
          logFile.close();
      }
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