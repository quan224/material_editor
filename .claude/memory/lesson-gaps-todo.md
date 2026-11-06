---
name: lesson-gaps-todo
description: 终裁后教案待补缺口清单（2026-09-12 定，做到对应课之前补）
metadata: 
  node_type: memory
  type: project
  originSessionId: f6bd5b96-13a1-4f89-807f-db37f2a43fff
  modified: 2026-09-12T08:48:21.514Z
---

# 教案待补缺口（CLAUDE.md 终裁 743b215 的落地项）

做到对应课之前补教案，别提前大改：

1. **课 12**：多选/框选（裁决保留，教案现只有单选）
2. **课 15-17**：渲染改全量——延迟渲染 GBuffer 全套（教案现是前向单 pass）、阴影升级到 CSM 完整、Bloom+DOF 后处理（Tonemap 已有）、多线程渲染线程（教案完全没有）；篇幅装不下时课 17 后加半课
3. **课 20**：LWC 求值链（WSAdd/WSMul CPU 实现，类型位已有）；DerivativeAutogen 最简版（链式法则+核心算子导数表，兜底条款）

**Why**: 2026-09-12 用户终裁"渲染不省略、多选要、其他按清单"——CLAUDE.md 已有完整 keep/omit 表，此处只记教案侧缺口（教案还没跟上裁决的部分）。
**How to apply**: 用户开始做某课前，先查这份清单，把该课缺口补进教案再动手。
