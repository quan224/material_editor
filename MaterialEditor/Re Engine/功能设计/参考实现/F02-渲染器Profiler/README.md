# F02 — 渲染器 Profiler 面板（完整参考实现）

> DX12 GPU timestamp 计时 + CPU/draw/tri 统计 + Qt 实时面板。**完整参考实现**。
> 设计蓝本：RE Engine「正確なパフォーマンス情報を毎日蓄積！」（CEDEC2018 自動計測レポート）+ REProfiler。

## 提供的文件

| 文件 | 作用 |
|---|---|
| `GpuProfiler.h/.cpp` | DX12 timestamp query heap、每 pass GPU 计时（三缓冲解析）、CPU 计时、draw/tri 计数、`FrameStats` 结构 |
| `ProfilerDock.h/.cpp` | Qt `QDockWidget`：FPS 大数、帧时间折线、pass 横条、底部统计 |

## 设计要点

- **GPU timestamp 三缓冲**：GPU query 结果要等 GPU 跑完才能 CPU 读，有 N 帧延迟。用 3 份解析缓冲（resolve buffer）+ 3 份 `FrameStats`，每帧解析当前帧的 query 到 `resolve[frameIndex]`，CPU 读 `frameIndex - 2` 那份（已确定完成）。注释里写清楚，避免新手显示成"上一帧"。
- **per-pass begin/end**：`BeginPass(cmd,"gbuffer")` ... `EndPass(cmd)` 各发一条 timestamp query，解析后 `(end-begin)/freq*1000` 得 ms。query heap 容量 = `kMaxPasses*2*3`。
- **CPU 计时**：`std::chrono` 测一帧 CPU 提交耗时。
- **draw/tri 计数**：渲染器每发一个 draw 调 `OnDraw(triCount)`，帧首清零。
- **显存**：`FrameStats::gpuMemUsed` 由渲染器从 F01 `GpuAllocator` 填（可选）。

## 集成步骤

1. `Renderer::Initialize`：`profiler_.Init(device, kFrameCount=3)`。
2. 每帧（frameIndex）：
   ```cpp
   profiler_.BeginFrame(frameIndex);                 // CPU 计时开始 + 计数清零
   profiler_.BeginPass(cmd, "GBuffer"); ...绘制...; profiler_.EndPass(cmd);
   profiler_.BeginPass(cmd, "Lighting"); ...;        profiler_.EndPass(cmd);
   // ...
   profiler_.OnDraw(triCount);  // 每个 DrawIndexed/ExecuteIndirect 后调
   profiler_.EndFrame(cmd, frameIndex);              // ResolveQueryData + CPU 计时结束
   ```
3. UI：`ProfilerDock` 持 `GpuProfiler*`，`QTimer` 100ms 刷新重绘，`GetStats()` 拿最新可读帧。
4. UI 加到主窗口：`mainWindow->addDockWidget(Qt::RightDockWidgetArea, profilerDock)`。

## 关键坑（注释里也写了）

- **GPU timestamp 一帧延迟**：`GetStats()` 返回的是 2 帧前的数据，正常。
- **frequency**：`device->GetTimestampFrequency(&freq)`，别假设。不同 GPU tick 频率不同。
- **ResolveQueryData 必须在命令里**：它是 GPU 侧把 query 写进 readback buffer 的命令，要在 `EndFrame` 里用 command list 发出。

## 参考文献

- **RE Engine 自動計測レポート（CEDEC2018，齊藤/西井）**：https://www.docswell.com/s/CAPCOM_RandD/5DE486-cedec2018
- **RE:2023 REProfiler 工具链**：https://www.capcom-games.com/coc/2023/en/
- **DX12 Timestamp Queries 官方**：https://learn.microsoft.com/en-us/windows/win32/direct3d12/timing
- **Pix & timing（GPU 计时实践）**：https://devblogs.microsoft.com/directx/pix-timing/

## 诚实边界

- 不保证首次编译跑通——三缓冲 + query 索引计算容易差一个，开 D3D12 debug layer 跑一遍，报错我修。
- 这是"实时面板"版（M3）；M4 落盘 CSV 回归报告可后续加（RE Engine 那套是每日采集）。
