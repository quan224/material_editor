// ProfilerDock.h
// Qt 实时性能面板：FPS / 帧时间折线 / 各 pass 横条 / draw&tri&mem。
#pragma once
#include <QDockWidget>
#include <QWidget>
#include <QTimer>
#include <vector>
#include "GpuProfiler.h"

class ProfilerPanel : public QWidget {
    Q_OBJECT
public:
    explicit ProfilerPanel(GpuProfiler* profiler, QWidget* parent = nullptr);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    void onTick();
    GpuProfiler* profiler_;
    QTimer timer_;
    std::vector<float> history_; // 最近 N 帧 frameGpuMs
};

class ProfilerDock : public QDockWidget {
    Q_OBJECT
public:
    explicit ProfilerDock(GpuProfiler* profiler, QWidget* parent = nullptr);
};
