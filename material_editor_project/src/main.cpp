#include <cstdio>
#include <string>
#include <vector>

#include "Reflection/Public/Reflection.h"
#include "Reflection/Public/ReflectionMacros.h"
#include "Expression/Public/Expression.h"
#include "MaterialGraph/Public/Types.h"
#include "Core/Public/MathTypes.h"
#include "Core/Public/Logger.h"

// ============================================================
// 测试用 Expression 子类：用反射宏声明 3 个不同类型的字段
// ============================================================
class TestExpr : public Expression
{
public:
    float      scale  = 1.0f;
    Vec3       color  = Vec3(1.0f, 0.0f, 0.0f);
    bool       enabled = true;
    std::string label  = "default";

    // === 反射注册 ===
    ME_BEGIN_CLASS(TestExpr)
        ME_DISPLAY_NAME("Test Expr")
        ME_CATEGORY("Test")
        ME_CATEGORY_COLOR("#FF8800")
        ME_FIELD(TestExpr, scale,   1.0f)
        ME_FIELD(TestExpr, color,   Vec3(1.0f, 0.0f, 0.0f))
        ME_FIELD(TestExpr, enabled, true)
        ME_FIELD(TestExpr, label,   std::string("default"))
    ME_END_CLASS(TestExpr)

    // === 其余纯虚函数（测试不需要它们，留空实现）===
    std::vector<ExpressionPinDesc> GetInputPins()  const override { return {}; }
    std::vector<ExpressionPinDesc> GetOutputPins() const override { return {}; }
    std::vector<int32_t> Compile(MaterialCompiler*, Node*) const override { return {}; }
};

// ============================================================
// 简单的测试断言宏
// ============================================================
static int g_tests    = 0;
static int g_failures = 0;

#define TEST_CHECK(cond, fmt, ...) do {                                  \
    bool _c = (cond);                                                    \
    std::printf("[%s] " fmt "\n", _c ? "PASS" : "FAIL", ##__VA_ARGS__);  \
    g_tests++;                                                            \
    if (!_c) g_failures++;                                               \
} while(0)

#define TEST_SECTION(name) do {                                           \
    std::printf("\n=== %s ===\n", name);                                  \
} while(0)

// ============================================================
// 测试用例
// ============================================================

// 测试 1：ClassDesc 元数据
static void Test_ClassMetadata()
{
    TEST_SECTION("Test 1: ClassDesc metadata");
    TestExpr obj;
    const reflection::ClassDesc* desc = obj.GetClassDesc();

    TEST_CHECK(desc != nullptr, "GetClassDesc() 返回非空");
    TEST_CHECK(desc->type_name    == "TestExpr",   "type_name = '%s'",   desc->type_name.c_str());
    TEST_CHECK(desc->display_name == "Test Expr",  "display_name = '%s'", desc->display_name.c_str());
    TEST_CHECK(desc->category     == "Test",       "category = '%s'",    desc->category.c_str());
    TEST_CHECK(desc->category_color == "#FF8800",  "category_color = '%s'", desc->category_color.c_str());
}

// 测试 2：字段枚举（GetParameters）
static void Test_FieldEnumeration()
{
    TEST_SECTION("Test 2: Field enumeration via GetParameters");
    TestExpr obj;
    auto params = obj.GetParameters();

    TEST_CHECK(params.size() == 4, "字段数量 = %zu (期望 4)", params.size());
    TEST_CHECK(params[0].name == "scale",   "字段[0].name = '%s'", params[0].name.c_str());
    TEST_CHECK(params[1].name == "color",   "字段[1].name = '%s'", params[1].name.c_str());
    TEST_CHECK(params[2].name == "enabled", "字段[2].name = '%s'", params[2].name.c_str());
    TEST_CHECK(params[3].name == "label",   "字段[3].name = '%s'", params[3].name.c_str());

    TEST_CHECK(params[0].type == reflection::FieldType::Float,  "scale 类型 = Float");
    TEST_CHECK(params[1].type == reflection::FieldType::Float3, "color 类型 = Float3");
    TEST_CHECK(params[2].type == reflection::FieldType::Bool,   "enabled 类型 = Bool");
    TEST_CHECK(params[3].type == reflection::FieldType::String, "label 类型 = String");
}

// 测试 3：默认值（GetParameter 读默认值）
static void Test_DefaultValues()
{
    TEST_SECTION("Test 3: Default values");
    TestExpr obj;
    auto scale_json   = obj.GetParameter("scale");
    auto enabled_json = obj.GetParameter("enabled");
    auto label_json   = obj.GetParameter("label");

    TEST_CHECK(scale_json.is_number()   && scale_json.get<float>() == 1.0f,      "scale 默认 = 1.0");
    TEST_CHECK(enabled_json.is_boolean() && enabled_json.get<bool>() == true,     "enabled 默认 = true");
    TEST_CHECK(label_json.is_string()   && label_json.get<std::string>() == "default", "label 默认 = 'default'");
}

// 测试 4：SetParameter → GetParameter 往返
static void Test_SetGetRoundTrip()
{
    TEST_SECTION("Test 4: SetParameter / GetParameter round-trip");
    TestExpr obj;

    // 改 float
    obj.SetParameter("scale", 3.14f);
    TEST_CHECK(obj.scale == 3.14f, "scale 改为 3.14 (实际 = %f)", obj.scale);
    TEST_CHECK(obj.GetParameter("scale").get<float>() == 3.14f, "GetParameter 回读 scale = 3.14");

    // 改 Vec3
    obj.SetParameter("color", nlohmann::json::array({0.5f, 0.25f, 0.125f}));
    auto c = obj.color;
    TEST_CHECK(c.x == 0.5f && c.y == 0.25f && c.z == 0.125f,
               "color 改为 (0.5, 0.25, 0.125) (实际 = %f, %f, %f)", c.x, c.y, c.z);

    // 改 bool
    obj.SetParameter("enabled", false);
    TEST_CHECK(obj.enabled == false, "enabled 改为 false");

    // 改 string
    obj.SetParameter("label", std::string("hello"));
    TEST_CHECK(obj.label == "hello", "label 改为 'hello' (实际 = '%s')", obj.label.c_str());
}

// 测试 5：未知字段应该安全返回（不崩溃）
static void Test_UnknownField()
{
    TEST_SECTION("Test 5: Unknown field safety");
    TestExpr obj;
    auto val = obj.GetParameter("nonexistent");
    TEST_CHECK(val.is_null(), "未知字段 GetParameter 返回 null");

    obj.SetParameter("nonexistent", 42);   // 不应该崩溃
    TEST_CHECK(true, "未知字段 SetParameter 不崩溃");
}

// 测试 6：静态 ClassDesc 在多次访问间是同一个实例
static void Test_StaticInstance()
{
    TEST_SECTION("Test 6: ClassDesc 是函数静态单例");
    TestExpr a, b;
    const reflection::ClassDesc* da = a.GetClassDesc();
    const reflection::ClassDesc* db = b.GetClassDesc();
    TEST_CHECK(da == db, "两个实例的 ClassDesc* 指向同一对象 (a=%p, b=%p)", (void*)da, (void*)db);
}

// 测试 7：手动构建 ClassDesc + 注册到 Registry
static void Test_RegistryManualRegister()
{
    TEST_SECTION("Test 7: Registry 手动注册 + Find");
    reflection::Registry& reg = reflection::Registry::GetInstance();

    reflection::ClassDesc desc;
    desc.type_name    = "ManualType";
    desc.display_name = "Manual";
    desc.category     = "Test";
    reg.RegisterClass(desc);

    const reflection::ClassDesc* found = reg.Find("ManualType");
    TEST_CHECK(found != nullptr,                            "Find('ManualType') 找到");
    TEST_CHECK(found->display_name == "Manual",            "display_name = '%s'", found->display_name.c_str());

    const reflection::ClassDesc* not_found = reg.Find("DoesNotExist");
    TEST_CHECK(not_found == nullptr,                       "Find('DoesNotExist') 返回 nullptr");
}

// 测试 8：Accessor 直接调用（不走宏）
static void Test_AccessorDirect()
{
    TEST_SECTION("Test 8: Accessor 直接调用（绕过宏）");
    struct Foo {
        float  f;
        int    i;
        Vec3   v;
    };
    Foo foo;
    foo.f = 2.5f;
    foo.i = 42;
    foo.v = Vec3(10.0f, 20.0f, 30.0f);

    nlohmann::json jf, ji, jv;
    reflection::Accessor<float>::toJson(&foo, offsetof(Foo, f), jf);
    reflection::Accessor<int32_t>::toJson(&foo, offsetof(Foo, i), ji);
    reflection::Accessor<Vec3>::toJson(&foo, offsetof(Foo, v), jv);

    TEST_CHECK(jf.get<float>() == 2.5f,                       "Accessor<float>::toJson f = 2.5");
    TEST_CHECK(ji.get<int32_t>() == 42,                       "Accessor<int32_t>::toJson i = 42");
    TEST_CHECK(jv[0].get<float>() == 10.0f &&
               jv[1].get<float>() == 20.0f &&
               jv[2].get<float>() == 30.0f,                   "Accessor<Vec3>::toJson v = (10,20,30)");
}

// 上面 Test 8 第 8 测试有个手误（Foo2），改写如下：
static void Test_AccessorFromJson()
{
    TEST_SECTION("Test 8b: Accessor::fromJson 反向写入");
    struct Foo {
        float  f = 0.0f;
        Vec3   v = Vec3(0);
    };
    Foo foo;

    reflection::Accessor<float>::fromJson(&foo, offsetof(Foo, f), nlohmann::json(99.5f));
    reflection::Accessor<Vec3>::fromJson(&foo, offsetof(Foo, v),
                                          nlohmann::json::array({1.0f, 2.0f, 3.0f}));

    TEST_CHECK(foo.f == 99.5f, "fromJson: f = 99.5 (实际=%f)", foo.f);
    TEST_CHECK(foo.v.x == 1.0f && foo.v.y == 2.0f && foo.v.z == 3.0f,
               "fromJson: v = (1,2,3) (实际=%f,%f,%f)", foo.v.x, foo.v.y, foo.v.z);
}

// ============================================================
// main
// ============================================================
int main(int argc, char* argv[])
{
    (void)argc; (void)argv;
    std::printf("============================================================\n");
    std::printf("  课5 反射系统测试\n");
    std::printf("============================================================\n");

    Test_ClassMetadata();
    Test_FieldEnumeration();
    Test_DefaultValues();
    Test_SetGetRoundTrip();
    Test_UnknownField();
    Test_StaticInstance();
    Test_RegistryManualRegister();
    Test_AccessorDirect();
    Test_AccessorFromJson();

    std::printf("\n============================================================\n");
    std::printf("  结果：%d / %d 通过，%d 失败\n",
                g_tests - g_failures, g_tests, g_failures);
    std::printf("============================================================\n");

    // 也写入日志文件方便排查
    ME_LOG_INFO("Reflection tests: %d/%d passed, %d failed",
                g_tests - g_failures, g_tests, g_failures);

    return g_failures == 0 ? 0 : 1;
}
