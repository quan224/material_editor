// ProfilerDock.cpp
#include "ProfilerDock.h"
#include <QPainter>
#include <algorithm>

ProfilerPanel::ProfilerPanel(GpuProfiler* profiler, QWidget* parent)
    : QWidget(parent), profiler_(profiler)
{
    setMinimumSize(280, 260);
    setAutoFillBackground(true);
    // 深色背景
    QPalette p = palette();
    p.setColor(QPalette::Window, QColor(30,30,30));
    setPalette(p);

    connect(&timer_, &QTimer::timeout, this, &ProfilerPanel::onTick);
    timer_.start(100); // 10Hz 刷新
}

void ProfilerPanel::onTick()
{
    const FrameStats& s = profiler_->GetStats();
    history_.push_back(s.frameGpuMs);
    if (history_.size() > 180) history_.erase(history_.begin());
    update(); // 触发 paintEvent
}

void ProfilerPanel::paintEvent(QPaintEvent*)
{
    QPainter g(this);
    g.setRenderHint(QPainter::Antialiasing);
    QRectF r = rect();
    const FrameStats& s = profiler_->GetStats();

    float fps = s.frameCpuMs > 0.f ? 1000.f / s.frameCpuMs : 0.f;

    // 顶部：FPS 大数 + 帧时间
    g.setPen(QColor(230,230,230));
    QFont f = g.font(); f.setPointSize(20); f.setBold(true); g.setFont(f);
    g.drawText(QPointF(12, 32), QString("FPS %1").arg(fps, 0, 'f', 0));
    f.setPointSize(9); f.setBold(false); g.setFont(f);
    g.setPen(QColor(160,200,255));
    g.drawText(QPointF(120, 24), QString("CPU %1 ms").arg(s.frameCpuMs, 0, 'f', 2));
    g.drawText(QPointF(120, 38), QString("GPU %1 ms").arg(s.frameGpuMs, 0, 'f', 2));

    // 帧时间折线（历史）
    QRectF chartR(12, 50, r.width()-24, 60);
    g.setPen(QColor(90,90,90)); g.drawRect(chartR);
    if (history_.size() > 1) {
        float maxV = *std::max_element(history_.begin(), history_.end());
        maxV = std::max(maxV, 16.7f); // 至少按 60fps 标定
        g.setPen(QColor(120,220,160));
        QPainterPath path;
        for (size_t i = 0; i < history_.size(); ++i) {
            float x = chartR.left() + (float)i / (history_.size()-1) * chartR.width();
            float y = chartR.bottom() - (history_[i] / maxV) * chartR.height();
            if (i == 0) path.moveTo(x, y); else path.lineTo(x, y);
        }
        g.drawPath(path);
        // 16.67ms (60fps) 参考线
        float y60 = chartR.bottom() - (16.7f / maxV) * chartR.height();
        g.setPen(QColor(200,120,120)); g.drawLine(QPointF(chartR.left(), y60), QPointF(chartR.right(), y60));
    }

    // 各 pass 横条
    float y = chartR.bottom() + 16;
    float maxPass = 1.f;
    for (auto& pp : s.passes) maxPass = std::max(maxPass, pp.gpuMs);
    g.setPen(QColor(220,220,220)); f.setPointSize(8); g.setFont(f);
    for (auto& pp : s.passes) {
        g.drawText(QPointF(12, y + 8), QString::fromStdString(pp.name));
        QRectF bar(110, y, (r.width()-12-110) * (pp.gpuMs / maxPass), 10);
        g.setPen(Qt::NoPen); g.setBrush(QColor(100,160,240)); g.drawRoundedRect(bar, 3, 3);
        g.setBrush(Qt::NoBrush); g.setPen(QColor(180,180,180));
        g.drawText(QPointF(bar.right() + 4, y + 9), QString("%1").arg(pp.gpuMs,0,'f',2));
        y += 14;
    }

    // 底部统计
    g.setPen(QColor(160,160,160));
    g.drawText(QPointF(12, r.bottom()-8),
        QString("draws %1   tris %2   mem %3 MB")
            .arg(s.drawCalls).arg(s.triangles).arg(s.gpuMemUsed / (1024*1024)));
}

// —— Dock 外壳 ——
ProfilerDock::ProfilerDock(GpuProfiler* profiler, QWidget* parent)
    : QDockWidget("Profiler", parent)
{
    setWidget(new ProfilerPanel(profiler));
    setObjectName("ProfilerDock"); // saveState 用
}
