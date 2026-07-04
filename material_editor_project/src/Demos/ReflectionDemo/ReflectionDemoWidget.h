#pragma once
#include <QWidget>
#include <QLabel>
#include <QFormLayout>
#include <QVBoxLayout>
#include <map>
#include <string>
#include "Expression/Public/Expression.h"

class ReflectionDemoWidget : public QWidget
{
    Q_OBJECT

public:
    ReflectionDemoWidget(Expression *expression, QWidget *parent = nullptr);

private slots:
    void OnFiledChanged();

private:
    void BuildForm();          // 遍历 GetParameters() 构建UI
    void RefreshJsonPreview(); // 刷新底部JSON显示

    Expression *expr_;
    QVBoxLayout *main_layout_;
    QFormLayout *form_layout_;
    QLabel *json_label_;

    // 字段名 -> Qt控件映射
    std::map<std::string, QWidget *> field_widgets_;
};