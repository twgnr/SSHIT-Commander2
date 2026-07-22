#include "ncssh/gui/command_builder.hpp"

#include "ncssh/core/i18n.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;

CommandBuilder::CommandBuilder(const core::CommandSpec &spec, const QString &osType,
                               QWidget *parent)
    : QDialog(parent), m_spec(spec), m_osType(osType)
{
    setWindowTitle(_t("Befehlsassistent") + QStringLiteral(" — ") + spec.name);
    resize(640, 560);

    auto *layout = new QVBoxLayout(this);

    auto *desc = new QLabel(spec.description, this);
    desc->setWordWrap(true);
    desc->setObjectName(QStringLiteral("Muted"));
    layout->addWidget(desc);

    // Parameter-Formular (scrollbar, falls viele Parameter)
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto *formHost = new QWidget(scroll);
    auto *form = new QFormLayout(formHost);
    for (const core::CommandParam &param : spec.params) {
        const QString label = param.required ? param.label + QStringLiteral(" *") : param.label;
        if (param.kind == QLatin1String("choice")) {
            auto *combo = new QComboBox(formHost);
            combo->addItems(param.choices);
            if (!param.defaultValue.isEmpty())
                combo->setCurrentText(param.defaultValue);
            combo->setToolTip(param.description);
            connect(combo, &QComboBox::currentTextChanged, this, &CommandBuilder::updatePreview);
            m_choiceInputs.insert(param.name, combo);
            form->addRow(label, combo);
        } else if (param.kind == QLatin1String("flag")) {
            auto *check = new QCheckBox(param.description.isEmpty() ? param.label
                                                                    : param.description,
                                        formHost);
            check->setChecked(!param.defaultValue.isEmpty());
            connect(check, &QCheckBox::toggled, this, &CommandBuilder::updatePreview);
            m_flagInputs.insert(param.name, check);
            form->addRow(label, check);
        } else {
            auto *edit = new QLineEdit(param.defaultValue, formHost);
            edit->setPlaceholderText(param.description);
            edit->setToolTip(param.description);
            connect(edit, &QLineEdit::textChanged, this, &CommandBuilder::updatePreview);
            m_textInputs.insert(param.name, edit);
            form->addRow(label, edit);
        }
    }
    scroll->setWidget(formHost);
    layout->addWidget(scroll, 1);

    // sudo / anderer Benutzer (nur posix sinnvoll)
    if (m_osType != QLatin1String("windows")) {
        auto *box = new QGroupBox(_t("Rechte"), this);
        auto *boxLayout = new QHBoxLayout(box);
        m_sudo = new QCheckBox(QStringLiteral("sudo"), box);
        m_sudoUser = new QLineEdit(box);
        m_sudoUser->setPlaceholderText(_t("als Benutzer (optional)"));
        m_sudoUser->setEnabled(false);
        connect(m_sudo, &QCheckBox::toggled, this, [this](bool on) {
            m_sudoUser->setEnabled(on);
            updatePreview();
        });
        connect(m_sudoUser, &QLineEdit::textChanged, this, &CommandBuilder::updatePreview);
        boxLayout->addWidget(m_sudo);
        boxLayout->addWidget(m_sudoUser, 1);
        layout->addWidget(box);
    }

    layout->addWidget(new QLabel(_t("Vorschau"), this));
    m_preview = new QPlainTextEdit(this);
    m_preview->setReadOnly(true);
    m_preview->setMaximumHeight(90);
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    m_preview->setFont(mono);
    layout->addWidget(m_preview);

    if (!spec.example.isEmpty()) {
        auto *example = new QLabel(_t("Beispiel:") + QStringLiteral(" ") + spec.example, this);
        example->setObjectName(QStringLiteral("Muted"));
        example->setWordWrap(true);
        layout->addWidget(example);
    }

    auto *buttons = new QHBoxLayout();
    auto *cancel = new QPushButton(_t("Abbrechen"), this);
    auto *insert = new QPushButton(_t("Einfügen"), this);
    auto *run = new QPushButton(_t("Ausführen"), this);
    run->setDefault(true);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(insert, &QPushButton::clicked, this, [this] {
        m_command = buildCommand();
        m_runDirectly = false;
        accept();
    });
    connect(run, &QPushButton::clicked, this, [this] {
        m_command = buildCommand();
        m_runDirectly = true;
        accept();
    });
    buttons->addWidget(cancel);
    buttons->addStretch(1);
    buttons->addWidget(insert);
    buttons->addWidget(run);
    layout->addLayout(buttons);

    updatePreview();
}

QString CommandBuilder::buildCommand() const
{
    QHash<QString, QString> values;
    for (auto it = m_textInputs.begin(); it != m_textInputs.end(); ++it)
        values.insert(it.key(), it.value()->text());
    for (auto it = m_choiceInputs.begin(); it != m_choiceInputs.end(); ++it)
        values.insert(it.key(), it.value()->currentText());
    for (const core::CommandParam &param : m_spec.params) {
        if (param.kind != QLatin1String("flag"))
            continue;
        auto *check = m_flagInputs.value(param.name, nullptr);
        values.insert(param.name, (check && check->isChecked()) ? param.flagValue : QString());
    }
    QString cmd = core::render(m_spec, values);
    if (m_sudo && m_sudo->isChecked())
        cmd = core::wrapPrivilege(cmd, true, m_sudoUser->text().trimmed());
    return cmd;
}

void CommandBuilder::updatePreview()
{
    m_preview->setPlainText(buildCommand());
}

} // namespace ncssh::gui
