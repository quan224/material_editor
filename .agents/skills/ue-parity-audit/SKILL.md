---
name: ue-parity-audit
description: 凡涉及「对照 UE」的教案/代码内容，必须先产出 UE 侧全集清单（grep 源码逐项列出），再逐项映射教学版覆盖，打叉项当场裁决补齐，最后才动手写内容。防止任何省略简化暗处藏身。
---

# UE 对照审计流程

## 何时触发

- 写/改教案中任何声称「对照 UE」「对齐 UE」的内容
- 实现任何以 UE 结构为原型的代码（类型系统/表达式树/chunk/算子/序列化……）
- 用户问「这里和 UE 有什么差别」「你是不是又省略了」

## 流程（顺序不可变）

### 第 1 步：列 UE 全集

用 grep 在本地 UE 源码（`D:/material_editor/UE5/UnrealEngine-release/Engine/`）列出该主题的**全部成员**，附文件:行号。常见主题的查询：

| 主题 | 查询 |
|---|---|
| 类型位 | `grep -n "1u <<\|1ull <<" Engine/Source/Runtime/Engine/Public/MaterialValueType.h` |
| 表达式树子类 | `grep -n "^class FMaterialUniformExpression" Engine/Source/Runtime/Engine/Private/Materials/MaterialUniformExpressions.h` |
| chunk 字段 | 读 `HLSLMaterialTranslator.h:83-174` |
| 编译器算子 | `grep -n "int32 FHLSLMaterialTranslator::" HLSLMaterialTranslator.cpp` |
| preshader opcode | 读 `Engine/Public/Shader/Preshader.h` 的 EPreshaderOpcode |
| 表达式节点 | `ls Engine/Source/Runtime/Engine/Public/Materials/MaterialExpression*.h` |

### 第 2 步：逐项映射

对 UE 全集每一项，查教学版覆盖（教案 + 代码）：

```bash
# 逐项 grep 教案和代码，输出 → 文件 或 MISSING
for item in <UE全集>; do
  hits=$(grep -rl "$item" MaterialEditor/docs/lessons/*.md material_editor_project/src 2>/dev/null | head -1)
  echo "$item → ${hits:-MISSING}"
done
```

产出三档清单：
- ✅ 已覆盖（注明教案哪节/哪课）
- ⚠️ 仅提及无实现内容（半缺失——**最容易漏判的档**，要点开验证是否真有实现内容）
- ❌ MISSING

### 第 3 步：MISSING 项直接补齐

按两条铁律：不替用户做决定（全补，不出「建议不做」方案）、不省略（每项都有实现内容，不是一句「按模板追加」）。补齐分布到对应课（数据结构→课6、算子→课7、生成→课8、渲染→课14/15、参数/DDC/Substrate→课20）。

### 第 4 步：复审

```bash
# 重跑第 2 步的循环，MISSING 数必须为 0
# 违禁词检查（挖坑句式）
grep -n "简化版\|TODO\|占位\|留到\|课 [0-9] 再\|课 [0-9] 完善" <改动的教案>
# 「按模板追加」「同模板」这类表述必须伴随完整模板代码，否则视为半缺失
```

## 已完成的审计基线（2026-08-27，commit 6edf803）

- `EMaterialValueType` 全部位（含 UInt/Bool/Execution/CubeArray/SVT/VTPageTableResult/Unexposed）✅
- `FMaterialUniformExpression` 44 子类 ✅（数学族→课6/7、参数族→课20、贴图元数据族→课14）
- `FShaderCodeChunk` 逐字段 ✅
- preshader 字节码（opcode 子集 + 栈 VM）✅ 课20
- External/Virtual 完整采样路径 ✅
- Add/Sub 建树语义（UE 原版，非立即折叠）✅

下次审计直接从此基线增量查，不重做已完成项。
