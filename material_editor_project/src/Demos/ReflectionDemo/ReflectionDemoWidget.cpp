#include "Demos/ReflectionDemo/ReflectionDemoWidget.h"
#include "Reflection/Public/Property.h"   // FloatProperty/IntProperty/BoolProperty/StringProperty/Vec3Property
#include <QFrame>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QHBoxLayout>
#include <sstream>

// 课5c 反射演示窗口 —— 适配 UE 风格 Property 继承体系。
// 旧版用 switch(reflection::FieldType) 分派，现在 GetParameters() 返回 vector<const Property*>，
// 按【Property 子类】dynamic_cast 决定创建什么编辑器；读回按【控件类型】qobject_cast 取值。

ReflectionDemoWidget::ReflectionDemoWidget(Expression *expression, QWidget *parent):
QWidget(parent), expr_(expression)
{
    setWindowTitle("Reflect Demo Widget");
    resize(600,700);
    main_layout_ = new QVBoxLayout(this);

    // 顶部：类元信息
    const ClassDesc* c_desc = expr_->GetClassDesc();
    QLabel* c_desc_label = new QLabel;
    c_desc_label->setText(QString("表达式：%1, 分组:%2")
        .arg(QString::fromStdString(c_desc->display_name))
        .arg(QString::fromStdString(c_desc->category)));
    c_desc_label->setStyleSheet("font-size: 14px; font-weight: bold; padding: 8px;");
    main_layout_->addWidget(c_desc_label);

    QFrame* line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    main_layout_->addWidget(line);

    // 中部：按反射字段生成表单
    form_layout_ = new QFormLayout;
    main_layout_->addLayout(form_layout_);
    BuildForm();

    // 底部：实时 JSON 预览
    main_layout_->addWidget(new QLabel("实时JSON状态"));
    json_label_ = new QLabel;
    json_label_->setStyleSheet(
        "font-family: Consolas, 'Courier New', monospace;"
        "background: #1e1e1e; color: #dcdcdc;"
        "padding: 10px; border-radius: 4px;");
    json_label_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    json_label_->setMinimumHeight(150);
    json_label_->setTextFormat(Qt::RichText);
    main_layout_->addWidget(json_label_, /*stretch=*/1);

    RefreshJsonPreview();
}

void ReflectionDemoWidget::BuildForm()
{
    // GetParameters() 现在返回 vector<const Property*>，按子类 dynamic_cast 分派创建编辑器
    std::vector<const Property*> fields = expr_->GetParameters();
    for (const Property* field : fields) {
        nlohmann::json cur = expr_->GetParameter(field->name);   // 当前值（默认值）
        QWidget* editor = nullptr;

        if (dynamic_cast<const FloatProperty*>(field)) {
            auto* spin = new QDoubleSpinBox;
            spin->setRange(-1000.0, 1000.0);
            spin->setDecimals(3);
            spin->setSingleStep(0.1);
            spin->setValue(cur.is_number() ? cur.get<float>() : 0.0f);
            connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                    this, &ReflectionDemoWidget::OnFiledChanged);
            editor = spin;
        }
        else if (dynamic_cast<const IntProperty*>(field)) {
            auto* spin = new QSpinBox;
            spin->setRange(-10000, 10000);
            spin->setValue(cur.is_number_integer() ? cur.get<int>() : 0);
            connect(spin, QOverload<int>::of(&QSpinBox::valueChanged),
                    this, &ReflectionDemoWidget::OnFiledChanged);
            editor = spin;
        }
        else if (dynamic_cast<const BoolProperty*>(field)) {
            auto* check = new QCheckBox;
            check->setChecked(cur.is_boolean() ? cur.get<bool>() : false);
            connect(check, &QCheckBox::stateChanged,
                    this, &ReflectionDemoWidget::OnFiledChanged);
            editor = check;
        }
        else if (dynamic_cast<const StringProperty*>(field)) {
            auto* edit = new QLineEdit;
            edit->setText(QString::fromStdString(cur.is_string() ? cur.get<std::string>() : ""));
            connect(edit, &QLineEdit::textEdited,
                    this, &ReflectionDemoWidget::OnFiledChanged);
            editor = edit;
        }
        else if (dynamic_cast<const Vec3Property*>(field)) {
            // Vec3 用 3 个 SpinBox 横排
            auto* container = new QWidget;
            auto* hbox = new QHBoxLayout(container);
            hbox->setContentsMargins(0, 0, 0, 0);
            for (int i = 0; i < 3; ++i) {
                auto* spin = new QDoubleSpinBox;
                spin->setRange(-1000.0, 1000.0);
                spin->setDecimals(3);
                spin->setSingleStep(0.1);
                float v = (cur.is_array() && cur.size() > i) ? cur[i].get<float>() : 0.0f;
                spin->setValue(v);
                connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                        this, &ReflectionDemoWidget::OnFiledChanged);
                hbox->addWidget(spin);
            }
            editor = container;
        }
        else {
            editor = new QLabel("未支持的类型");
        }

        form_layout_->addRow(QString::fromStdString(field->name) + " :", editor);
        field_widgets_[field->name] = editor;
    }
}

void ReflectionDemoWidget::OnFiledChanged()
{
    // 读回：按【控件类型】qobject_cast 决定怎么取值（与 Property 子类类型无关）
    for (const auto& [name, widget] : field_widgets_) {
        if (auto* dspin = qobject_cast<QDoubleSpinBox*>(widget)) {
            expr_->SetParameter(name, (float)dspin->value());
        }
        else if (auto* ispin = qobject_cast<QSpinBox*>(widget)) {
            expr_->SetParameter(name, (int)ispin->value());
        }
        else if (auto* check = qobject_cast<QCheckBox*>(widget)) {
            expr_->SetParameter(name, check->isChecked());
        }
        else if (auto* edit = qobject_cast<QLineEdit*>(widget)) {
            expr_->SetParameter(name, edit->text().toStdString());
        }
        else if (auto* layout = widget->layout()) {
            // Vec3 容器：layout 里有几个 spin 就读几个，拼成数组
            nlohmann::json arr = nlohmann::json::array({});
            for (int i = 0; i < layout->count(); ++i) {
                auto* spin = qobject_cast<QDoubleSpinBox*>(layout->itemAt(i)->widget());
                if (spin) arr.push_back((float)spin->value());
            }
            if (!arr.empty()) expr_->SetParameter(name, arr);
        }
        // else：未支持类型（QLabel），跳过
    }
    RefreshJsonPreview();
}

void ReflectionDemoWidget::RefreshJsonPreview()
{
    nlohmann::json j;
    for (const Property* field : expr_->GetParameters()) {
        j[field->name] = expr_->GetParameter(field->name);
    }
    std::stringstream ss;
    ss << std::setw(2) << j;   // 缩进 2 空格，pretty-print
    std::string raw = ss.str();

    // 转义 HTML 字符（让 QLabel 不会把 JSON 当富文本解析）
    QString html = QString::fromStdString(raw)
        .toHtmlEscaped()
        .replace(" ", "&nbsp;")
        .replace("\n", "<br>");
    html = "<pre style='margin:0'>" + html + "</pre>";
    json_label_->setText(html);
}
