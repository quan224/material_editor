#include "Demos/ReflectionDemo/ReflectionDemoWidget.h"
#include <QFrame>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLineEdit>



ReflectionDemoWidget::ReflectionDemoWidget(Expression *expression, QWidget *parent): 
QWidget(parent), expr_(expression)
{
    setWindowTitle("Reflect Demo Widger");
    resize(600,700);
    main_layout_ = new QVBoxLayout(this);

    const reflection::ClassDesc* c_desc =  expr_->GetClassDesc();
    QLabel* c_desc_label = new QLabel;
    c_desc_label->setText(QString("表达式：%1, 分组:%2")
    .arg(c_desc->display_name)
    .arg(c_desc->category));
    c_desc_label->setStyleSheet("font-size: 14px; font-weight: bold; padding: 8px;");
    main_layout_->addWidget(c_desc_label);

    QFrame* line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    main_layout_->addWidget(line);

    form_layout_ = new QFormLayout;
    main_layout_->addLayout(form_layout_);
    BuildForm();

    main_layout_->addWidget(new QLabel("实时JSON状态"));
    json_label_ = new QLabel;
        json_label_->setStyleSheet(
        "font-family: Consolas, 'Courier New', monospace;"
        "background: #1e1e1e; color: #dcdcdc;"
        "padding: 10px; border-radius: 4px;");
    json_label_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    json_label_->setMinimumHeight(150);
    json_label_->setTextFormat(Qt::RichText);  // 支持 <br> 和 &nbsp;
    main_layout_->addWidget(json_label_, /*stretch=*/1);

    RefreshJsonPreview();
       

}

void ReflectionDemoWidget::BuildForm()
{
    std::vector<reflection::FieldDesc> fields = expr_->GetParameters();
    for (const auto &field : fields)
    {
        QWidget *editor = nullptr;

        switch (field.type)
        {
        case reflection::FieldType::Float:
        {
            auto *spin = new QDoubleSpinBox;
            spin->setRange(-1000.0f, 1000.0f);
            spin->setDecimals(3);
            spin->setSingleStep(0.1);
            nlohmann::json field_value = expr_->GetParameter(field.name);
            spin->setValue(field_value.is_number() ? field_value.get<float>() : 0.0f);
            // QOverload 信号重载
            connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ReflectionDemoWidget::OnFiledChanged);
            editor = spin;
            break;
        }
        case reflection::FieldType::Bool: {
            auto* check = new QCheckBox;
            nlohmann::json cur = expr_->GetParameter(field.name);
            check->setChecked(cur.is_boolean() ? cur.get<bool>() : false);
            connect(check, &QCheckBox::stateChanged,
                    this, &ReflectionDemoWidget::OnFiledChanged);
            editor = check;
            break;
        }

        case reflection::FieldType::String: {
            auto* edit = new QLineEdit;
            nlohmann::json cur = expr_->GetParameter(field.name);
            edit->setText(QString::fromStdString(
                cur.is_string() ? cur.get<std::string>() : ""));
            connect(edit, &QLineEdit::textEdited,   // 注意：textEdited 不是 textChanged
                    this, &ReflectionDemoWidget::OnFiledChanged);
            editor = edit;
            break;
        }

        case reflection::FieldType::Float3: {
            // Vec3 用 3 个 SpinBox 横排
            auto* container = new QWidget;
            auto* hbox = new QHBoxLayout(container);
            hbox->setContentsMargins(0, 0, 0, 0);
            nlohmann::json cur = expr_->GetParameter(field.name);
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
            break;
        }
        default:
        {
            editor = new QLabel("未支持的类型");
        }
        }
        form_layout_->addRow(QString::fromStdString(field.name) + " :", editor);
        field_widgets_[field.name] = editor;
    }
}

void ReflectionDemoWidget::OnFiledChanged()
{
    // 遍历所有字段，把当前控件值写回 Expression
    const reflection::ClassDesc* desc = expr_->GetClassDesc();
    if (!desc) return;

    for (const auto& [name, widget] : field_widgets_) {
        const reflection::FieldDesc* field = desc->find(name);
        if (!field) continue;

        if (field->type == reflection::FieldType::Float) {
            auto* spin = qobject_cast<QDoubleSpinBox*>(widget);
            if (spin) expr_->SetParameter(name, (float)spin->value());
        }
        else if (field->type == reflection::FieldType::Bool) {
            auto* check = qobject_cast<QCheckBox*>(widget);
            if (check) expr_->SetParameter(name, check->isChecked());
        }
        else if (field->type == reflection::FieldType::String) {
            auto* edit = qobject_cast<QLineEdit*>(widget);
            if (edit) expr_->SetParameter(name, edit->text().toStdString());
        }
        else if (field->type == reflection::FieldType::Float3) {
            // Vec3 控件是一个 QWidget 包了 3 个 SpinBox
            auto* container = qobject_cast<QWidget*>(widget);
            if (!container) continue;
            auto* hbox = container->layout();
            // 先读当前 JSON 再覆盖（这样未变化的分量保持原值）
            nlohmann::json jv = expr_->GetParameter(name);
            if (!jv.is_array()) jv = nlohmann::json::array({0, 0, 0});
            for (int i = 0; i < 3 && i < hbox->count(); ++i) {
                auto* spin = qobject_cast<QDoubleSpinBox*>(hbox->itemAt(i)->widget());
                if (spin) {
                    while ((int)jv.size() <= i) jv.push_back(0.0f);
                    jv[i] = (float)spin->value();
                }
            }
            expr_->SetParameter(name, jv);
        }
    }

    RefreshJsonPreview();
}

void ReflectionDemoWidget::RefreshJsonPreview(){
    nlohmann::json j;
    for (const auto& field : expr_->GetParameters()){
        j[field.name] = expr_->GetParameter(field.name);
    }
    std::stringstream ss;
    ss << std::setw(2) << j;   // 缩进 2 空格，pretty-print
    std::string raw = ss.str();

    // 转义 HTML 字符（让 QLabel 不会把 JSON 当富文本解析）
    QString html = QString::fromStdString(raw)
        .toHtmlEscaped()
        .replace(" ", "&nbsp;")
        .replace("\n", "<br>");
    // 用等宽字体显示
    html = "<pre style='margin:0'>" + html + "</pre>";
    json_label_->setText(html);
}

// private slots:
//     void OnFiledChanged();

// private:
//     void BuildForm();          // 遍历 GetParameters() 构建UI
//     void RefreshJsonPreview(); // 刷新底部JSON显示