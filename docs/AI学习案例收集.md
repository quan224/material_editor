# AI 辅助学习：全网案例收集（2026-11 搜集）

## 跨领域研究：AI 学习效果的证据（2025 学术界）

### 数学/科学（证据最强）

- [AI tutoring outperforms in-class active learning](https://www.nature.com/articles/s41598-025-97652-6)（Nature / Harvard, Kestin et al. 2025）——**AI 家教效果超过课堂主动学习**：学生学得更多、时间更少、动机更高。2025 年被引最多的教育 AI 研究之一（600+ 引用）
- [Systematic review of intelligent tutoring systems](https://pmc.ncbi.nlm.nih.gov/articles/PMC12078640)（PMC, Létourneau et al. 2025）——K-12 效果总体正面，但受学科/年级/实施质量影响
- [What research shows about generative AI in tutoring](https://www.brookings.edu/articles/what-the-research-shows-about-generative-ai-in-tutoring)（Brookings）——结论：AI 适合**增强**人类教学，不适合替代
- [AI Tutoring is Not a Monolith](https://scale.stanford.edu/research-in-action/ai-tutoring-not-monolith)（Stanford SCALE）——不同 AI 家教设计的效果差异巨大

### 语言学习（稳定正面，中等效应量）

- [Meta-analysis: Chatbots for language learning](https://onlinelibrary.wiley.com/doi/10.1111/ijal.12668)（Wiley, Lyu et al. 2025）——聊天机器人对二语学习有**中等正面效应**
- [AI-driven chatbots in second language education](https://www.sciencedirect.com/science/article/pii/S2215039025000086)（ScienceDirect）——提升熟练度 + 降低语言焦虑（"和 AI 练不怕丢脸"是反复出现的主题）
- [Chatbots in primary EFL education](https://link.springer.com/article/10.1186/s40561-025-00379-0)（Springer）——小学英语：流利度/发音/词汇均有提升

### 音乐

- [UCSD: Rethinking AI and Music](https://today.ucsd.edu/story/beyond-the-song-generator)——UC 圣地亚哥在构建**与音乐家协作**的 AI 工具而非替代
- [MusicScaffold](https://dl.acm.org/doi/10.1145/3772318.3790440)（ACM）——AI 音乐创作学习框架

### 最大的反面证据

- [Anthropic 自家研究：AI assistance impacts coding skills](https://www.anthropic.com/research/AI-assistance-coding-skills)——用 AI 辅助练习的程序员，对**刚练过的概念**的掌握度**显著下降**——"认知卸载"（cognitive offloading）风险适用于任何技能领域，不只是编程

---

## 个人经验帖（跨领域）

| 案例 | 领域 | 要点 |
|---|---|---|
| [I Switched to Claude for 60 Days](https://ai.plainenglish.io/i-switched-from-chat-gpt-to-claude-for-60-days-heres-what-i-learned-63215022a8b9) | 通用 | 60 天对比实验：ChatGPT 快任务强，Claude 深度理解强 |
| [How I Turned Claude Code Into My AI Agent](https://aimaker.substack.com/p/how-i-turned-claude-code-into-personal-ai-agent-operating-system-for-writing-research-complete-guide) | 写作/研究 | "用过的最好的写作和研究助手"——两个月实测 |
| [My experience: Claude, ChatGPT, and Gemini](https://www.reddit.com/r/AI_Agents/comments/1six77f/my_experience_with_ai_claude_chatgpt_and_gemini) | 自学编程 | 失去家人后用 AI 自学编程 + 建了一个 Memento Mori 应用 |
| [Claude Code: Researcher's Setup Guide](https://paulgp.substack.com/p/getting-started-with-claude-code) | 学术研究 | 研究人员用 Claude Code 读代码/写脚本的工作流 |
| [The Mind Melt: 2 Weeks With ChatGPT & Claude](https://www.starkinsider.com/2025/06/ai-chatgpt-claude-mind-melt.html) | 通用 | 两周深度使用两个助手的体验对比 |

---

## 和我们路径相似的（AI + 图形编程）

| 案例 | 做了什么 | 和我们的异同 |
|---|---|---|
| [Sudo Make Me A Triangle](https://nathany.com/claude-triangle) | 让 Claude 分别在 Vulkan、DX11、DX12 三个 API 里画三角形，对比 AI 在不同图形 API 下的表现 | 方向相反：他让 AI 写代码，我们让 AI 带路、自己写 |
| [Learning Vulkan in the age of AI](https://www.reddit.com/r/vulkan/comments/1wwxjiw/learning_vulkan_and_graphics_programming_in_the) | r/vulkan 社区讨论，有人用 LLM 完整构建了 "Vulkan Game Engine Aurora" | 他证明 AI 能写 Vulkan；我们证明人能借 AI 读懂 Vulkan |
| [Claude Code Renders Triangle in Vulkan and DX12](https://www.linkedin.com/posts/alexanderradkov_gamedev-graphicsprogramming-vulkan-activity-7447077821576904704-1lFf) | LinkedIn 上用 Claude Code + Rust 写 Vulkan 场景/模型结构 | 同上，AI 主笔 |
| [How I learned Vulkan and wrote a game engine](https://edw.is/learning-vulkan) | 三个月纯手写学 Vulkan + 小引擎，无 AI | 对照组——我们的"有 AI 辅助"路径如果砍掉 AI 就长这样 |

## AI 学习方式的争论

| 讨论 | 核心观点 |
|---|---|
| [Cursor is the best way to learn coding?](https://www.reddit.com/r/cursor/comments/1jjrtpq/cursor_is_the_best_way_to_learn_coding_if_you) | 标题肯定，最高赞反对："Cursor teaches you to read code and fix it in place. It does not teach you coding." |
| [Vibe Coding is a Trap in the Long Run](https://www.reddit.com/r/cursor/comments/1j6ufkg/vibe_coding_is_a_trap_in_the_long_run) | 纯让 AI 写不看代码：早期爽、长期废 |
| [Anyone else struggle with how to learn coding in the AI era?](https://news.ycombinator.com/item?id=46866165) | 最实用建议："Ask Claude Code to explain the code until you understand it." |
| [I Used to Tell Beginners to Use Cursor. I Don't Anymore.](https://medium.com/@michaeltutor/i-used-to-tell-beginners-to-use-cursor-i-dont-anymore-2cfe8fe8f7dd) | 教育者从推荐到不推荐——观察到新手基础能力被 AI 侵蚀 |
| [Coding for 20+ years, honest take on AI tools](https://www.reddit.com/r/ClaudeAI/comments/1ra3fiq/coding_for_20_years_here_is_my_honest_take_on_ai) | 老手视角：AI 有用但过度工程化；关键是"知道 AI 什么时候在胡说" |

## 教育研究

| 资源 | 内容 |
|---|---|
| [CS Achievement and Writing Skills Predict Learning with AI](https://dl.acm.org/doi/full/10.1145/3772318.3791666)（ACM） | 正式学术论文：研究 AI 辅助编程学习的效果影响因素 |
| [Beyond Passive AI Rewrites](https://medium.com/google-cloud/beyond-passive-ai-rewrites-and-vibe-coding-in-google-docs-d1ea10a36d4f)（Google Cloud） | 提出"主动学习"模式：贴源码给 AI 让它解释，而非让 AI 直接生成 |
| [Vibe Coding Explained](https://cloud.google.com/discover/what-is-vibe-coding)（Google 官方） | vibe coding 的定义和工具链概览 |

## 我们的模式在光谱上的位置

```
纯手写（无 AI）────── AI 当老师（我们在这）────── AI 当枪手（vibe coding）
     edw.is                ↕                    大多数"用 Cursor 学编程"
                    翻译教程 / 查源码行号 /
                    抓真实数据 / 画图 / 修 bug
                    —— 代码自己写
```

社区共识中"正确但少见"的用法：**AI 当解释器和带路人，不当打字员**。从"推荐 Cursor 给新手"转为"不推荐"的教育者们担心的恰恰是右边那端——用户只会读 AI 生成的代码，不会自己从源码推导。

## 怎么解决 AI"不听话"的问题（全网经验汇总）

### 问题本质

[Your AI Has Infinite Knowledge and Zero Habits](https://medium.com/@elliotJL/your-ai-has-infinite-knowledge-and-zero-habits-heres-the-fix-e279215d478d)（Medium）——核心洞察：LLM 能**知道**最佳实践但不会自动**执行**——"声明性知识 vs 程序性知识"的鸿沟。AI 不是不听话，是它每次回复都是"从零开始做的决定"，没有"习惯"。

[Claude often ignores CLAUDE.md](https://news.ycombinator.com/item?id=46102048)（HN）——讨论的正是这个挫折：**规则文件越长、越杂，遵循率越低**（上下文稀释）。

### 全网总结的解法（按有效性排序）

| 解法 | 说明 | 来源 |
|---|---|---|
| **① 把规则写成文件而不是聊天消息** | CLAUDE.md / .cursorrules——指令从"会衰减的对话上下文"移到"每次会话都读的持久文件" | [Why Claude Ignores Your Instructions](https://dev.to/olivia_craft/why-claude-ignores-your-instructions-and-how-to-fix-it-with-claudemd-1ba1) |
| **② 规则要短、要少、要有优先级** | 写了 200 行规则全被忽略——砍到 4 条关键的就遵守了 | [I Wrote 200 Lines of Rules. It Ignored Them All](https://dev.to/minatoplanb/i-wrote-200-lines-of-rules-for-claude-code-it-ignored-them-all-4639) |
| **③ 写命令式规则，不写描述性规则** | "先读文件，再改代码，然后跑测试"比"遵循最佳实践"有效一万倍 | [What's the one line that made the difference](https://www.reddit.com/r/ClaudeCode/comments/1xeautc/whats_the_one_line_in_your_claudemd_that_made_the) |
| **④ 用代码强制，不靠模型自觉** | 能用 hooks/lint/CI 检查的规则不要写在提示词里——程序不会忘 | [Claude Code Best Practices](https://code.claude.com/docs/en/best-practices)（官方） |
| **⑤ 按目录拆分规则文件** | 主 CLAUDE.md 太长会稀释；拆成子目录的局部规则，AI 读那个目录的文件时自动拉取 | [HN: Claude often ignores CLAUDE.md](https://news.ycombinator.com/item?id=46102048) |
| **⑥ 长会话定期重启** | 上下文越长遵循度越低——重要任务开新会话 | [Why Claude Keeps Ignoring Your Instructions](https://blog.devops.dev/why-claude-keeps-ignoring-your-instructions-and-the-4-line-fix-1920ffa5bd19) |
| **⑦ 给 AI 它看不到的信息** | "你看不到 X，所以做 Y 之前先读 Z"——比"注意 X"有效 | [Claude Code Best Practices](https://code.claude.com/docs/en/best-practices)（官方） |

### 我们已经用过的（对照）

| 我们的做法 | 对应全网共识 |
|---|---|
| CLAUDE.md 铁律（不挖坑/不省略/注释极简） | ① 写文件 ✅ |
| memory/ 目录的 feedback_*.md | ① + ⑤（按主题拆分）✅ |
| "把代码去掉只要注释"之后我记住了"只留注释" | ③ 命令式 ✅ |
| 你发现 build 断了不让我猜→我跑了 configure | ④ 用命令验证 ✅ |
| 会话越来越长后我的错误率上升 | ⑥ 长会话退化——确实存在 |
| 我写代码前自作主张填了内容 | 没做 ⑦——我看不到你的意图，该先问 |

### 没解决的部分（诚实的现状）

全网都没有银弹。以下是我们反复遇到但社区也没有完美解法的：

- **模型"知道规则但在具体时刻不执行"**——规则文件降低了频率，没有归零
- **用户纠正后被理解"字面"而不是"意图"**——我说"只留注释"你理解的是"把代码删了"，不是"把我要写的代码也删了"
- **模型对自己的能力边界判断不准**——不知道自己不知道（配色空间那种，我编了才知道编了）

社区的最终态度：**接受模型的解释有弹性（interpret with latitude），用工具和流程兜底，而不是追求完美服从**。

---

## CLAUDE.md / skill "时看时不看"的问题及解法

### 问题的本质

CLAUDE.md、.cursorrules、skill 文件——AI 把它们当**强上下文**而不是**硬约束**。"考虑你的规则"≠"执行你的规则"。同一条规则有时遵守有时不遵守，取决于当次生成的注意力分配。

| 工具 | 问题 | 来源 |
|---|---|---|
| CLAUDE.md | "建议性的，不是强制" | [Reddit: doesn't follow Claude.md](https://www.reddit.com/r/ClaudeAI/comments/1s4dvmz/claude_code_doesnt_follow_claudemd_file) |
| .cursorrules | "被静默忽略"；`alwaysApply: true` 不可靠 | [Cursor Forum](https://forum.cursor.com/t/cursorrules-not-working-how-to-know-if-they-are-being-used/43147), [Reddit](https://www.reddit.com/r/cursor/comments/1p90f7i/why_does_cursor_ignore_project_rules_set_to) |
| 规则越长越被忽略 | 200 行全被无视，砍到 4 条就遵守 | [dev.to](https://dev.to/minatoplanb/i-wrote-200-lines-of-rules-for-claude-code-it-ignored-them-all-4639) |

### 三层解法（从弱到强）

```
弱 ────────────────────────────────────────── 强

CLAUDE.md / .cursorrules  →  Hooks（代码钩子）  →  CI / 人工审查
（AI"考虑"一下）            （模型外强制执行）     （完全不信任模型）
```

#### 第一层：CLAUDE.md（建议性）

- 写在文件里比写在聊天里强（从"会衰减的上下文"移到"每次会话都读的持久文件"）
- 但 AI 仍然可能"看到了但没执行"
- **规则要短**：只有铁律级别的（3-5 条）才有较高遵循率

#### 第二层：Hooks——真正的强制执行

[官方文档](https://code.claude.com/docs/en/hooks-guide)：Hook 是注册在 `settings.json` 里的 **shell 命令**，在 AI 工作流固定节点触发（PreToolUse / PostToolUse / Stop），**在模型外面跑**——AI 无法忽略。

```json
// settings.json——这是代码，不是提示词
{
  "hooks": {
    "PostToolUse": [{
      "matcher": "Edit",
      "command": "grep -n '违禁词' $CLAUDE_FILE_PATH && exit 1 || exit 0"
    }]
  }
}
```

**Reddit 最高赞建议**：[让 Claude 自己审查你的 CLAUDE.md，把能自动化的转成 hooks](https://www.reddit.com/r/ClaudeAI/comments/1s4dvmz/claude_code_doesnt_follow_claudemd_file)——一旦转了，就从"建议"变成了"物理定律"。

#### 第三层：但 hooks 也不是 100%

- [HN: Claude 4.7 ignoring stop hooks](https://news.ycombinator.com/item?id=47895029)
- [GitHub Issue #54041](https://github.com/anthropics/claude-code/issues/54041)——Claude Code 忽略自己的安全 hooks

Hooks 是**最强可用工具**但不是绝对保证。

### 对我们项目的适用

| 规则 | 现在怎么做 | 能不能转 hook |
|---|---|---|
| 教案违禁词（简化版/TODO/占位/留到/课 X 再） | 我自觉 grep | **✅ PostToolUse**——编辑 lesson*.md 后自动 grep，命中就阻止 |
| 代码注释极简 | CLAUDE.md 铁律 | 部分——行数阈值检查可自动化 |
| 不替用户做决定 | memory 文件 | ❌ 概念性，不适合 hook |
| replace_all 后验证 | memory 文件 | **✅ PostToolUse**——自动对比替换前后 |
| 编译验证 | 手动跑 cmake | **已经是了**（cmake --build 就是 hook 的手工版） |

**结论**：能写成 shell 命令的规则（违禁词/格式/编译）→ 转 hooks；概念性规则（不替用户做决定/不比喻）→ 留在 CLAUDE.md + memory，接受"降低频率但不归零"。



| 维度 | 纯手写（edw.is） | 我们的模式 | vibe coding |
|---|---|---|---|
| AI 角色 | 无 | 带路 + 答疑 + 机械活 | 主笔 |
| 代码谁来写 | 人 | **人** | AI |
| 源码阅读 | 教程 + 官方文档 | **UE 源码逐行** | 不读 |
| 出错时 | 自己 debug | AI 帮定位但人确认 | AI 自己修 |
| 学到的东西 | 基础最扎实 | 基础 + 源码级理解 | 会用工具但不理解原理 |
| 速度 | 最慢 | 中等 | 最快 |
