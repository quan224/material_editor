---
name: answer-directly
description: 答题先给名词+一句话+结果，问题词决定答案长度，别默认教学模式
metadata: 
  node_type: memory
  type: feedback
  originSessionId: f6bd5b96-13a1-4f89-807f-db37f2a43fff
  modified: 2026-10-27T10:59:51.019Z
---

用户问"这是什么/在干什么"时，第一句就要给出：名词（概念名）+ 一句话定义 + 结果。不要铺背景、对照表、原理、实验。

**Why:** 2026-10-27 Vulkan 支线答疑，用户问 getRequiredExtensions/vector 区间构造，连续三轮才收敛到他要的答案：第一轮铺扩展哲学+DX12 对照（被批"瞎回答"），第二轮还剩表格，第三轮骂了才给三行干净版。用户原话："就整个事情需要我问题三遍才能回答给我吗？"

**How to apply:** 问题词定答案长度："是什么/在干什么"→名词+一句话+结果；"为什么"→才展开原理；实验只在"我的结论可能错、需要编译器裁决"时跑。项目是教学性质不代表每句话都要教学化——用户要深挖会自己问下一层。相关：[[lesson-plan-policy]]
