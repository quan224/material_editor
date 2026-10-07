// ShaderTypes/ShaderValue 全方法测试
// 内容按 tests/shader_types_full_test.md 的代码块填写（.h 已有入口声明）
#include "Demos/ShaderTest/ShaderTypesTest.h"
#define _CRT_SECURE_NO_WARNINGS
#include "Shader/Public/ShaderTypes.h"
#include "Core/Public/StringBuilder.h"
#include "Core/Public/MemStack.h"
#include <cstdio>

using namespace shader;

static FILE* g_out;
#define P(...) do {fprintf(g_out, __VA_ARGS__); printf(__VA_ARGS__);}while(0)  // 文件+控制台双输出

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

void RunShaderTypesFullTest() {
    g_out = fopen("shader_types_test_output.txt", "w");
    // ============================================================
    // [1] 类型表 GetValueTypeDescription —— 29 项全打印
    // ============================================================
    P("==== [1] GValueTypeDescriptions ====\n");
    P("%-16s %-8s %-8s %-4s %s\n", "name", "comp", "comps", "bytes", "");
    for (int i = 0; i <= (int)EValueType::Num; i++) {
        const FValueTypeDescription& d = GetValueTypeDescription((EValueType)i);
        P("%-16s %-8s %-8d %-4d %d\n", d.name, CT(d.comp_type), (int)d.num_components, (int)d.component_size_in_bytes, i);

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
            IsNumericType(t) ? "Y" : "-",
            IsGenericType(t) ? "Y" : "-",
            IsLWCType(t) ? "Y" : "-",
            IsNumericScalarType(t) ? "Y" : "-",
            IsNumericVectorType(t) ? "Y" : "-",
            IsNumericMatrixType(t) ? "Y" : "-");
    }
    P("\n");

    // ============================================================
    // [3] Make 族 —— MakeValueType 两个重载 /
    //     MakeDerivativeType / MakeNonLWCType / MakeConcreteType
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
            VT(MakeNonLWCType(made)),
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
    EValueComponentType cts[] = { EValueComponentType::Float, EValueComponentType::Double,
                                 EValueComponentType::Int, EValueComponentType::Bool,
                                 EValueComponentType::Numeric, EValueComponentType::Void };
    for (auto l : cts) for (auto r : cts)
        P("CombineComponentTypes(%-7s,%-7s) = %s\n", CT(l), CT(r), CT(CombineComponentTypes(l, r)));
    P("CombineTypes(Float3, Float1)  = %s  (标量x向量)\n",
        CombineTypes(FType(EValueType::Float3), FType(EValueType::Float1)).GetName());
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
        CT(FType(EValueType::Float1).GetComponentType(0)),
        CT(FType(EValueType::Float1).GetComponentType(1)),
        CT(FType(EValueType::Float1).GetComponentType(2)),
        CT(FType(EValueType::Float1).GetComponentType(3)));
    P("Double3: nonLWC=%s deriv=%s  Numeric3: concrete=%s\n",
        FType(EValueType::Double3).GetNonLWCType().GetName(),
        FType(EValueType::Double3).GetDerivativeType().GetName(),
        FType(EValueType::Numeric3).GetConcreteType().GetName());
    P("Float3 flatFieldType[0]=%s flatFields=%d\n",
        VT(FType(EValueType::Float3).GetFlatFieldType(0)),
        FType(EValueType::Float3).GetNumFlatFields());

    // 结构分支：就地建一个小结构（Registry 的全面测试见 [9]，这里只测 FType 查询）
    MemStack s_alloc;
    FStructTypeRegistry s_registry(s_alloc);
    const FStructType* st = s_registry.NewType({
        "ST", { {"X", FType(EValueType::Float1)}, {"Y", FType(EValueType::Double1)} }, false });
    FType t_st(st);
    P("FType(struct ST{X:float1,Y:double1}): IsStruct=%d comps=%d flat=%d name=%s deriv=%s\n",
        t_st.IsStruct(), t_st.GetNumComponents(), t_st.GetNumFlatFields(),
        t_st.GetName(), t_st.GetDerivativeType().GetName());
    P("  ctype[0]=%s ctype[1]=%s flatType[0]=%s flatType[1]=%s (逐扁平字段，字段类型可不同)\n\n",
        CT(t_st.GetComponentType(0)), CT(t_st.GetComponentType(1)),
        VT(t_st.GetFlatFieldType(0)), VT(t_st.GetFlatFieldType(1)));


    // ============================================================
    // [6] FValue 构造墙 —— 每个构造器一行：标签+分量
    // ============================================================
    P("==== [6] FValue constructors ====\n");
    auto dump = [](const char* tag, const FValue& v) {
        FStringBuilderBase sb;
        sb.Append(tag).Append(" -> ").Append(v.type_.GetName()).Append(" [");
        for (size_t i = 0; i < v.component.size(); ++i)
            sb.Appendf("%.2f ", v.component[i]._float);
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
        (unsigned long long)(FValue(0.5f).component[0].packed >> 32));
    // GetComponent：标量复制 / 越界兜底
    P("GetComponent: Float1[2]=%.2f (replicate)  Float3[1]=%.2f  Float3[9].packed=0x%llx (oob)\n\n",
        FValue(2.5f).GetComponent(2)._float,
        FValue(1.f, 2.f, 3.f).GetComponent(1)._float,
        (unsigned long long)FValue(1.f, 2.f, 3.f).GetComponent(9).packed);

    // ============================================================
    // [7] 转出族 —— AsFloat/AsFloatScalar/IsZero
    // ============================================================
    P("==== [7] As*/IsZero ====\n");
    {
        FFloatValue af = FValue(1.0, 2.0, 3.0).AsFloat();   // double3 -> float3（跨类型转换）
        P("AsFloat(double3)      = %.1f %.1f %.1f %.1f (expect 1.0 2.0 3.0 0.0)\n", af[0], af[1], af[2], af[3]);
        P("AsFloatScalar(Float3) = %.1f (expect 1.0)\n", FValue(1.f, 9.f, 9.f).AsFloatScalar());
        P("IsZero(0,0,0)=%d  IsZero(0,1,0)=%d (expect 1 0)\n",
            FValue(0.f, 0.f, 0.f).IsZero(), FValue(0.f, 1.f, 0.f).IsZero());
    }
    P("\n");

    // ============================================================
    // [7b] 补充：内存映像往返 + 完整 As 族 + ToString
    // ============================================================
    P("==== [7b] memory image / full As* / ToString ====\n");
    // --- 内存映像往返（FromMemoryImage 修过循环方向 bug，重点核对）---
    {
        FValue vf(1.5f, 2.5f, 3.5f);
        FMemoryImageValue img = vf.AsMemoryImage();
        uint32_t used = 0;
        FValue back = FValue::FromMemoryImage(EValueType::Float3, img.bytes, &used);
        P("roundtrip float3  : size=%u (expect 12)  back=%.2f %.2f %.2f (expect 1.50 2.50 3.50)\n",
            used, back.component[0]._float, back.component[1]._float, back.component[2]._float);

        FValue vd(1.0, 2.0, 3.0, 4.0);
        FMemoryImageValue imgd = vd.AsMemoryImage();
        FValue backd = FValue::FromMemoryImage(EValueType::Double4, imgd.bytes, &used);
        P("roundtrip double4 : size=%u (expect 32)  back=%.2f %.2f %.2f %.2f\n",
            used, backd.component[0]._double, backd.component[1]._double,
            backd.component[2]._double, backd.component[3]._double);

        FValue vi(EValueComponentType::Int, 2);
        vi.component[0]= FValueComponent((int32_t)7);
        vi.component[1] = FValueComponent((int32_t)-9);
        FMemoryImageValue imgi = vi.AsMemoryImage();
        FValue backi = FValue::FromMemoryImage(EValueType::Int2, imgi.bytes, &used);
        P("roundtrip int2    : size=%u (expect 8)   back=%d %d (expect 7 -9)\n",
            used, backi.component[0]._int, backi.component[1]._int);

        FValue vb(true);
        FMemoryImageValue imgb = vb.AsMemoryImage();
        FValue backb = FValue::FromMemoryImage(EValueType::Bool1, imgb.bytes, &used);
        P("roundtrip bool1   : size=%u (expect 1)   back=%s\n",
            used, backb.component[0].AsBool() ? "true" : "false");
    }

    // --- 完整 As 族 ---
    {
        FDoubleValue ad = FValue(1.5f, 2.5f).AsDouble();   // float2 -> double2（宽度提升）
        P("AsDouble(float2)   = %.1f %.1f %.1f %.1f (expect 1.5 2.5 0.0 0.0)\n", ad[0], ad[1], ad[2], ad[3]);
        FIntValue ai = FValue(1.7, 2.9).AsInt();           // double 截断
        P("AsInt(double2)     = %d %d (expect 1 2 trunc)\n", ai[0], ai[1]);
        FBoolValue ab = FValue(0.0f, 1.5f, 0.0f, 0.0f).AsBool();
        P("AsBool(float4)     = %d %d %d %d (expect 0 1 0 0)\n", ab[0], ab[1], ab[2], ab[3]);
        P("AsBoolScalar(0,1)  = %d  AsBoolScalar(0) = %d (expect 1 0)\n",
            FValue(0.0f, 1.0f).AsBoolScalar(), FValue(0.0f).AsBoolScalar());
        DVec4 v4d = FValue(1.f, 2.f, 3.f).AsVector4d();
        P("AsVector4d(float3) = %.1f %.1f %.1f %.1f (expect 1.0 2.0 3.0 0.0)\n",
            v4d.x, v4d.y, v4d.z, v4d.w);
    }

    // --- ToString（FValueComponent::ToString 未写 double 分支，别传 Double——default 是 ME_CHECK(false) 会崩）---
    {
        FStringBuilderBase sb;
        FValue(1.5f, 2.5f, 3.5f).ToString(EValueStringFormat::HLSL, sb);
        P("ToString(float3,HLSL) = %s\n", sb.GetData());

        sb.Reset();
        FValue vi(EValueComponentType::Int, 2);
        vi.component.push_back(FValueComponent((int32_t)3));
        vi.component.push_back(FValueComponent((int32_t)4));
        vi.ToString(EValueStringFormat::HLSL, sb);
        P("ToString(int2,HLSL)   = %s\n", sb.GetData());

        sb.Reset();
        FValue(false, true, false, true).ToString(EValueStringFormat::HLSL, sb);
        P("ToString(bool4,HLSL)  = %s\n", sb.GetData());

        sb.Reset();
        FValue(2.5f).ToString(EValueStringFormat::HLSL, sb);
        P("ToString(float1,HLSL) = %s (double 族不加前缀名)\n", sb.GetData());

        sb.Reset();
        FValueComponent(2.5f).ToString(EValueComponentType::Float, sb);
        P("FValueComponent::ToString(Float) = %s\n", sb.GetData());

        sb.Reset();
        FValueComponent((int32_t)7).ToString(EValueComponentType::Int, sb);
        P("FValueComponent::ToString(Int)   = %s\n", sb.GetData());

        sb.Reset();
        FValueComponent(true).ToString(EValueComponentType::Bool, sb);
        P("FValueComponent::ToString(Bool)  = %s\n", sb.GetData());
    }
    P("\n");

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
        false });
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
        false });
    P("  dedup: same content -> %s\n", mp2 == mp ? "same ptr(OK)" : "NEW(BAD)");

    P("  derivative: %s fields=%d (float3->float3, float->float 都可导)\n",
        mp->derivative_type ? mp->derivative_type->name : "null",
        mp->derivative_type ? (int)mp->derivative_type->fields.size() : -1);

    const FStructType* inner = registry.NewType({   // 嵌套
        "Inner", { {"X", FType(EValueType::Float1)}, {"Y", FType(EValueType::Float1)} }, false });
    const FStructType* outer = registry.NewType({
        "Outer",
        { {"A", FType(EValueType::Float3)}, {"B", FType(inner)} },
        false });
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
    printf("done -> shader_types_test_output.txt (运行目录下)\n");
}