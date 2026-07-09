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

#include "Compiler/compiler_test.cpp"


// ============================================================
// main
// ============================================================

int main(int argc, char* argv[])
{
    // (void)argc; (void)argv;

    QApplication app(argc, argv);

    RunCompilerTest();

    return app.exec();
}
