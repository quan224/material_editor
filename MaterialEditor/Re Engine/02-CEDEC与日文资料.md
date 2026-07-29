# 02 — CEDEC 与日文技术资料

> CEDEC（日本最大的开发者大会）是 RE Engine 日文一手资料的主战场。卡普空历年在这里讲了引擎架构、渲染优化、光照、摄影测量、自动计测等。本文件也收录日文技术媒体（CGWORLD / GameMakers / 4Gamer / Famitsu / 電ファミ / Autodesk AREA 等）的详报与访谈。
> 注：卡普空自办的 RE:2019/2022/2023 大会见 [01-官方技术大会.md](01-官方技术大会.md)（与 CEDEC 是不同活动）。

---

## A. CEDEC セッション（技术讲演）

### ラピッドイテレーションを実現するゲームエンジンの設計
- **登壇者：** 石田智史（リードプログラマ）、是松匡亮
- **年：** CEDEC 2016
- **URL：** https://cedec.cesa.or.jp/2016/session/ENG/1130.html
- **中文译文：** https://zhuanlan.zhihu.com/p/36657962
- **摘要：** RE Engine 架构刷新。从 MT Framework 反省出发：模块化、远程实机编辑、全资源动态 reload、C# 写游戏逻辑缩短 build 时间等快速迭代设计，配 RE7 开发案例。

### 「バイオハザード7」を実現するレンダリング技術
- **登壇者：** 三嶋仁、清水昭尋（技术开发室）
- **年：** CEDEC 2016
- **URL：** https://cedec.cesa.or.jp/2016/session/ENG/918.html
- **slides：** https://www.docswell.com/s/CAPCOM_RandD/ZJLVPJ-cedec2016
- **摘要：** RE Engine 渲染系统机制与优化。光照/阴影、GI、网格高效渲染、流体仿真技术的实现导向介绍。从 GPU 驱动渲染发展而来。

### BIOHAZARD 7 - PHOTOGRAMMETRY -
- **登壇者：** 黒籔裕也、遠藤和幸（CS 第一开发统括 第一开发部）
- **年：** CEDEC 2016
- **URL：** https://cedec.cesa.or.jp/2016/session/VA/1342.html
- **详报（Famitsu）：** https://www.famitsu.com/news/201608/28114313.html
- **详报（IGN Japan）：** https://jp.ign.com/cedec-2016/6194/7
- **摘要：** RE7 引入的摄影测量（照片测量 3D 扫描）实制作案例。100 台 EOS Kiss X7 的扫描棚、PhotometricStereo 抽 NormalMap、特殊化妆下的生物扫描等资产管线内幕。

### 壊れ物への取り組み 〜いかにベイクを美しく魅せるか〜（RE7 破坏表现）
- **登壇者：** 滝崇海（Technical Artist）
- **年：** CEDEC 2017
- **CGWORLD 详报：** https://cgworld.jp/feature/201709-cedec2017-capcom.html
- **摘要：** Maya + Pulldownit 预计算破坏并烘焙到 RE ENGINE 上回放的"展示型破坏"工作流，与 HAVOK 实时破坏的分工。PSVR 要求恒定 1.2ms/帧 下的优化。

### 【CEDEC2018】最新タイトルのグラフィックス最適化事例 ⭐
- **登壇者：** 三嶋仁
- **年：** CEDEC 2018
- **slides（94 页全，Capcom 官方 Docswell）：** https://www.docswell.com/s/CAPCOM_RandD/ZXYVJG-cedec2018
- **摘要：** RE2 重制版 / DMC5 的 CPU/GPU 优化决定版资料。AABB/frustum culling 的 SIMD 化、MultiDrawIndirect/ExecuteIndirect 的 GPU 驱动网格绘制、GPU 遮挡剔除（Compute Shader + CountBuffer + Wave Intrinsic 压缩）、网格自动分割剔除、部分 Z-prepass、定向光阴影图预烘焙（Morton 编码 + 四叉树压缩，16K 压到数 MB）、DepthBoundsTest 等，附实现与实测数据。

### 正確なパフォーマンス情報を毎日蓄積！RE ENGINE タイトルを支える自動計測レポート
- **登壇者：** 齊藤俊介、西井崇也（技术研究开发部）
- **年：** CEDEC 2018
- **CEDiL：** https://cedil.cesa.or.jp/cedil_sessions/view/1841 ｜ **slides：** https://www.docswell.com/s/CAPCOM_RandD/5DE486-cedec2018
- **摘要：** RE Engine 全作品通用的自动计测报告系统——崩溃/错误、CPU/GPU 性能、内存使用自动采集；以及 RE Engine AI 模块驱动的通用自动游玩（autoplay）实现与运营。

### アニメーションワークフロー改善のための RE ENGINE と DCC ツールのアニメーション同期再生システム
- **登壇者：** 内山創一（CS 制作统括 技术研究开发部 技术开发室）
- **年：** CEDEC 2018
- **CEDiL：** https://cedil.cesa.or.jp/cedil_sessions/view/1810 ｜ **slides：** https://www.docswell.com/s/CAPCOM_RandD/KLL2Y3-cedec2018
- **摘要：** 操作 Maya/MotionBuilder 时在 RE ENGINE 上实时同步回放动画的系统实现。Python + 通信做 DCC 联动，及早发现引擎功能引发的问题，省工时提品质。

### モンスターハンターワイルズにおけるフィールド描画システムの最適化
- **年：** CEDEC 2025
- **URL：** https://cedec.cesa.or.jp/2025/timetable/detail/s67adbbaac8a9e
- **摘要：** RE Engine 构建的场地绘制系统与优化。各绘制系统的基本原理/数据结构/基本绘制处理 + 荒野新开发技术。

### モンスターハンターワイルズにおける動的な環境変化に対応したリアルタイムグローバルイルミネーション
- **年：** CEDEC 2025
- **URL：** https://cedec.cesa.or.jp/2025/timetable/detail/s67aea5fc8fdc6
- **摘要：** 荒野新开发的动态环境变化应对实时 GI「DPGI（Dynamic Probe Global Illumination）」。时间流逝/天气变化跟随的间接光实现手法。

### モンスターハンターワイルズにおけるモンスターのプロシージャルアニメーション
- **年：** CEDEC 2025
- **URL：** https://cedec.cesa.or.jp/2025/timetable/detail/s67ad91775bcd8
- **摘要：** RE Engine 动画 know-how 应用到荒野怪物。地形传感器 + IK 的姿态补正做自然踩踏表现。

### 参考：CEDEC 2025 卡普空讲座中止报道
- **URL：** https://automaton-media.com/articles/newsjp/capcom-cedec-20250705-347902/ ｜ https://www.itmedia.co.jp/news/articles/2507/07/news075.html
- **摘要：** 荒野性能优化讲座的一部分因作品遭诽谤/威胁而中止的记录。非技术资料，但用于核实 2025 卡普空讲座群的完整性。

---

## B. CGWORLD 技术详报（CEDEC/大会的深度报道）

### 内製エンジン「RE ENGINE」の設計思想からタイトルでの技術活用まで 〜RE:2019（1）
- **URL：** https://cgworld.jp/feature/201910-re2019-01.html
- **摘要：** RE:2019 前篇。竹内潤谈内产引擎开发意义；石田智史讲 RE Engine 设计（MT Framework 历史、模块结构、向下兼容、工具进程分解、资源访问限制、C# 脚本/REVM/FrameGC/IL2CPP）。

### 『バイオハザード RE:2』で作業を 1/3 以下に効率化した「組み技リターゲット」〜RE:2019（2）
- **URL：** https://cgworld.jp/feature/201910-re2019-02.html
- **摘要：** RE:2019 中篇。动画技术：模块化绑定、组技 retarget、程序化动画/motion matching 三场详报。

### RE ENGINE が実現した『DMC5』のフォトリアルな破壊 & 揺れもの 〜RE:2019（3）
- **URL：** https://cgworld.jp/feature/201911-re2019-03.html
- **摘要：** RE:2019 后篇。滝崇海的 DMC5 仿真（Dynamics/GPU Motion/CPU Motion/Chain）+ 相曽将仁的 VFX + 伊集院勝的 closing（Panta Rhei 重新出发、Stadia/Vulkan 支持、未来展望）。

### シミュレーションをベイクして魅せる、『バイオ7』における破壊表現 〜CEDEC2017
- **URL：** https://cgworld.jp/feature/201709-cedec2017-capcom.html
- **摘要：** CEDEC 2017"壊れ物"详报。Pulldownit 全流程、HAVOK 实时破坏、断面 UV 自动化、简易网格降负载、polygon 数对比数据。

### 「世界最速の開発」を可能にする！バイオハザード 7
- **URL：** https://cgworld.jp/feature/201702-cgw222T2-bio.html
- **摘要：** RE7 的 RE Engine 导入与快速开发；完全弃用光照贴图式间接光、改用 probe network 方式 GI 等渲染手法的历史变迁。

### RE ENGINE が描き出す和の『モンスターハンター』〜『モンハンライズ』
- **URL：** https://cgworld.jp/feature/202107-mhrise.html
- **摘要：** Switch 上用 RE Engine 全新构建 Forward Rendering 管线的案例。伪 SSS、Fur 模糊、油膜（Thin Film Iridescence）、环境图折射、大气散射、水面反射等 shader 实现，Python 资产批量处理。含访谈。

---

## C. GameMakers（ゲームメーカーズ）技术报告与访谈

### カプコン技研の開発者が CEDEC2015〜2018 にて公開した講演資料まとめ
- **URL：** https://gamemakers.jp/article/2024_01_22_59359/
- **摘要：** 网罗性汇总卡普空 CEDEC 2015–2018 的演讲视频/slides/报告的枢纽文章（含"CAPCOM オープンテクニカルカンファレンス スクランブル"）。追 RE Engine 初期技术史的索引。

### RE ENGINE 開発者 & エンジニア座談会。カプコンの文化を掘り下げる
- **URL：** https://gamemakers.jp/article/2022_11_04_22707/
- **摘要：** 伊集院勝等基础技术研究开发团队的座谈。技术者视角谈 RE Engine 的规格/设计意图/开发体制/文化。

### カプコンの内製エンジン「RE ENGINE」をゲームエンジンプログラマが触ってみた（RE:2022 报告）
- **URL：** https://gamemakers.jp/article/2022_12_14_25313/
- **摘要：** 游戏引擎程序员 rita 从 RE:2022 体验展示推察 RE Engine 功能/结构——层级/检查器/资产浏览器、prefab 资产（json）、shader 编辑器、scene memo、motion matching 等工具 UI 与工作流设计。

### CAPCOM Open Conference Professional RE:2023 レポート・インタビュー
- **URL：** https://gamemakers.jp/article/2023_09_01_49045/
- **摘要：** RE:2023 演讲报告 + 以 RE Engine 为主题的全 21 讲大会解说。

### RE ENGINE に触れながらゲーム制作工程を体験（RE:2022 报告 + 伊集田访谈）
- **URL：** https://gamemakers.jp/article/2022_10_04_20029/
- **摘要：** RE:2022 体验型制作工程介绍 + 基础技术研究开发部技术总监伊集院勝访谈。

### RE ENGINE のリアルタイムパストレーシング技術を解説（GDC2026 资料介绍）
- **URL：** https://gamemakers.jp/article/2026_05_26_138337/ ｜ **slides：** https://www.docswell.com/s/CAPCOM_RandD/5DM2NL-gdc2026-implementing-real-time-path-tracing-in-re-engine
- **摘要：** Requiem / PRAGMATA 引入的实时路径追踪实现。参考路径追踪器搭建、DLSS Ray Reconstruction 游戏向优化、与 NVIDIA 合作。含对 CEDEC2018 资料的引用。

### REAC 2025 slides/動画公開（荒野技术资料）
- **URL：** https://gamemakers.jp/article/2025_07_09_110510/
- **摘要：** CEDEC 关联活动上公开的荒野 RE Engine 技术 slides/视频公开通知。

### その他 GameMakers
- **RE:2022/2023 导线文：** https://gamemakers.jp/article/2022_07_28_12913/
- **RE:2026 举办报告：** https://gamemakers.jp/article/2026_07_07_140770/

---

## D. 4Gamer / Famitsu / 電ファミ / その他日文媒体

### カプコン オープンカンファレンス RE:2023 特集
- **URL：** https://www.4gamer.net/games/635/G063507/20231027063/
- **摘要：** RE:2023 综合解说，含开发文化/游戏引擎设计思想、CEDEC 2023 时点 RE Engine/REX 定位。

### 職業探索：RE ENGINE を支え『モンハンライズ』最適化に貢献！末永尚己访谈
- **URL：** https://www.4gamer.net/games/999/G999905/20240529022/
- **摘要：** 特效自动检查等出名的末永尚己的技术职访谈。RE Engine 程序员实务、Switch 向优化的实际。

### カプコン × 近畿大学 産学連携（RE Engine 教育实习，负责人伊集院勝）
- **URL：** https://www.4gamer.net/games/999/G999905/20240905056/
- **摘要：** RE Engine 用于学生实习；通过访谈触及教育现场活用与设计思想。

### カプコン × Google Cloud，开发引入 AI
- **URL：** https://www.4gamer.net/games/991/G999101/20260519017/
- **摘要：** RE Engine 开发管线引入 AI。"AI 不是用来做美术，而是释放创作者潜力"的方针 + 平台/AI 基建战略。

### 『モンスターハンターライズ』はなぜ安定して美しく動くのか
- **URL：** https://automaton-media.com/articles/newsjp/20210407-157177/
- **摘要：** 从 MT Framework 迁到 RE Engine 的理由、Switch 上 RE Engine 运营的优化与稳定运行技术背景。

### カプコン RE:2022 関連（Famitsu/4Gamer）
- **Famitsu 全 21 内容详细：** https://www.famitsu.com/news/202210/04277752.html
- **4Gamer 体验报告（含一瀬泰范访谈）：** https://www.4gamer.net/games/512/G051243/20221003108/
- **Famitsu 大会举办报道：** https://www.famitsu.com/news/202207/11268116.html

### Autodesk AREA 案例
- **龙信 2（RE Engine 首个开放世界）：** https://area.autodesk.jp/case/game/dragonsdogma-2/
- **RE7 摄影测量资产管线：** https://area.autodesk.jp/case/game/biohazard7/
- **摘要：** 两个案例分别讲开放世界基础功能扩展 + 与 Autodesk 工具联动；RE7 摄影测量管线全面重做。

### その他
- **Inside Games RE7 恐怖感新技术访谈：** https://www.inside-games.jp/article/2017/02/07/105189.html
- **ITmedia Game Watch DMC5 访谈：** https://game.watch.impress.co.jp/docs/interview/1127909.html
- **電ファミ RE:2023 报告：** https://news.denfaminicogamer.jp/news/231027q
- **DMC5 VFX 技术笔记（个人 blog）：** https://effect.hatenablog.com/entry/2019/11/04/194547

---

## E. CEDEC AWARDS / 公式 IR

### CEDEC AWARDS 2020 工程部门最优赏（RE:2019 + RE ENGINE）
- **Capcom IR：** https://www.capcom.co.jp/ir/news/html/200909.html
- **PR：** https://prtimes.jp/main/html/rd/p/000001730.000013450.html
- **获奖一览：** https://cedec.cesa.or.jp/2020/event/awards/prize.html
- **GameWatch：** https://game.watch.impress.co.jp/docs/news/1274853.html
- **摘要：** 因自办大会上对内产引擎技术的系统信息发布获最优赏（评价的是整体信息发布贡献，非 RE Engine 单体）。

### RE7 获 CEDEC AWARDS 2017（工程/声音部门优秀赏）
- **Famitsu：** https://www.famitsu.com/news/201707/07137154.html
- **電撃：** https://dengekionline.com/elem/000/001/553/1553611/

### 内製ゲームエンジン「RE ENGINE」｜オンライン統合報告書 2025（公式 IR）
- **URL：** https://www.capcom.co.jp/ir/data/oar/2025/re-engine.html
- **摘要：** RE Engine 官方技术解说。多光照方式支持、低成本 light probe、按用途分别计算的光路（影/反射）、写实~动画表现覆盖力、向 REX 阶段性整合方针。

### Capcom R&D 公式 Docswell（演讲 slides 总集）
- **URL：** https://www.docswell.com/s/CAPCOM_RandD
- **摘要：** CEDEC2016/2018、RE:2022/2023、GDC2026 等演讲 slides 公开账号。个别 slides URL 见各条目。

### Capcom R&D 公式 X（@CAPCOM_RandD）
- **URL：** https://x.com/CAPCOM_RandD
- **摘要：** 开发 RE Engine 的技术研究统括官方账号，CEDEC/大会演讲告知与信息发布的一手源。

---

## 未找到 / 待核实

- **Capcom オープンテクニカルカンファレンス スクランブル**（CEDEC 2015–2018 关联卡普空勉強会）：GameMakers 文章提及，个别演讲 URL 需经文章确认。伊集院勝 CEDEC 2015 场存在，但是否与 RE Engine 直接相关待精查。
- **CEDEC 2019 本会议的卡普空 RE Engine 场**：RE:2019 是独立自办，非 CEDEC 本会议；CEDEC 2019 本会议是否有卡普空 RE Engine 演讲 URL 本次未能确认。
- **Village 单独的 CEDEC 技术演讲 slides（2021）**：COVID 下在线举办的 CEDEC 2020/2021 的 RE Engine 单独演讲难以确认；Village 技术首次较大规模公开是在 RE:2022。
- **Qiita / Zenn 的 RE Engine 技术文章**：日文技术社区存在 writeup，但权威/持久 URL 本次未确定，暂不收录。
