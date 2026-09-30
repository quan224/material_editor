# ShaderTypes.h / ShaderValue.cpp 全方法测试案例

> 目标：把两个文件里已实现的所有方法过一遍，每步有输出、结果可肉眼检查。
> 输出写进 `tests/test_output.txt`（文件输出，不截断）。
>
> 编译运行（在 material_editor_project/ 下）：
> ```bash
> mkdir -p tests
> # 把下面代码存为 tests/shader_types_full_test.cpp 后：
> g++ -std=c++17 -Isrc tests/shader_types_full_test.cpp -o build-mingw/full_test.exe
> ./build-mingw/full_test.exe
> # 然后打开 tests/test_output.txt 看完整输出
> ```

```cpp
// tests/shader_types_full_test.cpp
#include "Shader/Public/ShaderTypes.h"
#include "Core/Public/StringBuilder.h"
#include "Core/Public/MemStack.h"
#include <cstdio>

using namespace shader;

static FILE* g_out;
#define P(...) fprintf(g_out, __VA_ARGS__)   // 所有输出走文件，保证完整

static const char* CT(EValueComponentType t) {   // 分量类型枚举转名字（打印用）
    switch (t) {
    case EValueComponentType::Void:   return "Void";
    case EValueComponentType::Float:  return "Float";
    case EValueComponentType::Double: return "Double";
    case EValueComponentType::Int:    return "Int";
    case EValueComponentType::Bool:   return "Bool";
    case EValueComponentType::Numeric:return "Numeric";
    default: return "?";
    }
}
static const char* VT(EValueType t) { return GetValueTypeDescription(t).name; }  // 值类型转名字

int main() {
    g_out = fopen("tests/test_output.txt", "w");

    // ============================================================
    // [1] 类型表 GetValueTypeDescription —— 29 项全打印
    // ============================================================
    P("==== [1] GValueTypeDescriptions ====\n");
    P("%-16s %-8s %-8s %-4s %s\n", "name", "comp", "comps", "bytes", "");
    for (int i = 0; i <= (int)EValueType::Num; ++i) {
        const FValueTypeDescription& d = GetValueTypeDescription((EValueType)i);
        P("%-16s %-8s %-8d %-4d %d\n", d.name, CT(d.comp_type),
          (int)d.num_components, (int)d.component_size_in_bytes, i);
    }
    P("\n");

    // ============================================================
    // [2] 判定族矩阵 —— IsNumericType/IsGenericType/IsLWCType/
    //     IsNumericScalarType/IsNumericVectorType/IsNumericMatrixType
    //     每个 EValueType 全跑
    // ============================================================
    P("==== [2] predicate matrix ====\n");
    P("%-16s Num Gen LWC Sca Vec Mat\n", "type");
    for (int i = 0; i < (int)EValueType::Num; ++i) {
        EValueType t = (EValueType)i;
        P("%-16s %s   %s   %s   %s   %s   %s\n", VT(t),
          IsNumericType(t)      ? "Y" : "-",
          IsGenericType(t)      ? "Y" : "-",
          IsLWCType(t)          ? "Y" : "-",
          IsNumericScalarType(t)? "Y" : "-",
          IsNumericVectorType(t)? "Y" : "-",
          IsNumericMatrixType(t)? "Y" : "-");
    }
    P("\n");

    // ============================================================
    // [3] Make 族 —— MakeValueType 两个重载 /
    //     MakeDerivativeType / MakeNoneLWCType / MakeConcreteType
    // ============================================================
    P("==== [3] Make* ====\n");
    struct { EValueComponentType ct; int n; } makes[] = {
        {EValueComponentType::Float, 1}, {EValueComponentType::Float, 3},
        {EValueComponentType::Double, 4},{EValueComponentType::Int, 2},
        {EValueComponentType::Bool, 1},  {EValueComponentType::Numeric, 3},
        {EValueComponentType::Float, 16},   // 矩阵
        {EValueComponentType::Float, 5},    // 非法宽度 → 期待 Void
    };
    for (auto& m : makes) {
        EValueType made = MakeValueType(m.ct, m.n);
        P("MakeValueType(%-7s,%-2d)=%-12s deriv=%-10s nonLWC=%-10s concrete=%s\n",
          CT(m.ct), m.n, VT(made),
          VT(MakeDerivativeType(made)),
          VT(MakeNoneLWCType(made)),
          VT(MakeConcreteType(made)));
    }
    P("MakeValueType(Double3, 2)  = %s   (改宽度：FWSVector3 -> ?)\n",
      VT(MakeValueType(EValueType::Double3, 2)));
    P("MakeValueType(Float4x4, 1) = %s   (矩阵改标量)\n\n",
      VT(MakeValueType(EValueType::Float4x4, 1)));

    // ============================================================
    // [4] Combine —— CombineComponentTypes 全组合(6x6) + CombineTypes 场景
    // ============================================================
    P("==== [4] Combine ====\n");
    EValueComponentType cts[] = {EValueComponentType::Float, EValueComponentType::Double,
                                 EValueComponentType::Int, EValueComponentType::Bool,
                                 EValueComponentType::Numeric, EValueComponentType::Void};
    for (auto l : cts) for (auto r : cts)
        P("CombineComponentTypes(%-7s,%-7s) = %s\n", CT(l), CT(r), CT(CombineComponentTypes(l, r)));
    P("CombineTypes(Float3, Float)   = %s  (标量x向量)\n",
      CombineTypes(FType(EValueType::Float3), FType(EValueType::Float)).GetName());
    P("CombineTypes(Double2, Float2) = %s  (LWC吸收float)\n",
      CombineTypes(FType(EValueType::Double2), FType(EValueType::Float2)).GetName());
    P("CombineTypes(Int2, Float2)    = %s  (int+float)\n",
      CombineTypes(FType(EValueType::Int2), FType(EValueType::Float2)).GetName());
    P("CombineTypes(Void, Float3)    = %s  (Void让位)\n",
      CombineTypes(FType(EValueType::Void), FType(EValueType::Float3)).GetName());
    P("CombineTypes(Bool2, Float2)   = %s  (bool和float)\n\n",
      CombineTypes(FType(EValueType::Bool2), FType(EValueType::Float2)).GetName());

    // ============================================================
    // [5] FType 查询族 —— GetName/GetNumComponents/GetNumFlatFields/
    //     GetComponentType(标量特权)/GetFlatFieldType/GetDerivativeType/
    //     GetNonLWCType/GetConcreteType/IsStruct/IsObject/IsAny/IsGeneric
    // ============================================================
    P("==== [5] FType queries ====\n");
    FType t3(EValueType::Float3);
    P("Float3 : name=%s comps=%d ctype[0..2]=%s,%s,%s [3]out=%s\n",
      t3.GetName(), t3.GetNumComponents(),
      CT(t3.GetComponentType(0)), CT(t3.GetComponentType(1)), CT(t3.GetComponentType(2)),
      CT(t3.GetComponentType(3)));
    P("Float标量特权: ctype[0..3]=%s,%s,%s,%s （xyzw全答自己，广播保障）\n",
      CT(FType(EValueType::Float).GetComponentType(0)),
      CT(FType(EValueType::Float).GetComponentType(1)),
      CT(FType(EValueType::Float).GetComponentType(2)),
      CT(FType(EValueType::Float).GetComponentType(3)));
    P("Double3: nonLWC=%s deriv=%s  Numeric3: concrete=%s\n",
      FType(EValueType::Double3).GetNonLWCType().GetName(),
      FType(EValueType::Double3).GetDerivativeType().GetName(),
      FType(EValueType::Numeric3).GetConcreteType().GetName());
    P("Float3 flatFieldType[0]=%s flatFields=%d\n\n",
      VT(FType(EValueType::Float3).GetFlatFieldType(0)),
      FType(EValueType::Float3).GetNumFlatFields());

    // ============================================================
    // [6] FValue 构造墙 —— 每个构造器一行：标签+分量
    // ============================================================
    P("==== [6] FValue constructors ====\n");
    auto dump = [](const char* tag, const FValue& v) {
        FStringBuilderBase sb;
        sb.Append(tag).Append(" -> ").Append(v.type_.GetName()).Append(" [");
        for (size_t i = 0; i < v.componet.size(); ++i)
            sb.Appendf("%.2f ", v.componet[i]._float);
        sb.Append("]");
        return sb.ToString();
    };
    P("%s\n", dump("FValue(1.5f)         ", FValue(1.5f)).c_str());
    P("%s\n", dump("FValue(1f,2f)        ", FValue(1.f, 2.f)).c_str());
    P("%s\n", dump("FValue(1f,2f,3f)     ", FValue(1.f, 2.f, 3.f)).c_str());
    P("%s\n", dump("FValue(1f,2f,3f,4f)  ", FValue(1.f, 2.f, 3.f, 4.f)).c_str());
    P("%s\n", dump("FValue(2.5) double   ", FValue(2.5)).c_str());
    P("%s\n", dump("FValue(1.0,2.0,3.0)  ", FValue(1.0, 2.0, 3.0)).c_str());
    P("%s\n", dump("FValue((int)7)       ", FValue((int32_t)7)).c_str());
    P("%s\n", dump("FValue(true)         ", FValue(true)).c_str());
    P("%s\n", dump("FValue(Vec2)         ", FValue(Vec2(9.f, 8.f))).c_str());
    P("%s\n", dump("FValue(Vec3)         ", FValue(Vec3(7.f, 8.f, 9.f))).c_str());
    P("%s\n", dump("FValue(Vec4)         ", FValue(Vec4(1.f, 2.f, 3.f, 4.f))).c_str());
    P("%s\n", dump("FValue(FType(Float2))", FValue(FType(EValueType::Float2))).c_str());
    P("%s\n", dump("FValue(CompType,2)   ", FValue(EValueComponentType::Double, 2)).c_str());

    // union 位稳定性：float 写入后高 32 位必须为 0（Packed(0u) 纪律）
    P("bit-stability: FValue(0.5f) packed hi32 = 0x%llx (expect 0)\n",
      (unsigned long long)(FValue(0.5f).componet[0].packed >> 32));
    // GetComponent：标量复制 / 越界兜底
    P("GetComponent: Float1[2]=%.2f (replicate)  Float3[1]=%.2f  Float3[9].packed=0x%llx (oob)\n\n",
      FValue(2.5f).GetComponent(2)._float,
      FValue(1.f, 2.f, 3.f).GetComponent(1)._float,
      (unsigned long long)FValue(1.f, 2.f, 3.f).GetComponent(9).packed);

    // ============================================================
    // [7] 转出族 —— AsFloat/AsDouble/AsInt/AsBool/AsFloatScalar/
    //     AsBoolScalar/IsZero（实现后放开注释）
    // ============================================================
    P("==== [7] As*/IsZero (enable after impl) ====\n");
    // FFloatValue af = FValue(1.0, 2.0, 3.0).AsFloat();
    // P("AsFloat(double3) = %.1f %.1f %.1f %.1f\n", af[0], af[1], af[2], af[3]);
    // P("AsFloatScalar(Float3[0]=1) = %.1f\n", FValue(1.f, 9.f, 9.f).AsFloatScalar());
    // P("IsZero(0,0,0)=%d  IsZero(0,1,0)=%d\n",
    //   FValue(0.f,0.f,0.f).IsZero(), FValue(0.f,1.f,0.f).IsZero());
    P("(pending)\n\n");

    // ============================================================
    // [8] 运算函数族 —— Mul/Add/... （实现后放开，示例：材质折叠链）
    // ============================================================
    P("==== [8] value ops (enable after impl) ====\n");
    // FValue tint = FValue(0.8f, 0.2f, 0.1f);
    // FValue brightness = FValue(1.5f);
    // FValue emissive = FValue(0.1f, 0.f, 0.f);
    // FValue lit = Mul(tint, brightness);
    // FValue final = Add(lit, emissive);
    // FStringBuilderBase sb;
    // ToStringHLSL(final, sb);
    // P("tint*brightness+emissive = %s\n", sb.GetData());
    P("(pending)\n\n");

    // ============================================================
    // [9] FStructTypeRegistry —— NewType/去重/FindFieldByName/
    //     嵌套拍平/导数结构/NewExternalType/FindType
    // ============================================================
    P("==== [9] FStructTypeRegistry ====\n");
    MemStack allocator;
    FStructTypeRegistry registry(allocator);

    const FStructType* mp = registry.NewType({
        "MaterialParams",
        { {"Tint",      FType(EValueType::Float3)},
          {"Roughness", FType(EValueType::Float1)} },
        false});
    P("NewType(MaterialParams{Tint:float3,Roughness:float})\n");
    P("  fields=%d flat=%d comps=%d\n",
      (int)mp->fields.size(), (int)mp->flat_field_types.size(), mp->GetNumComponents());
    P("  Tint@slot %d  Roughness@slot %d  FindFieldByName(Nope)=%s\n",
      mp->FindFieldByName("Tint")->component_index,
      mp->FindFieldByName("Roughness")->component_index,
      mp->FindFieldByName("Nope") ? "found(BAD)" : "nullptr(OK)");

    const FStructType* mp2 = registry.NewType({     // 同内容二注
        "MaterialParams",
        { {"Tint",      FType(EValueType::Float3)},
          {"Roughness", FType(EValueType::Float1)} },
        false});
    P("  dedup: same content -> %s\n", mp2 == mp ? "same ptr(OK)" : "NEW(BAD)");

    P("  derivative: %s fields=%d (float3->float3, float->float 都可导)\n",
      mp->derivative_type ? mp->derivative_type->name : "null",
      mp->derivative_type ? (int)mp->derivative_type->fields.size() : -1);

    const FStructType* inner = registry.NewType({   // 嵌套
        "Inner", { {"X", FType(EValueType::Float1)}, {"Y", FType(EValueType::Float1)} }, false});
    const FStructType* outer = registry.NewType({
        "Outer",
        { {"A", FType(EValueType::Float3)}, {"B", FType(inner)} },
        false});
    P("nested Outer{A:float3, B:Inner{X,Y}}: fields=%d flat=%d comps=%d\n",
      (int)outer->fields.size(), (int)outer->flat_field_types.size(), outer->GetNumComponents());

    FType fts(mp);                                    // FType 结构分支
    P("  FType(struct): IsStruct=%d comps=%d name=%s\n",
      fts.IsStruct(), fts.GetNumComponents(), fts.GetName());

    const FStructType* ext = registry.NewExternalType("FSubstrateData");
    P("NewExternalType: fields empty=%s (opaque)\n", ext->fields.empty() ? "YES" : "NO");

    P("FindType(hash of MaterialParams) -> %s\n\n",
      registry.FindType(mp->hash) == mp ? "same ptr(OK)" : "MISMATCH(BAD)");

    // ============================================================
    // [10] EmitDeclarationsCode —— 生成真 HLSL
    // ============================================================
    P("==== [10] EmitDeclarationsCode (HLSL out) ====\n");
    FStringBuilderBase code;
    registry.EmitDeclarationsCode(code);
    P("%s\n", code.GetData());

    // ============================================================
    // [11] MemStack —— Mark/Pop 回收复用
    // ============================================================
    P("==== [11] MemStack ====\n");
    auto mark = allocator.GetMark();
    void* p1 = allocator.Alloc(100);
    allocator.Alloc(200);
    allocator.Pop(mark);
    void* p3 = allocator.Alloc(100);
    P("Pop then Alloc: p3==p1 ? %s (address reuse = batch free works)\n",
      p3 == p1 ? "YES" : "NO");
    P("chunks=%zu\n", allocator.GetNumChunks());

    P("\n==== ALL SECTIONS DONE ====\n");
    fclose(g_out);
    printf("done -> tests/test_output.txt\n");
    return 0;
}
```

## 覆盖清单（对照两个文件的 API）

| 节 | 覆盖的方法 | 所在文件 |
|---|---|---|
| 1 | GValueTypeDescriptions / GetValueTypeDescription / NumValueTypes | 两者 |
| 2 | IsNumericType / IsGenericType / IsLWCType / IsNumericScalar/Vector/MatrixType（全套判定）| ShaderTypes.h |
| 3 | MakeValueType×2 / MakeDerivativeType / MakeNoneLWCType / MakeConcreteType | ShaderValue.cpp |
| 4 | CombineComponentTypes（6×6 全组合）/ CombineTypes（5 场景）| ShaderValue.cpp |
| 5 | FType: GetName / GetNumComponents / GetComponentType（含标量特权+越界）/ GetFlatFieldType / GetNumFlatFields / GetDerivativeType / GetNonLWCType / GetConcreteType / IsStruct 等 | 两者 |
| 6 | FValue 全部 13 个构造器 + union 位稳定性 + GetComponent | ShaderTypes.h |
| 7 | AsFloat / AsFloatScalar / IsZero（**转出族——实现后放开注释**）| 待实现 |
| 8 | Mul / Add / ToStringHLSL（**运算族——实现后放开注释**）| 待实现 |
| 9 | Registry: NewType / 去重 / FindFieldByName / 导数结构 / 嵌套拍平 / NewExternalType / FindType | ShaderValue.cpp |
| 10 | EmitDeclarationsCode（产出可用的 HLSL struct 声明）| ShaderValue.cpp |
| 11 | MemStack: GetMark / Pop / Alloc 复用验证 | Core |

## 看输出怎么核对

跑完后打开 `tests/test_output.txt`，重点核对：

- **[1]** 29 行表：Float1-4/Double1-4/.../Num 顺序和你的枚举一致，矩阵行 comps=16
- **[2]** 矩阵：Numeric1-4 行 Num=Gen=Y；Double 行 LWC=Y；float4x4 行只有 Vec/Mat 特性按你的判定结果——**有疑问的格子就是理解判定语义的地方**
- **[3]** `MakeValueType(Float,5)=void`（非法宽度）、`deriv(FWSVector3)=float3`（LWC 降精度）、`concrete(Numeric3)=float3`
- **[4]** `Combine(Double,Float)=Double`（LWC 吸收）、`Combine(Bool,Float)=?`（bool 的位置看你的实现）
- **[6]** 位稳定性行 `hi32 = 0x0`——非 0 就是 Packed(0u) 纪律破了
- **[9]** `dedup: same ptr(OK)`、`flat=3`（嵌套拍平 A,X,Y）、`comps=5`
- **[10]** 输出的就是能进 shader 的 `struct MaterialParams {...}` + setter
