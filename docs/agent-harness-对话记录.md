# Agent Harness 对话记录

> 记录时间：2026-11-19

---

## Q：harness是什么？

**Harness（测试/运行框架）** 指包住代码或模型、负责驱动它运行的外层框架，核心含义是"套具/马具"——套在实际逻辑外面的那部分。

两种常见语境：

1. **测试 harness**：加载代码、准备环境、调用被测函数、比对结果、报告通过/失败的脚手架代码。被测逻辑本身不算 harness。

2. **Agent harness**（如 Claude Code 这类工具）：指 CLI 外壳——管理对话循环、工具调用、权限审批、上下文压缩的那层运行时。模型是核心，harness 是驱动它的框架。

如果你是在某个具体场景（某段代码/文档）里看到这个词，贴出来我可以给出更精确的解释。

---

## Q：agent harness

**Agent harness** = 把 LLM 包起来、让它能真正"做事"的运行时框架。模型本身只会生成文本，harness 负责其余一切。

核心是一个循环：

```
组装上下文（系统提示 + 对话历史 + 工具结果）
  → 发给模型
  → 模型返回文本 / 工具调用请求
  → harness 执行工具（跑命令、改文件、抓网页…）
  → 结果追加回上下文
  → 重复，直到模型不再调用工具
```

harness 的典型职责：

- **工具层**：定义有哪些工具（读文件、bash、搜索…），校验参数，执行调用
- **权限与安全**：审批危险操作（用户确认、沙箱）
- **上下文管理**：对话超长时压缩/摘要，保证循环能持续
- **周边机制**：hooks、子 agent 调度、任务列表、输出渲染

一句话：**模型决定"下一步做什么"，harness 负责"真的去做并把结果喂回来"**。你现在用的 Claude Code 就是一个 agent harness——CLI 管循环和工具，底下接的模型是 GLM。类似的还有 SWE-agent、OpenCode、aider 等。

---

## Q：国内比较知名的harness有哪些？

国内比较有代表性的几个（按关注度排）：

**1. DeepSeek Harness（dsh）** — 2026 年 8 月开源，MIT 协议
- 继 Codex、Claude Code 之后第三款由大模型原厂推出的编程 agent harness，开源 1 小时破 2.2 万星
- 核心设计是"一切皆插件"：模型、工具、技能、会话、沙箱、UI 全部由 Cordis 插件组合而成
- [知乎上手攻略](https://zhuanlan.zhihu.com/p/2071727612582441366)、[对比评测](https://my.oschina.net/u/9487999/blog/19739544)

**2. iFlow CLI（心流）** — 阿里
- 免费终端形态 + VSCode 插件，针对国内开发者优化，被称作"国内版 Claude Code"
- [量子位报道](https://www.qbitai.com/2025/10/341545.html)

**3. Qwen Code** — 阿里通义
- Apache 2.0 开源，改编自 Gemini CLI，针对 Qwen3-Coder 优化，提供 CLI + SDK
- [2026 全景测评](https://zhuanlan.zhihu.com/p/1999804779141030200)

行业背景：2026 年大家把重心从 Prompt Engineering → Context Engineering → Harness Engineering 迁移，harness 本身成了竞争焦点（[DataCamp 十大开源 harness](https://www.datacamp.com/zh/blog/top-open-source-ai-agent-harnesses)、[知乎 Harness 详解](https://zhuanlan.zhihu.com/p/2025881289593557837)）。

---

## Q：codex是什么？

**Codex** 是 OpenAI 的编程 agent 产品线，和 Claude Code 直接对标。两个阶段：

**早期（2021–2023）**：最初是一个代码生成基础模型（GPT-3 微调版），也是 GitHub Copilot 最初背后的模型，后被 GPT-4 系列取代而弃用。

**现在（2025 起）**：重启为完整的软件工程 agent——
- 形态：终端 CLI（`codex` 命令）+ IDE 扩展 + ChatGPT 内置，能并行处理多个任务
- 底层模型：codex 系列专用模型（如 GPT-5-Codex）
- harness 部分 CLI 已开源（Apache 2.0），可以接其他模型

所以它是"大模型原厂三强 harness"之一：**Anthropic 的 Claude Code、OpenAI 的 Codex、DeepSeek 的 dsh**。三者定位相同——终端里的 agent 循环 + 工具执行 + 权限管理，区别主要在模型能力和 harness 设计细节。

---

## Q：big model和big harness都是在哪些方面优化使用体验？big harness怎么让需求实现更精准？

**分工一句话**：模型优化"每一步想得对不对"，harness 优化"整个过程做得牢不牢"。

### 模型侧的优化方向

| 方面 | 作用 |
|---|---|
| 指令遵循 | 复杂需求不走样、不遗漏 |
| 推理能力 | 拆解任务、规划步骤更合理 |
| 代码知识 | 生成代码的质量、覆盖的语言/框架 |
| 长上下文 | 一次能"看进来"的代码量 |
| 工具调用可靠性 | 参数给对、该调哪个工具不犹豫 |

模型再强也有硬限制：无状态、不能执行、无法验证自己的输出。

### Harness 侧的优化方向

- **执行**：把"说要改"变成"真的改"（文件、shell、浏览器）
- **上下文管理**：超长对话压缩、CLAUDE.md 注入、按需检索——让模型始终看到相关信息
- **安全**：权限审批、沙箱、改前必须先读
- **流程**：计划模式、任务列表、子 agent 并行探索
- **体验**：diff 展示、断点恢复、回滚历史版本

### Harness 怎么让需求实现更精准

核心不是让模型更聪明，而是**把一次性生成变成带反馈的闭环**：

1. **验证回路（最关键）**：改完代码自动跑测试/linter/编译，把真实报错喂回模型再修。模型的猜测让位于环境的客观反馈——这是精准度的最大来源。
2. **强制先读后写**：编辑前必须读文件、old_string 唯一匹配才能替换，结构上杜绝"凭想象改代码"。
3. **计划审批**：先探索代码库、产出实施方案给你确认，理解偏差在写代码**之前**就被拦下。
4. **上下文投喂**：只在恰当时机注入相关文件片段和项目约定，减少对代码库的幻觉。
5. **任务分解**：多步需求拆成可勾选的 todo，子需求不丢。

一句话：**模型负责"猜得准"，harness 负责"错了能发现、发现能改回来"**。

---

## Q：通用harness是什么？怎么做团队的专用harness？

**通用 harness** = 不预设领域的 agent 运行时（Claude Code、Codex、dsh 都是）。工具是通用的（读写文件、跑命令、搜索），对任何代码库都能用，但**不知道你团队的任何事**——不知道你的构建流程、配置管线、内部规范。

### 团队专用 harness 的正确路径：不从零写，分层定制

#### 第一层：约定层（零代码，覆盖 80%）
- **CLAUDE.md / AGENTS.md**：写清项目结构、构建命令、代码规范、禁忌操作，每次会话自动注入
- **Skills**：把重复流程打包，如 `/发布配置` = 校验 xlsx → 跑 Luban → 生成对比报告
- 自定义子 agent、slash 命令

#### 第二层：工具与强制层（少量代码）
- **MCP server**：把内部系统暴露为模型可调用的工具——构建系统、配置管线（Luban 封装）、资产数据库、测试环境
- **Hooks**：强制执行团队规则，如"编辑 .xlsx 后自动跑 Luban 校验，失败直接打回""改 .uasset 前自动备份"
- 这层的本质：**把团队的验证回路固化进 agent 循环**——通用 harness 只有通用的 lint/test 回路，你把领域回路（Luben 校验、UE 资产校验）接进去，精准度立刻上一个台阶

#### 第三层：框架层（真要自研才动）
- 基于开源 harness 二次开发：Codex（Apache 2.0）、dsh（MIT，"一切皆插件"，就是为这种定制设计的）
- 写自己的插件：专用工具、专用上下文策略、专用 UI、权限预设

### 判断标准

| 情况 | 选择 |
|---|---|
| 只是规范和流程知识 | 第一层就够 |
| 有内部系统需要 agent 调用，或有强制校验环节 | 加第二层 |
| 需要定制循环逻辑/UI/分发方式（如给非程序员的内部工具） | 第三层 |

常见错误是一上来就冲第三层。**专用 harness 的价值不在"自己写了框架"，而在"把团队的领域知识和验证回路灌进了循环"**——第一二层就能做到大部分。

---

## Q：这个第三层是什么？以及你这里说的强制先读后写是怎么强制的？怎么控制上下文的投喂

### 第三层是什么

= **改 harness 本身的代码**，而不是用它的配置接口。

一二层你是"配置者"：CLAUDE.md、MCP、hooks 都是框架预留的扩展点，agent 循环本身动不了。第三层你是"框架开发者"：

- fork 开源 harness（Codex Apache 2.0 / dsh MIT），写插件改**循环本身**
- 可改的东西：每次工具调用后插入领域校验步骤、自定义上下文组装策略（每次发给模型什么）、删减工具集、自定义权限模型、换掉整个 UI
- 什么时候才需要：扩展点表达不了你的需求。比如把 harness 包成给策划用的内部工具（改配置表 → 自动校验 → 出报告），要限制工具集、固化流程、换界面——hooks 塞不进去，就得动框架
- dsh 的"一切皆插件"就是为这层设计的：模型、工具、会话、沙箱、UI 全是 Cordis 插件，替换即可

### 强制先读后写怎么强制的

**不是靠提示词说"请先读"，是工具层的前置校验，模型绕不过去**。拿我自己（Claude Code）举例，规则写在 Edit 工具的执行代码里：

1. **没读过的文件，Edit 直接报错**——harness 记录本会话读过的文件，不是听模型声称读过
2. **old_string 必须和文件内容精确匹配且唯一**——模型如果凭想象编了一段"以为在文件里"的代码，匹配失败，编辑被拒
3. **新鲜度校验**——读之后文件被外部改过，再编辑会报 "file modified since read"
4. Write 覆盖已存在但没读过的文件，同样拒绝

关键区别：提示词规则可能被违反，**工具前置条件不可能被绕过**——不真的读文件，编辑这个动作在物理上无法成功。这就是"结构上杜绝"的含义。

### 上下文投喂怎么控制

harness 组装每次发给模型的请求时，控制四个维度：

1. **常驻注入**：系统提示、CLAUDE.md（项目约定）每次都在
2. **按需拉取**：文件内容、搜索结果**只有模型主动调工具后才进入上下文**——不是把整个仓库倒进去。读文件还带 offset/limit，只拉相关段落；grep 结果限条数
3. **输出整形**：工具返回结果先截断/压缩再进上下文（比如命令输出超长时存文件、只给路径）
4. **压缩与隔离**：
   - 对话过长时，把老内容摘要化，带着摘要继续
   - 子 agent 在**独立上下文**里探索，只有结论回到主上下文——搜索过程的噪音不污染主线

原则就是：**默认少给，主动来取；取了整形；撑了压缩**。通用 harness 的这套策略是通用的（相关性靠文件路径/搜索命中），专用 harness 在第三层可以换成领域逻辑——比如"改了哪个 xlsx 就注入对应的 Luban schema"。

---

## Q：那比如codex怎么来搞修改harness框架，修改循环逻辑？

Codex 的做法是开源 + 三层集成接口，按"侵入深度"递增给你三条路（[openai/codex](https://github.com/openai/codex)，Apache 2.0，主体 Rust）：

### 路线一：不改源码，在外部驱动循环（app-server / SDK）

这是官方主推的方式——**循环逻辑你不动，但由你在循环外面接管关键环节**：

- **`codex app-server --stdio`**：以持久会话服务方式启动 harness，你的程序通过协议和它对话。每一轮的**发起、暂停、审批、拦截**都在你的代码里——比如工具调用发生前先过你自己的领域校验（Luben 校验、资产检查），通过了再放行
- **Codex SDK**：程序化编排，把整个 harness 当库用，构建你自己的 agent 产品（自定义 UI、审批队列、工单系统）
- **`codex exec`**：非交互一次性调用，适合 CI 脚本

效果：你可以在"模型请求工具 → 工具执行"之间插入自己的逻辑，**相当于改了循环的行为而没改循环的代码**。

### 路线二：fork 源码，真改循环内部

Apache 2.0 允许任意修改商用。核心在 `codex-rs`（Rust）：

- agent loop、上下文组装、压缩策略、沙箱（Linux Landlock / macOS Seatbelt）都在这里
- 改完自己编译发布内部版本
- 代价：要自己跟进上游更新，改动越深合并越痛苦——所以只做路线一表达不了的事（比如彻底换掉上下文组装策略）

### 路线三：把 Codex 当组件调度

生态里已有的玩法：dsh-codex-app-server 这类项目通过 app-server 把 Codex 整个挂进来当子 agent 运行时——你的专用 harness 负责流程和领域逻辑，重活交给 Codex 的循环。

### 怎么选

| 需求 | 路线 |
|---|---|
| 插入审批/校验环节、自定义 UI | 一（app-server/SDK） |
| 换上下文策略、改循环本身 | 二（fork） |
| 已有自己的框架，只想复用它的执行能力 | 三 |

多数团队专用 harness 停在路线一就够了——36氪 的评论也是这个意思：OpenAI 开放的就是"把智能体拆成零件装进自己产品"的能力（[36氪](https://m.36kr.com/p/3948952877661575)、[知乎：从一次 Turn 看懂 Codex Harness](https://zhuanlan.zhihu.com/p/2074964290659000336)）。

---

## Q：比如说我这里的操作逻辑是每次运行前必须读A资产，就在harness的接口处把这段代码写进去吗？一般怎么写？

对，就是这个意思。但先纠正一个定位：**"每次运行前必须读 A 资产"这种需求是典型的 hook 场景（第二层），不用动框架**。有两种落点，写法不同：

### 落点一：注入——资产直接进上下文，模型没得选

**Claude Code 写法**（`.claude/settings.json` + 一个脚本，零框架代码）：

```json
{
  "hooks": {
    "SessionStart": [{
      "hooks": [{ "type": "command", "command": "python inject_asset.py" }]
    }]
  }
}
```

```python
# inject_asset.py — stdout 会被自动注入到会话上下文
import json
asset = parse_uasset("Assets/A.uasset")   # 你已有的解析器
print(f"[资产A当前状态] 版本:{asset.version} 名字:{asset.name}")
```

每次会话开始，模型的上下文里**已经**有 A 资产的状态了——不是"要求它读"，是它睁眼就在眼前。

### 落点二：拦截——改 A 之前校验，不满足就打回

```json
{
  "hooks": {
    "PreToolUse": [{
      "matcher": "Edit|Write",
      "hooks": [{ "type": "command", "command": "python guard_asset.py" }]
    }]
  }
}
```

```python
# guard_asset.py — 从 stdin 收到工具调用信息，退出码 2 = 阻止执行
import sys, json
event = json.load(sys.stdin)
if event["tool_input"]["file_path"] == "Assets/A.uasset":
    if not already_read_this_session("A.uasset"):
        print("必须先读取A资产才能修改", file=sys.stderr)
        sys.exit(2)   # 编辑被拒绝，原因回到模型，它会先去读
```

### Codex 的写法（app-server 驱动模式）

逻辑相同，但写在你的驱动代码里（伪代码，具体 API 看官方 SDK 文档）：

```python
async def run_task(session, task):
    # 注入：任务开始前先替模型读好
    asset = await read_asset_summary("Assets/A.uasset")
    await session.send_user(f"[注入] 资产A状态：\n{asset}\n\n任务：{task}")

    # 拦截：模型每次请求改文件时过你的闸门
    async def guard(tool_call):
        if touches(tool_call, "A.uasset") and not was_read("A.uasset"):
            return deny("先读A资产")
        return allow()
    session.on_tool_call(guard)
```

### 选择原则

- 只要"**模型开工前 A 的信息必须在场**" → 注入（落点一）
- 要"**没读就改必须失败**" → 拦截（落点二）
- 两个都是配置层/接口层的事，离 fork 框架还远

---

## Q：claude code能否自定义harness？和codex的区别是什么？

能自定义，但和 Codex 走的是两条路线：**Codex 是"全开源、随便 fork"，Claude Code 是"封闭内核 + 丰富的扩展接口"**。

### Claude Code 的自定义层次

| 层次 | 能做什么 |
|---|---|
| CLAUDE.md / skills / 子 agent | 约定、流程打包、并行探索（配置层） |
| Hooks | 会话启动注入、工具调用前拦截/打回 |
| MCP | 接内部系统当工具（Luban、构建、资产库） |
| **Agent SDK** | 把整套 Claude Code harness 当库用，TypeScript/Python 程序化编排循环——启动会话、接管审批、流式收事件 |
| Headless 模式 | `claude -p --output-format stream-json`，无 UI 跑进 CI/脚本 |

到 SDK 这层，你能做到和 Codex app-server 差不多的事：外部驱动循环、插入自己的审批和校验环节。

### 关键区别

| | Claude Code | Codex |
|---|---|---|
| 内核开源 | ❌ 闭源二进制 | ✅ Apache 2.0，Rust 源码全公开 |
| 改循环本身 | 只能在外围驱动/拦截，**循环内核不可改** | fork `codex-rs`，上下文组装、循环逻辑随便动 |
| 重新分发 | 不能改了发内部版本 | 可以编译自己的内部发行版 |
| 接口丰富度 | hooks/MCP/SDK 体系成熟、文档全 | 三层接口（exec/SDK/app-server）+ 源码 |
| 模型绑定 | 深度绑定 Anthropic 模型 | 相对更容易接其他模型 |

### 怎么选

- 你的定制都是"注入、拦截、接工具、程序化编排" → **两者都够用**，Claude Code 的 hooks 生态还更顺手
- 你需要**换掉上下文压缩策略、彻底改循环、做内部发行版** → 只有 Codex（或 dsh 这种全插件化路线）可以
- 一句话：**Claude Code 让你"配置和驾驶" harness，Codex 让你"拥有" harness 源码**

---

## Q：彻底改循环能做到哪些内容？这样做的多吗？

### 彻底改循环能做到什么

owns 源码后，循环里每个环节都可以换。按价值排：

**1. 换上下文组装策略**（最常见动机）
- 通用 harness 按路径/搜索命中判断相关性；你可以换成领域逻辑：改了哪个 xlsx 自动注入对应 Luban schema，改了哪个 uasset 注入引用它的蓝图列表

**2. 换压缩/记忆策略**
- 通用压缩是"把旧对话摘要成一段话"；你可以换成结构化状态：任务进度落盘成 JSON、关键决策单独存档，恢复会话时精确重建，而不是靠一段模糊的摘要

**3. 插入/删除循环步骤**
- 强制检查点：每 N 轮强制重新规划
- 强制验证后才能继续：编辑后没跑过领域校验，循环不许进入下一轮
- 多模型路由：探索用便宜模型、编辑用强模型，在循环里按工具类型分流

**4. 换终止条件**
- 通用："模型不再调工具就停"；你可以："领域校验全绿才许停"——没过就自动把错误喂回去继续

**5. 改 turn 结构本身**
- 循环内嵌多 agent：生成→对抗审查→投票表决
- 并行分支、回滚点、speculation（预测模型下一步要读什么提前加载）

### 做的多吗

**不多，是少数派。** 大致分布：

- **绝大多数团队**停在第一二层（CLAUDE.md + hooks/MCP）——注入、拦截、接工具就解决了 80% 需求
- **做深度 fork 的**主要是两类：拿 harness 做自己产品的公司（内部平台、IDE 厂商），以及有特殊合规/部署要求的大厂内部工具团队
- 原因很实际：**fork 的代价是永久跟上游合并**，harness 迭代很快，改得越深越是包袱

行业也在往"降低 fork 需求"的方向走——dsh 的"一切皆插件"、Codex 的三层接口，本质都是让你不用动源码就能改循环行为。36氪当时的评论就是这个问题："有多少开发者会放弃舒适的聊天框，把智能体拆成零件装进自己的产品"——现在看，愿意拆零件的多，愿意重造零件的少。

**给你的建议不变**：先 hooks + MCP 把验证回路接进去，真撞到"循环行为本身不对"的天花板（比如必须换压缩策略），再考虑 fork。

---

## Q：我写了个mcp，写了个配套的技能，怎么再写个配套的hooks？

三件套的分工先摆正：**MCP 提供"动作"，skill 提供"流程知识"，hook 提供"强制环节"**——前两个模型可以不遵守，hook 是物理约束。

### Hook 的结构

配置放 `.claude/settings.json`（项目级）或 `~/.claude/settings.json`（用户级），也可以在会话里输 `/hooks` 用界面加：

```json
{
  "hooks": {
    "PostToolUse": [{
      "matcher": "mcp__你的server名__工具名",
      "hooks": [{ "type": "command", "command": "node D:/material_editor/hooks/validate.js" }]
    }]
  }
}
```

- `matcher` 匹配工具名，MCP 工具的命名是 `mcp__server__工具`
- 脚本从 **stdin 收 JSON**（含 `tool_name`、`tool_input`、`tool_response`）
- **退出码决定后果**：`0` 放行；`2` 打回——stderr 的内容会喂回模型，它会自己修

### 配套写法（按你的场景）

**PostToolUse：MCP 工具跑完自动验证**——这是和 MCP 最自然的配套，等于给你的工具接上验证回路：

```js
// D:/material_editor/hooks/validate.js
let input = "";
process.stdin.on("data", d => input += d);
process.stdin.on("end", () => {
  const event = JSON.parse(input);
  if (event.tool_name !== "mcp__matedit__save_material") return; // 只管这个工具

  const result = validateMaterial(event.tool_response); // 你的领域校验
  if (!result.ok) {
    console.error("[校验失败] " + result.errors.join("; "));
    process.exit(2);  // 报错回到模型，它会继续调 MCP 修正
  }
});
```

**PreToolUse：前置条件拦截**——比如改材质前必须先备份、危险参数直接拒绝：

```js
// guard.js — 退出码 2 = 这次调用不执行
const event = JSON.parse(input);
if (event.tool_name === "mcp__matedit__save_material") {
  if (!event.tool_input.asset_read) {
    console.error("必须先调用 read_asset 获取当前状态");
    process.exit(2);
  }
}
```

### 事件速查（挑常用的）

| 事件 | 时机 | 典型用途 |
|---|---|---|
| `PreToolUse` | 工具执行前 | 前置校验、备份、拒危险操作 |
| `PostToolUse` | 工具执行后 | 领域校验，错误喂回模型 |
| `UserPromptSubmit` | 用户发消息时 | 注入上下文（stdout 进上下文） |
| `SessionStart` | 会话开始 | 注入资产状态 |
| `Stop` | 模型结束回答 | 收尾检查，没过就打回去继续 |

默认超时 60 秒，校验脚本要快。

把你的 MCP 工具名和校验逻辑发我，我可以直接帮你写出能跑的两个脚本。

---

## Q：我实际希望的是在使用mcp前必须先阅读技能的内容，或者说在用mcp2之前必须先使用mcp1获取前置信息这样的hook绑定流程

可以，这叫**工具调用的顺序依赖**。Hook 是无状态进程，所以核心手法是：**用文件记状态，用 PreToolUse 卡顺序**。两个脚本配对：

### 模式：MCP1 → 标记 → MCP2 检查标记

```json
{
  "hooks": {
    "PostToolUse": [{
      "matcher": "mcp__matedit__get_asset_info",
      "hooks": [{ "type": "command", "command": "node D:/material_editor/hooks/mark.js" }]
    }],
    "PreToolUse": [{
      "matcher": "mcp__matedit__save_material",
      "hooks": [{ "type": "command", "command": "node D:/material_editor/hooks/require.js" }]
    }]
  }
}
```

```js
// mark.js — MCP1 成功跑完后，写会话级标记
let s = ""; process.stdin.on("data", d => s += d);
process.stdin.on("end", () => {
  const e = JSON.parse(s);
  if (e.tool_response?.isError) process.exit(0);   // 失败不标记
  const fs = require("fs"), os = require("os"), path = require("path");
  const dir = path.join(os.tmpdir(), "cc_hooks");
  fs.mkdirSync(dir, { recursive: true });
  // 把 MCP1 的返回也存下来，后面可以做参数一致性校验
  fs.writeFileSync(path.join(dir, `${e.session_id}.step1.json`),
    JSON.stringify({ at: Date.now(), info: e.tool_response }));
});
```

```js
// require.js — 调 MCP2 前查标记，没有就打回
let s = ""; process.stdin.on("data", d => s += d);
process.stdin.on("end", () => {
  const e = JSON.parse(s);
  const fs = require("fs"), os = require("os"), path = require("path");
  const marker = path.join(os.tmpdir(), "cc_hooks", `${e.session_id}.step1.json`);
  if (!fs.existsSync(marker)) {
    console.error("顺序违规：必须先调用 get_asset_info 获取前置信息，才能调用 save_material");
    process.exit(2);   // 调用被阻止，错误回到模型 → 它会先去调 MCP1
  }
});
```

效果：模型直接跳到 MCP2 → 调用被物理拦截 + 收到指引 → 转去调 MCP1 → 标记写入 → MCP2 放行。

进阶：`require.js` 还可以比对 marker 里存的 MCP1 返回值和 MCP2 的入参（比如 asset 版本号一致才放行），这就是真正的数据依赖校验，不只是顺序。

### 变体："必须先读技能"

原理相同，只是检查的对象变成"Skill 工具有没有被调用过"。更省事的替代是**无状态方案——扫 transcript**：事件 JSON 里有 `transcript_path`（完整对话记录 JSONL），脚本直接查历史里有没有对应的 tool_use：

```js
// require_skill.js — 查本会话是否调用过指定技能
const lines = require("fs").readFileSync(e.transcript_path, "utf8").split("\n");
const read = lines.some(l => {
  try { return JSON.stringify(JSON.parse(l)).includes('"skill":"material-workflow"'); }
  catch { return false; }
});
if (!read) { console.error("必须先通过 Skill 工具加载 material-workflow"); process.exit(2); }
```

### 两种状态方案对比

| | 标记文件 | 扫 transcript |
|---|---|---|
| 实现成本 | 低 | 中（要解析 JSONL） |
| 重启/resume 后 | 丢失（tmp 清了就没了） | 仍在（历史都在记录里） |
| 能做数据校验 | ✅（存了 MCP1 返回） | 要重新解析历史内容 |

要求严格就用标记文件 + 扫 transcript 双保险；一般场景标记文件够用。

---

## Q：能不能在输出是加上hook？比如我在skill或者claude里加上了说明希望输出格式简洁明了，不要挤牙膏式回答。但口头描述AI不一定执行，怎么加hook强制执行？

能，用 **Stop hook**——模型每轮回答结束时触发，检查最后一条回复，不合格就**驳回强制返工**。

先诚实定位：文本输出没法像工具调用那样"物理阻止"（话说出来 hook 才看得到），所以这是**事后驳回制**——像给文风上了个 linter，违规成本从"零"变成"必须重写"，执行力完全不是一个量级。

### 写法

```json
{
  "hooks": {
    "Stop": [{
      "hooks": [{ "type": "command", "command": "node D:/material_editor/hooks/style_guard.js" }]
    }]
  }
}
```

```js
// style_guard.js — "简洁、不挤牙膏"翻译成机械可查的规则
const fs = require("fs");
let s = ""; process.stdin.on("data", d => s += d);
process.stdin.on("end", () => {
  const e = JSON.parse(s);
  if (e.stop_hook_active) process.exit(0);   // 已经打回过一次，放行——防死循环

  // 从 transcript 取最后一条 assistant 文本
  const lines = fs.readFileSync(e.transcript_path, "utf8").trim().split("\n");
  let text = "";
  for (let i = lines.length - 1; i >= 0; i--) {
    const m = JSON.parse(lines[i]);
    if (m.type === "assistant" && m.message?.content) {
      text = m.message.content.filter(c => c.type === "text").map(c => c.text).join("\n");
      break;
    }
  }

  const problems = [];
  // 挤牙膏特征：吊胃口的收尾句
  if (/(需要|要不要|是否|还要)我.{0,6}(继续|展开|详细|说明|补充)/.test(text))
    problems.push("禁止挤牙膏式收尾，一次性给完整答案");
  // 冗长：行数上限按你容忍度调
  if (text.split("\n").length > 40) problems.push("超过40行，压缩到要点");
  // 客套开头
  if (/^(好的|当然|没问题|没问题！)[，,！!]/.test(text.trim()))
    problems.push("删掉客套开头，直接进入正题");

  if (problems.length) {
    console.error("[输出规范] " + problems.join("；"));
    process.exit(2);   // 驳回 → 错误回到模型 → 它会重写这轮输出
  }
});
```

### 关键点

1. **"简洁明了"必须翻译成机械规则**——hook 只认得出可计算的：禁用句式、行数上限、必须包含的结构（比如"必须以结论开头"）。把你的口味沉淀成一张规则表，越具体越有执行力
2. **`stop_hook_active` 防死循环**：驳回一次后模型重写，第二次 Stop 就放行，不会再打回。每轮最多强制返工一次，成本可控
3. **挤牙膏的规则最好写**：吊胃口收尾句基本是固定句式，正则一抓一个准
4. 可以再叠一个 `UserPromptSubmit` hook 每轮注入一句提醒（stdout 进上下文）——提示词 + 驳回双保险

局限也要知道：驳回制治的是"违规句式和长度"这类表象，模型重写后是否真的信息密度更高，取决于规则表设计得像不像你心里的"简洁"。规则表就是你的品味的机械化，值得反复调。

---

## Q：我的技能中记录了比较多的前置信息，像是收录的错误经验这一块，相当于一个表来给AI查询用的，这种前置hook怎么加？怎么防止AI加进来超上下文？

先说最容易踩的坑：**Skill 工具加载时是全文进上下文的**。错误经验表放在 skill 里，模型一调用就整表灌入——这就是爆上下文的头号来源。正确结构是拆开：

- **skill 里只留"怎么查"**：表的存在、结构、查询方式（几百字）
- **表本身挪出去**：`error_db.json` 结构化存储，由 hook 按需注入

```json
// D:/material_editor/docs/error_db.json
{ "entries": [
  { "id": 1, "title": "材质保存后丢贴图引用",
    "keywords": ["丢贴图", "贴图引用", "texture missing"],
    "solution": "先调 get_asset_info 刷新引用缓存再保存……（完整过程）" }
]}
```

### 前置 hook：UserPromptSubmit 关键词匹配注入

用户每发一条消息，hook 拿到原文（`e.prompt`），命中关键词才注入对应条目：

```json
{
  "hooks": {
    "UserPromptSubmit": [{
      "hooks": [{ "type": "command", "command": "node D:/material_editor/hooks/inject_exp.js" }]
    }]
  }
}
```

```js
// inject_exp.js — stdout(exit 0) 自动进入上下文
let s = ""; process.stdin.on("data", d => s += d);
process.stdin.on("end", () => {
  const e = JSON.parse(s);
  const db = require("D:/material_editor/docs/error_db.json");

  const hits = db.entries
    .filter(en => en.keywords.some(k => e.prompt.includes(k)))
    .slice(0, 3);                              // ① 上限 3 条

  if (!hits.length) process.exit(0);            // ② 无命中 → 零注入，零成本

  console.log("[历史错误经验] 本条消息命中：\n" + hits.map(h =>
    `- [${h.id}] ${h.title}：${h.solution.slice(0, 120)}…完整方案见 error_db.json #${h.id}`
  ).join("\n"));
});
```

### 防超上下文的四道闸

| 闸 | 做法 | 代价 |
|---|---|---|
| **不常驻** | 表不进 skill、不进 CLAUDE.md，只在命中时注入 | 平时零成本 |
| **限量** | 每次最多注入 N 条（`slice(0,3)`） | 漏掉的靠④兜底 |
| **截断** | 每条只给 120 字摘要 + 编号 | 模型需要时可查全文 |
| **留出口** | 告诉模型"完整条目在 error_db.json #id" | 模型自己 Read，按需拉取 |

### 大表进阶：hook 推 + MCP 拉 双通道

表到几百条以上时，关键词正则会漏（用户描述和记录用语对不上）。加一个 MCP 查询工具（比如 `query_error_db(query)`），让你已有的 MCP 顺手暴露：

- **hook 负责推**：高频、典型错误自动注入摘要（防"忘了查"）
- **MCP 负责拉**：模型怀疑有坑时主动查全文（防"没推到"）
- skill 里写一句规则："遇到报错先查 query_error_db"——这句话很短，不会爆

这就是之前说的"默认少给，主动来取"的完整版：**推送管命中率，拉取管覆盖率，两头都限了量**。

---

## Q：skill 里写一句规则："遇到报错先查query_error_db" 这句规则AI不是不会百分百执行吗？

对，你抓到要害了——**那句是提示词层规则，概率执行，这正是它和 hook 的本质区别**。所以完整的方案里，那句规则只是"兜底提示"，真正的保障应该这么补：

### 轻方案：报错瞬间，hook 强喂指令（PostToolUse）

模型看到报错的时机是工具返回时，就在这个瞬间拦截：

```json
{
  "hooks": {
    "PostToolUse": [{
      "matcher": "Bash|mcp__matedit__.*",
      "hooks": [{ "type": "command", "command": "node D:/material_editor/hooks/on_error.js" }]
    }]
  }
}
```

```js
// on_error.js — 检测到报错特征时，exit 2 的 stderr 会作为必须回应的信息喂回模型
let s = ""; process.stdin.on("data", d => s += d);
process.stdin.on("end", () => {
  const e = JSON.parse(s);
  const r = JSON.stringify(e.tool_response ?? {});
  const isError = e.tool_response?.isError
    || /exit code: [1-9]/.test(r)
    || /(Error|ERROR|失败|Exception)/.test(r);
  if (isError) {
    console.error("[强制流程] 检测到报错：必须先调用 query_error_db 查询历史经验，再动手修复");
    process.exit(2);   // 模型收到的不是可忽略的提示，是必须处理的反馈
  }
});
```

这已经把"概率提示"升级成"**错误通道里的强制指令**"——模型对喂回的错误信息响应率远高于静态提示词，但仍不是 100%。

### 重方案：门禁——没查过，修复动作物理不可执行（PreToolUse + 标记文件）

和你之前 MCP1→MCP2 是同一个模式，这才是 100%：

```js
// 三个 hook 配合：
// ① PostToolUse(报错类工具) → on_error.js 写标记 pending_query
// ② PostToolUse(query_error_db) → 清除标记
// ③ PreToolUse(修复类工具) → require_query.js：
const marker = path.join(os.tmpdir(), "cc_hooks", `${e.session_id}.pending`);
if (fs.existsSync(marker)) {
  console.error("存在未处理的报错：必须先调用 query_error_db，本次调用被拒绝");
  process.exit(2);   // save_material / 编辑动作直接不执行
}
```

效果：报错发生 → 模型想直接改 → **调用被拒** → 只能先查 error_db → 标记清除 → 修复放行。跳步在物理上不可能。

### 三层执行力对照

| 层 | 机制 | 执行率 | 成本 |
|---|---|---|---|
| skill 里写规则 | 提示词 | 概率性，最低 | 零 |
| PostToolUse 强喂指令 | 错误反馈通道 | 高 | 每次报错多一轮交互 |
| PreToolUse 门禁 + 标记 | 物理拦截 | **100%** | 实现最重，流程最死 |

工程上的标准做法是**叠着用**：门禁只锁最关键的修复工具（防止改坏资产），强喂指令覆盖一般报错，提示词规则做氛围。执行率要求多高，就往上加多重的层。

---

## Q：hooks只能放到.claude目录里面吗？

不是。要分两样东西：**配置**（声明 hook 挂哪个事件）和**脚本**（实际执行的 js/py 文件）。

### 配置位置（固定几个选项）

| 位置 | 作用域 | 典型用途 |
|---|---|---|
| `项目/.claude/settings.json` | 项目级，进版本库 | 团队共享的 hook |
| `项目/.claude/settings.local.json` | 项目级，不进库 | 个人临时调试 |
| `C:\Users\admin\.claude\settings.json` | 用户级，全局 | 所有项目生效 |
| 企业管理策略 `managed-settings.json` | 强制下发，优先级最高 | 公司统一管控 |

也可以在会话里输 `/hooks` 用界面配置，本质是写进上面这些文件。优先级：企业 > 项目 local > 项目 > 用户，逐层合并。

### 脚本位置：随便放

`command` 就是一条普通 shell 命令，写绝对路径即可，脚本想在哪就在哪：

```json
{ "type": "command", "command": "node D:/material_editor/hooks/validate.js" }
```

之前给你的示例一直是 `D:/material_editor/hooks/` —— 和你的 MCP、skill 代码放一起更好维护，`.claude` 里只留一条配置指过去。

顺带区分：**skill** 才是必须放 `.claude/skills/`（项目）或 `~/.claude/skills/`（个人）的——那是因为 harness 要扫描发现它；hook 脚本没有这个发现机制，全靠配置里的路径，所以自由。

---

## Q：那我作为一个技能，这个技能给别人用，又希望别人能设置上hook。怎么改别人的setting？

标准答案：**别改别人的 settings——把技能+hook 打成 plugin，装上自动生效**。

### 正解：Plugin（官方的分发单元）

Plugin 就是为"技能+hook+MCP 打包给别人"设计的，hooks 定义随插件自带，安装后自动注册，用户不用碰 settings.json：

```
material-workflow/
├── .claude-plugin/
│   └── plugin.json          # {"name": "material-workflow", "version": "1.0.0"}
├── hooks/
│   ├── hooks.json           # 和 settings.json 里同格式的 hooks 配置
│   └── scripts/
│       ├── guard.js         # 脚本随包走，路径写相对包内的逻辑
│       └── validate.js
└── skills/
    └── material-workflow/
        └── SKILL.md
```

```json
// hooks/hooks.json — 格式和 settings 里的完全一样
{
  "hooks": {
    "PreToolUse": [{
      "matcher": "mcp__matedit__save_material",
      "hooks": [{ "type": "command", "command": "node \"$CLAUDE_PLUGIN_ROOT/hooks/scripts/guard.js\"" }]
    }]
  }
}
```

`$CLAUDE_PLUGIN_ROOT` 是插件安装后的根路径变量，脚本引用它就不用担心装到哪了。

分发方式：
- 内部团队：把 plugin 目录放共享盘/git，用户 `/plugin install <本地路径>` 装入
- 对外：发到 marketplace，用户 `/plugin marketplace add <repo>` 后安装

装完 hook、skill 一起到位，卸载也干净——这是它优于手改 settings 的核心：**声明式带入，可整体卸载**。

### 备选：skill 带安装脚本

如果不想引入 plugin 机制，就在技能包里放一个安装脚本，**让用户自己跑**（SKILL.md 开头写明"首次使用先运行 setup"）：

```js
// setup.js — node setup.js
const fs = require("fs");
const p = require("os").homedir() + "/.claude/settings.json";
const s = fs.existsSync(p) ? JSON.parse(fs.readFileSync(p, "utf8")) : {};
fs.copyFileSync(p, p + ".bak");                        // 备份
s.hooks = deepMerge(s.hooks, require("./my-hooks.json")); // 只合并 hooks 字段
fs.writeFileSync(p, JSON.stringify(s, null, 2));
console.log("hooks 已安装，重启会话生效");
```

### 原则

悄悄改别人的 settings 是大忌（用户会懵、会冲突、卸不干净）。两条正路都是**用户主动执行一个动作**（装 plugin / 跑 setup），区别是 plugin 是官方机制、生命周期完整，setup 脚本是土办法、简单但散装。要分发给多人，直接上 plugin。

---

## Q：这也是很多人把强流程放到 MCP/外部脚本层。怎么放外面？

> 背景：Claude Code 和 Codex 的上下文/机制不通用——会话历史、记忆、skills、hooks 各自独立，只有 MCP server 本身、约定文件（CLAUDE.md/AGENTS.md）和普通数据文件可共享。所以强流程放 hooks 层换 harness 就要重写。

核心思路一句话：**门禁不写在 harness 的配置里，写进工具自己的身体里**——MCP server 内部记状态、查前置条件，不满足就让这次调用"失败"。这样不管哪个 harness 来调，纪律都跟着工具走。

### 做法：MCP server 内部当门卫

之前 hooks 的方案是"外面拦"（harness 在工具执行前拦截），现在改成"里面拒"（工具自己拒绝干活）：

```js
// 你的 MCP server（Node）
// 每个 MCP 连接 ≈ 一个会话，状态就放连接级，比临时文件还干净
const state = new Map();  // connId -> { assetInfoRead: bool, version: ... }

// MCP1：读资产信息（顺便记状态）
server.tool("get_asset_info", { assetId: z.string() }, async ({ assetId }, connId) => {
  const info = await readAsset(assetId);
  state.set(connId, { assetInfoRead: true, version: info.version });
  return json(info);
});

// MCP2：保存（内部查门禁）
server.tool("save_material", { material: z.any(), baseVersion: z.string() }, async (args, connId) => {
  const s = state.get(connId);
  if (!s?.assetInfoRead)
    return err("顺序违规：必须先调用 get_asset_info 获取当前状态");
  if (args.baseVersion !== s.version)
    return err("版本冲突：你基于旧版本改的，重新 get_asset_info 后再保存");

  await save(args.material);        // 校验全过才真正执行
  return ok("saved");
});

function err(msg) {   // isError 标记 → harness 会把这条作为工具错误喂回模型 → 它自己去补前置步骤
  return { isError: true, content: [{ type: "text", text: msg }] };
}
```

效果和 hooks 门禁完全等价：模型跳步 → `save_material` 返回错误 → 错误进上下文 → 模型转去调 `get_asset_info` → 再回来保存。**区别是这个纪律长在工具上**：Claude Code 调是这样，Codex 调是这样，iFlow 调还是这样，甚至人直接调 MCP 也逃不掉。

### 外部脚本层是同一思想的另一种载体

强流程放进**实际干活的脚本**里（MCP 调的底层脚本，或 agent 用 Bash 直接跑的脚本）：

```js
// save_material.js — 脚本自己查前置条件，谁跑都一样
const marker = ".state/asset_info.json";
if (!fs.existsSync(marker) || tooOld(marker, 5 * 60))
  die("必须先跑 get_asset_info.js 刷新状态（5分钟内有效）");
if (argv.baseVersion !== readVersion(marker))
  die("版本冲突：基于过期状态，拒绝保存");
backup(); save(); verify();   // 全过了才动真格
```

### 两种放置的取舍

| | hooks 层 | MCP/脚本层 |
|---|---|---|
| 拦截时机 | 调用前（调用都不发生） | 调用发生、动作不执行 |
| 换 harness | 重写（各家机制不同） | **零成本跟随** |
| 对人类生效 | ❌ | ✅（直接跑脚本同样被拦） |
| 能管的事 | 一切工具（Bash/Edit/…）、输出风格、上下文注入 | 只能管自己的工具 |
| 隐蔽动作 | 可静默拦截/改写 | 只能报错返回 |

### 实践分工

- **领域动作的门禁**（先读后写、版本一致、备份校验）→ 放 MCP/脚本层，可移植、人机通用
- **harness 特有的纪律**（输出风格驳回、拦 Bash/Edit 这类通用工具、SessionStart 注入）→ 只能放 hooks，换工具时接受重写

你之前的"读 A 资产才能改"如果挪进 MCP server，就是上面第一段代码的样子——hooks 那套可以退役，Codex 那边也不用再想办法补等价物了。

---

## Q：那强制读技能查错误经验这一块也能放到harness外面吗？

能，但形态要变——**从"强制它读"变成"读不读都逃不掉"**。harness 外没有"会话开始注入"这种 push 手段（MCP 只能被模型主动调），所以思路是把知识变成工具行为的一部分：

### 手法一：错误经验内联进失败返回（最优雅）

不强制"报错后去查"，而是**报错发生时查询已经在工具里发生了**：

```js
// MCP server 里，任何工具失败时
async function fail(toolName, error) {
  const hits = searchErrorDb(toolName, error);   // 查错经验表
  return {
    isError: true,
    content: [{ type: "text", text:
      `操作失败：${error.message}\n` +
      (hits.length ? `历史经验：\n${hits.map(h =>
        `- [${h.id}] ${h.title}：${h.solution}`).join("\n")}` :
        "（错误库无记录，已自动收录本次报错）`) }]
  };
}
```

模型收到报错的**同时**就拿到药方，连第二次调用都省了。比 hooks 那套"拦住、打回、逼它去查"路径更短。附带好处：新错误自动落库，经验表越用越厚。

### 手法二：技能知识变成校验逻辑（违规时才投递）

技能里的规则不再是"要求提前读的文档"，而是**工具内部的校验代码**——违反哪条，报错里引用哪条：

```js
// 技能里写着"贴图路径必须走 /Game/Assets/ 而非绝对路径"
server.tool("save_material", {...}, async (args) => {
  if (!args.texturePath?.startsWith("/Game/Assets/"))
    return fail("save_material", {
      message: `贴图路径违规：${args.texturePath}\n` +
        `规则：必须使用 /Game/Assets/ 工程内路径（原因：打包时绝对路径会丢引用）` });
  ...
});
```

这就是**延迟投递**：不需要模型提前背规则，违规瞬间规则精确送达。技能文档可以退化成给人看的说明书。

### 手法三：查询门禁（保底）

实在要强制"先查再修"的动作顺序，用上轮的标记模式：修复类工具内部检查 `query_error_db` 有没有被调用过，没有就拒绝——纯 MCP 内部状态，跨 harness。

### 诚实的边界

| 原 harness 能力 | 外置后 |
|---|---|
| UserPromptSubmit 关键词注入（用户话音刚落就推送经验） | ❌ 没有等价物，MCP 拉不动 |
| SessionStart 注入资产状态 | ❌ 同上 |
| 拦 Bash/Edit 等通用工具 | ❌ MCP 管不到，只能管自己的工具 |
| Stop 驳回输出风格 | ❌ 纯 harness |

两个缓解：
- **门禁化的第一步工具顺带携带上下文**：`get_asset_info`（反正必须先调）的返回里带上资产状态+当前步骤的操作要点——强制调用变成了强制注入
- **让 MCP 成为唯一合理的写路径**：危险操作只通过 MCP 暴露，模型没有绕过门禁的动机（Bash 硬改文件这种，靠 hooks 或接受风险）

**总结成一句**：harness 内靠"拦截+注入"管过程，harness 外靠"失败响应携带知识+违规即拒"管结果——前者管得细但绑平台，后者管得粗但走到哪都在。
