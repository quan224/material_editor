#include <QApplication>
#include "Expression/Public/Expression.h"
#include "Reflection/Public/ReflectionMacros.h"
#include "MaterialTypes/Public/ValueType.h"
#include "Core/Public/MathTypes.h"
#include "Demos/ReflectionDemo/ReflectionDemoWidget.h"

// ============================================================
// 测试用 Expression 子类
// ============================================================
class TestExpr : public Expression
{
public:
    float       scale   = 1.0f;
    Vec3        color   = Vec3(1.0f, 0.0f, 0.0f);
    bool        enabled = true;
    std::string label   = "default";

    ME_BEGIN_CLASS(TestExpr)
        ME_DISPLAY_NAME("Test Expr")
        ME_CATEGORY("Test")
        ME_CATEGORY_COLOR("#FF8800")
        ME_FIELD(TestExpr, scale,   1.0f)
        ME_FIELD(TestExpr, color,   Vec3(1.0f, 0.0f, 0.0f))
        ME_FIELD(TestExpr, enabled, true)
        ME_FIELD(TestExpr, label,   std::string("default"))
    ME_END_CLASS(TestExpr)

    std::vector<ExpressionPinDesc> GetInputPins()  const override { return {}; }
    std::vector<ExpressionPinDesc> GetOutputPins() const override { return {}; }
    std::vector<int32_t> Compile(MaterialCompiler*, Node*) const override { return {}; }
};

// ============================================================
// main —— 课5c：启动反射演示窗口
// ============================================================
int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    TestExpr expr;
    ReflectionDemoWidget w(&expr);
    w.show();

    return app.exec();
}
