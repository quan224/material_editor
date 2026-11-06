---
name: retry-mechanical-tasks
description: 网络推送/拉取等机械操作失败时自动多轮重试（间隔+循环），不要失败两次就丢回给用户
metadata: 
  node_type: memory
  type: feedback
  originSessionId: f6bd5b96-13a1-4f89-807f-db37f2a43fff
  modified: 2026-10-29T12:41:19.000Z
---

git push/pull 等机械操作遇网络失败时，自己跑循环重试（如 8 次 × 30 秒间隔，成功即停），全部失败才汇报。

**Why:** 2026-10-29 推送遇网络重置，我只试了 3 次就把"等网络恢复再说"丢回给用户，被批："能不能不要这种破事也麻烦我啊，你就不能自己多试几次啊"。第 7 次重试实际就成功了。

**How to apply:** 任何可重试的机械操作（网络、构建重跑、镜像源切换）默认自动多轮重试再汇报，不把中间态甩给用户。相关：[[answer-directly]]
