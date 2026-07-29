# 卡普空 RE Engine 技术资料汇编

> 本目录收录全网能找到的、关于卡普空 **RE Engine**（卡普空自研游戏引擎，用于《生化危机 7》、生化危机 2/3/4 重制版、《生化危机：村庄》、《鬼泣 5》、《怪物猎人 世界/崛起》、《龙之信条 2》、《怪物猎人 荒野》、《街霸 6》等）的**技术分享、演讲、报告、抓帧拆解**。
>
> 编制时间：2026-07-29。资料来自 5 路并行网络检索（英文会议 / 日文 CEDEC / DF 等技术媒体 / 中文资料 / 渲染技术专题）。

## 这套资料对当前项目的价值

本项目正在做一个「节点图 → 编译器 → HLSL → DX12 PBR 渲染」的材质编辑器，参考的是 UE5 架构。RE Engine 同样是 **PBR + 摄影测量 + 现代 GPU 驱动渲染管线**，是除了 UE5 之外最值得对照的商用引擎。其中**渲染技术专题（`05-渲染技术专题.md`）**和**抓帧拆解（`03-技术媒体抓帧拆解.md`）**与本项目最直接相关——Clustered Deferred、SSR、SSS、TAA、阴影、后处理等的实现细节都能在那些资料里找到。

## 文件导航

| 文件 | 内容 | 条目数（约） |
|---|---|---|
| [01-官方技术大会.md](01-官方技术大会.md) | GDC、REAC、卡普空自办大会（RE:2019 / RE:2022 / RE:2023）—— **最权威的一手资料** | 40+ |
| [02-CEDEC与日文资料.md](02-CEDEC与日文资料.md) | CEDEC 历年演讲 + 日文技术媒体（CGWORLD / GameMakers / 4Gamer / Famitsu 等） | 50+ |
| [03-技术媒体抓帧拆解.md](03-技术媒体抓帧拆解.md) | Digital Foundry、NX Gamer、VGTech、TechPowerUp、IGN、PC Gamer、80 Level、NVIDIA 博客 | 70+ |
| [04-中文技术资料.md](04-中文技术资料.md) | 知乎 / 博客园 / B 站 / 机核 / 游民等中文资料（含 CEDEC/GDC 译文） | 50+ |
| [05-渲染技术专题.md](05-渲染技术专题.md) | 按 13 个渲染主题梳理（GI / SSR / TAA / 阴影 / SSS / 头发 / 后处理 / Switch / 次世代 / 开放世界 / 动画 …） | 13 主题 |
| [功能设计/](功能设计/README.md) | **把 RE Engine 技术作为真功能嵌入项目的设计文档**（RT 预览 / SSS 皮肤 / Meshlet / Profiler …） | 4 已写 + 7 待展开 |

## 🌟 强烈推荐的必读资料（Top 10）

如果只看 10 份，按这个顺序：

1. **Behind the Pretty Frames: Resident Evil** — RE Engine 光栅管线最详尽的抓帧拆解（G-Buffer / SSR / SSS / TAA / 阴影 / 后处理，含 struct 布局）
   👉 https://mamoniem.com/behind-the-pretty-frames-resident-evil/
2. **Capcom Open Conference RE:2023 官方存档**（双语，含 slides + 视频 + Q&A）—— 一手资料总入口
   👉 https://www.capcom-games.com/coc/2023/en/
3. **CEDEC 2018 最新タイトルのグラフィックス最適化事例**（94 页 slides）—— RE2/DMC5 的 CPU/GPU 优化决定版
   👉 https://www.docswell.com/s/CAPCOM_RandD/ZXYVJG-cedec2018
4. **RE ENGINE Meshlet Rendering Pipeline（REAC 2025）**—— 现代 GPU 驱动渲染（龙信 2 / 荒野）
   👉 https://enginearchitecture.org/downloads/REAC_2025_Capcom.pdf
5. **Q&A: How Capcom Brought Path Tracing to RE ENGINE（NVIDIA 博客）**—— 路径追踪演进（生化 9 / Pragmata）
   👉 https://developer.nvidia.com/blog/qa-how-capcom-brought-path-tracing-to-re-engine-across-pragmata-and-resident-evil-requiem/
6. **CAPCOM オープンカンファレンス RE:2019 官方页**—— RE2/DMC5 的引擎设计到实现（CEDEC AWARDS 2020 最优赏）
   👉 https://www.capcom.co.jp/RE2019/
7. **实现快速迭代的游戏引擎设计（CEDEC 2016 中文译文）**—— RE Engine 工具架构（Tool/Runtime 分离、远程同步）
   👉 https://zhuanlan.zhihu.com/p/36657962
8. **DMC5 frame render breakdown（鬼泣 5 抓帧分析）**—— DX12 逐 Pass 拆解（蒙皮 CS / GI / SSSS / 眼睛晶体反射）
   👉 https://zhuanlan.zhihu.com/p/358786495
9. **RE ENGINE's Past and Future（RE:2023）**—— 最接近官方的 RE Engine 架构综述 + 次世代 REX
   👉 https://www.capcom-games.com/coc/2023/en/session/10/
10. **《怪物猎人崛起》卡普空如何在 Switch 上优化（CEDEC 译文）**—— RE Engine 从延迟切到正向渲染的关键资料
    👉 https://www.gameres.com/888638.html

## 关于"所有"

> 用户要求"搜索网上**所有**相关资料"。本汇编已尽可能覆盖主要会议（GDC / REAC / CEDEC / 卡普空自办大会）、主流技术媒体（DF / NXG / VGTech / TechPowerUp 等）、以及中英文社区高质量内容。但互联网资料浩繁，"所有"是目标而非保证。每份文件末尾的"未找到 / 待核实"小节记录了已知的覆盖缺口，便于后续补充。

## 关键入口（聚合页）

- **Capcom R&D 官方 Docswell**（历年演讲 slides 总集）：https://www.docswell.com/s/CAPCOM_RandD
- **Capcom R&D 官方 X**：https://x.com/CAPCOM_RandD
- **RE Engine — 维基百科（EN）**：https://en.wikipedia.org/wiki/RE_Engine
- **RE Engine — 维基百科（ZH）**：https://zh.wikipedia.org/zh-cn/RE引擎
- **GameMakers 卡普康 CEDEC2015–2018 讲演资料まとめ**：https://gamemakers.jp/article/2024_01_22_59359/
