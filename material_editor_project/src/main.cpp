#include <cstdio>
#include <string>
#include <vector>

#include "Reflection/Public/Reflection.h"
#include "Reflection/Public/ReflectionMacros.h"
#include "Expression/Public/Expression.h"
#include "MaterialGraph/Public/Types.h"
#include "Core/Public/MathTypes.h"
#include "Core/Public/Logger.h"

#include <QApplication>

#include "Demos/ReflectionDemo/ReflectionDemoWidget.h"


// ============================================================
// main
// ============================================================

class TestExpr : public Expression
{
public:
    float scale = 0.3f;
    Vec3 color = {1.0f, 0.0f, 0.0f};
    bool enabled = true;
    std::string label = "default";

    ME_BEGIN_CLASS(TestExpr)
    ME_DISPLAY_NAME("Test Expr")
    ME_CATEGORY("Demo")
    ME_CATEGORY_COLOR("#FF8800")

    ME_FIELD(TestExpr, scale, 0.3f)
    ME_FIELD(TestExpr, color, Vec3(1.0f, 0.0f, 0.0f))
    ME_FIELD(TestExpr, enabled, true)
    ME_FIELD(TestExpr, label, "default")

    ME_END_CLASS(TestExpr)

    std::vector<ExpressionPinDesc> GetInputPins() const override { return {}; }
    std::vector<ExpressionPinDesc> GetOutputPins() const override { return {}; }
    std::vector<int32_t> Compile(MaterialCompiler *compiler, Node *ownerNode) const override { return {}; }
};

int main(int argc, char* argv[])
{
    // (void)argc; (void)argv;
    ME_LOG_INFO("============================================================\n");
    ME_LOG_INFO("  课5 反射系统测试\n");
    ME_LOG_INFO("============================================================\n");

    QApplication app(argc, argv);

    TestExpr expr;
    ReflectionDemoWidget widget(&expr);
    widget.show();

    return app.exec();
}
