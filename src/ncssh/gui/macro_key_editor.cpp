#include "ncssh/gui/macro_key_editor.hpp"

#include "ncssh/core/i18n.hpp"
#include "ncssh/core/macros.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;
namespace ma = core::macroactions;

// ---------------------------------------------------------------------------
// PayloadEditor
// ---------------------------------------------------------------------------

PayloadEditor::PayloadEditor(const QStringList &layerNames, QWidget *parent)
    : QWidget(parent), m_layerNames(layerNames)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_stack = new QStackedWidget(this);
    layout->addWidget(m_stack);

    // 0: text
    m_text = new QLineEdit(m_stack);
    m_stack->addWidget(m_text);

    // 1: number
    auto *numberPage = new QWidget(m_stack);
    auto *numberLayout = new QHBoxLayout(numberPage);
    numberLayout->setContentsMargins(0, 0, 0, 0);
    m_number = new QSpinBox(numberPage);
    m_number->setRange(0, 100000);
    numberLayout->addWidget(m_number);
    numberLayout->addStretch(1);
    m_stack->addWidget(numberPage);

    // 2: none
    m_none = new QLabel(_t("Diese Aktion braucht keine weiteren Angaben."), m_stack);
    m_none->setObjectName(QStringLiteral("Muted"));
    m_stack->addWidget(m_none);

    // 3: layer
    m_layer = new QComboBox(m_stack);
    m_layer->addItems(m_layerNames);
    m_stack->addWidget(m_layer);

    // 4: window
    auto *windowPage = new QWidget(m_stack);
    auto *windowForm = new QFormLayout(windowPage);
    windowForm->setContentsMargins(0, 0, 0, 0);
    m_windowCommand = new QComboBox(windowPage);
    m_windowCommand->addItem(_t("Maximieren"), QStringLiteral("maximize"));
    m_windowCommand->addItem(_t("Minimieren"), QStringLiteral("minimize"));
    m_windowCommand->addItem(_t("Links andocken"), QStringLiteral("snap_left"));
    m_windowCommand->addItem(_t("Rechts andocken"), QStringLiteral("snap_right"));
    m_windowCommand->addItem(_t("Andere minimieren"), QStringLiteral("minimize_others"));
    m_windowTitle = new QLineEdit(windowPage);
    m_windowTitle->setPlaceholderText(_t("Fenstertitel (leer = aktives Fenster)"));
    windowForm->addRow(_t("Aktion"), m_windowCommand);
    windowForm->addRow(_t("Fenster"), m_windowTitle);
    m_stack->addWidget(windowPage);

    // 5: sequence
    auto *seqPage = new QWidget(m_stack);
    auto *seqLayout = new QVBoxLayout(seqPage);
    seqLayout->setContentsMargins(0, 0, 0, 0);
    m_sequence = new QListWidget(seqPage);
    seqLayout->addWidget(m_sequence);
    auto *seqButtons = new QHBoxLayout();
    auto *addBtn = new QPushButton(_t("Hinzufügen"), seqPage);
    auto *editBtn = new QPushButton(_t("Bearbeiten"), seqPage);
    auto *removeBtn = new QPushButton(_t("Entfernen"), seqPage);
    auto *upBtn = new QPushButton(QStringLiteral("▲"), seqPage);
    auto *downBtn = new QPushButton(QStringLiteral("▼"), seqPage);
    upBtn->setFixedWidth(34);
    downBtn->setFixedWidth(34);
    connect(addBtn, &QPushButton::clicked, this, &PayloadEditor::seqAdd);
    connect(editBtn, &QPushButton::clicked, this, &PayloadEditor::seqEdit);
    connect(removeBtn, &QPushButton::clicked, this, &PayloadEditor::seqRemove);
    connect(upBtn, &QPushButton::clicked, this, [this] { seqMove(-1); });
    connect(downBtn, &QPushButton::clicked, this, [this] { seqMove(1); });
    connect(m_sequence, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *) { seqEdit(); });
    seqButtons->addWidget(addBtn);
    seqButtons->addWidget(editBtn);
    seqButtons->addWidget(removeBtn);
    seqButtons->addStretch(1);
    seqButtons->addWidget(upBtn);
    seqButtons->addWidget(downBtn);
    seqLayout->addLayout(seqButtons);
    m_stack->addWidget(seqPage);

    // 6: ssh
    auto *sshPage = new QWidget(m_stack);
    auto *sshForm = new QFormLayout(sshPage);
    sshForm->setContentsMargins(0, 0, 0, 0);
    m_sshCommand = new QLineEdit(sshPage);
    m_sshRun = new QCheckBox(_t("direkt ausführen (sonst nur einfügen)"), sshPage);
    m_sshRun->setChecked(true);
    sshForm->addRow(_t("Befehl"), m_sshCommand);
    sshForm->addRow(QString(), m_sshRun);
    m_stack->addWidget(sshPage);

    // 7: json
    m_json = new QPlainTextEdit(m_stack);
    m_json->setPlaceholderText(QStringLiteral("{\"x\": 100, \"y\": 200}"));
    m_stack->addWidget(m_json);
}

void PayloadEditor::setAction(const QString &actionType)
{
    const QString editor = ma::spec(actionType).editor;
    int page = 0;
    if (editor == QLatin1String("number")) page = 1;
    else if (editor == QLatin1String("none")) page = 2;
    else if (editor == QLatin1String("layer")) page = 3;
    else if (editor == QLatin1String("window")) page = 4;
    else if (editor == QLatin1String("sequence")) page = 5;
    else if (editor == QLatin1String("ssh")) page = 6;
    else if (editor == QLatin1String("json")) page = 7;
    m_stack->setCurrentIndex(page);
}

void PayloadEditor::load(const QString &actionType, const QJsonValue &payload)
{
    setAction(actionType);
    const QString editor = ma::spec(actionType).editor;

    if (editor == QLatin1String("number")) {
        m_number->setValue(int(payload.toDouble(payload.toString().toDouble())));
    } else if (editor == QLatin1String("layer")) {
        m_layer->setCurrentText(payload.toString());
    } else if (editor == QLatin1String("window")) {
        if (payload.isObject()) {
            const QJsonObject o = payload.toObject();
            const int idx = m_windowCommand->findData(
                o.value(QStringLiteral("command")).toString());
            if (idx >= 0)
                m_windowCommand->setCurrentIndex(idx);
            m_windowTitle->setText(o.value(QStringLiteral("window_title")).toString());
        } else {
            const int idx = m_windowCommand->findData(payload.toString());
            if (idx >= 0)
                m_windowCommand->setCurrentIndex(idx);
        }
    } else if (editor == QLatin1String("sequence")) {
        m_steps.clear();
        for (const QJsonValue &v : payload.toArray()) {
            if (v.isObject())
                m_steps.push_back(v.toObject());
        }
        seqRefresh();
    } else if (editor == QLatin1String("ssh")) {
        if (payload.isObject()) {
            const QJsonObject o = payload.toObject();
            m_sshCommand->setText(o.value(QStringLiteral("command")).toString());
            m_sshRun->setChecked(o.value(QStringLiteral("run")).toBool(true));
        } else {
            m_sshCommand->setText(payload.toString());
        }
    } else if (editor == QLatin1String("json")) {
        if (payload.isObject() || payload.isArray()) {
            const QJsonDocument doc = payload.isObject()
                                          ? QJsonDocument(payload.toObject())
                                          : QJsonDocument(payload.toArray());
            m_json->setPlainText(QString::fromUtf8(doc.toJson(QJsonDocument::Indented)));
        } else {
            m_json->setPlainText(payload.toString());
        }
    } else {
        m_text->setText(payload.toString());
    }
}

QJsonValue PayloadEditor::value(const QString &actionType) const
{
    const QString editor = ma::spec(actionType).editor;
    if (editor == QLatin1String("number"))
        return m_number->value();
    if (editor == QLatin1String("none"))
        return QString();
    if (editor == QLatin1String("layer"))
        return m_layer->currentText();
    if (editor == QLatin1String("window"))
        return QJsonObject{
            {QStringLiteral("command"), m_windowCommand->currentData().toString()},
            {QStringLiteral("window_title"), m_windowTitle->text().trimmed()}};
    if (editor == QLatin1String("sequence")) {
        QJsonArray arr;
        for (const QJsonObject &step : m_steps)
            arr.append(step);
        return arr;
    }
    if (editor == QLatin1String("ssh"))
        return QJsonObject{{QStringLiteral("command"), m_sshCommand->text()},
                           {QStringLiteral("run"), m_sshRun->isChecked()}};
    if (editor == QLatin1String("json")) {
        const QJsonDocument doc = QJsonDocument::fromJson(m_json->toPlainText().toUtf8());
        if (doc.isObject())
            return doc.object();
        if (doc.isArray())
            return doc.array();
        return m_json->toPlainText();
    }
    return m_text->text();
}

void PayloadEditor::seqRefresh()
{
    m_sequence->clear();
    for (const QJsonObject &step : m_steps) {
        const QString type = step.value(QStringLiteral("action_type")).toString();
        const QJsonValue payload = step.value(QStringLiteral("payload"));
        m_sequence->addItem(QStringLiteral("%1 — %2")
                                .arg(ma::spec(type).label,
                                     payload.isString() ? payload.toString()
                                                        : QStringLiteral("…")));
    }
}

// Einen einzelnen Schritt bearbeiten (rekursiv der gleiche Editor, ohne Sequenz).
static bool editStep(QJsonObject &step, const QStringList &layerNames, QWidget *parent)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(_t("Aktion"));
    dlg.resize(520, 300);
    auto *layout = new QVBoxLayout(&dlg);
    auto *form = new QFormLayout();

    auto *action = new QComboBox(&dlg);
    for (const auto &[group, specs] : ma::groupedActions()) {
        for (const ma::ActionSpec &s : specs) {
            // Sequenzen nicht schachteln.
            if (s.editor == QLatin1String("sequence"))
                continue;
            action->addItem(QStringLiteral("%1 · %2").arg(group, s.label), s.key);
        }
    }
    const int idx = action->findData(step.value(QStringLiteral("action_type")).toString());
    if (idx >= 0)
        action->setCurrentIndex(idx);
    form->addRow(_t("Aktion"), action);
    layout->addLayout(form);

    auto *payload = new PayloadEditor(layerNames, &dlg);
    payload->load(action->currentData().toString(), step.value(QStringLiteral("payload")));
    QObject::connect(action, &QComboBox::currentIndexChanged, &dlg, [action, payload] {
        payload->setAction(action->currentData().toString());
    });
    layout->addWidget(payload, 1);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QObject::connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(box);

    if (dlg.exec() != QDialog::Accepted)
        return false;
    const QString type = action->currentData().toString();
    step = QJsonObject{{QStringLiteral("action_type"), type},
                       {QStringLiteral("payload"), payload->value(type)}};
    return true;
}

void PayloadEditor::seqAdd()
{
    QJsonObject step{{QStringLiteral("action_type"), QStringLiteral("execute")},
                     {QStringLiteral("payload"), QString()}};
    if (!editStep(step, m_layerNames, this))
        return;
    m_steps.push_back(step);
    seqRefresh();
}

void PayloadEditor::seqEdit()
{
    const int row = m_sequence->currentRow();
    if (row < 0 || row >= int(m_steps.size()))
        return;
    QJsonObject step = m_steps[row];
    if (!editStep(step, m_layerNames, this))
        return;
    m_steps[row] = step;
    seqRefresh();
}

void PayloadEditor::seqRemove()
{
    const int row = m_sequence->currentRow();
    if (row < 0 || row >= int(m_steps.size()))
        return;
    m_steps.erase(m_steps.begin() + row);
    seqRefresh();
}

void PayloadEditor::seqMove(int delta)
{
    const int row = m_sequence->currentRow();
    const int target = row + delta;
    if (row < 0 || target < 0 || target >= int(m_steps.size()))
        return;
    std::swap(m_steps[row], m_steps[target]);
    seqRefresh();
    m_sequence->setCurrentRow(target);
}

// ---------------------------------------------------------------------------
// MacroKeyEditor
// ---------------------------------------------------------------------------

MacroKeyEditor::MacroKeyEditor(const QJsonObject &config, const QStringList &layerNames,
                               QWidget *parent)
    : QDialog(parent), m_result(config), m_layerNames(layerNames)
{
    setWindowTitle(_t("Taste bearbeiten"));
    resize(620, 640);

    auto *layout = new QVBoxLayout(this);

    // --- Darstellung ---
    auto *lookBox = new QGroupBox(_t("Darstellung"), this);
    auto *lookForm = new QFormLayout(lookBox);
    m_label = new QLineEdit(config.value(QStringLiteral("label")).toString(), lookBox);
    m_labelPos = new QComboBox(lookBox);
    m_labelPos->addItem(_t("oben"), QStringLiteral("top"));
    m_labelPos->addItem(_t("mittig"), QStringLiteral("middle"));
    m_labelPos->addItem(_t("unten"), QStringLiteral("bottom"));
    const int posIdx = m_labelPos->findData(
        config.value(QStringLiteral("label_pos")).toString(QStringLiteral("bottom")));
    if (posIdx >= 0)
        m_labelPos->setCurrentIndex(posIdx);

    auto *iconRow = new QHBoxLayout();
    m_icon = new QLineEdit(config.value(QStringLiteral("icon")).toString(), lookBox);
    auto *iconBtn = new QPushButton(QStringLiteral("…"), lookBox);
    iconBtn->setFixedWidth(34);
    connect(iconBtn, &QPushButton::clicked, this, &MacroKeyEditor::pickIcon);
    iconRow->addWidget(m_icon, 1);
    iconRow->addWidget(iconBtn);

    m_color = new QPushButton(lookBox);
    m_color->setText(config.value(QStringLiteral("font_color")).toString(
        QStringLiteral("#ffffff")));
    connect(m_color, &QPushButton::clicked, this, &MacroKeyEditor::pickColor);

    m_fontFamily = new QLineEdit(config.value(QStringLiteral("font_family")).toString(), lookBox);
    m_fontFamily->setPlaceholderText(_t("Standard-Schriftart"));

    m_shortcut = new QKeySequenceEdit(lookBox);
    m_shortcut->setKeySequence(
        QKeySequence(config.value(QStringLiteral("shortcut")).toString()));

    lookForm->addRow(_t("Beschriftung"), m_label);
    lookForm->addRow(_t("Position"), m_labelPos);
    lookForm->addRow(_t("Icon"), iconRow);
    lookForm->addRow(_t("Schriftfarbe"), m_color);
    lookForm->addRow(_t("Schriftart"), m_fontFamily);
    lookForm->addRow(_t("Globales Kürzel"), m_shortcut);
    layout->addWidget(lookBox);

    // --- Aktion ---
    auto *actionBox = new QGroupBox(_t("Aktion"), this);
    auto *actionLayout = new QVBoxLayout(actionBox);
    m_action = new QComboBox(actionBox);
    for (const auto &[group, specs] : ma::groupedActions()) {
        for (const ma::ActionSpec &s : specs)
            m_action->addItem(QStringLiteral("%1 · %2").arg(group, s.label), s.key);
    }
    const int actionIdx = m_action->findData(
        config.value(QStringLiteral("action_type")).toString(QStringLiteral("execute")));
    if (actionIdx >= 0)
        m_action->setCurrentIndex(actionIdx);
    connect(m_action, &QComboBox::currentIndexChanged, this,
            &MacroKeyEditor::onActionChanged);
    actionLayout->addWidget(m_action);

    m_actionHint = new QLabel(actionBox);
    m_actionHint->setObjectName(QStringLiteral("Muted"));
    m_actionHint->setWordWrap(true);
    actionLayout->addWidget(m_actionHint);

    m_payload = new PayloadEditor(m_layerNames, actionBox);
    actionLayout->addWidget(m_payload, 1);
    layout->addWidget(actionBox, 1);

    auto *box = new QDialogButtonBox(this);
    box->addButton(QDialogButtonBox::Ok);
    box->addButton(QDialogButtonBox::Cancel);
    auto *clearBtn = box->addButton(_t("Taste leeren"), QDialogButtonBox::DestructiveRole);
    connect(box, &QDialogButtonBox::accepted, this, &MacroKeyEditor::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(clearBtn, &QPushButton::clicked, this, [this] {
        m_cleared = true;
        m_result = QJsonObject();
        QDialog::accept();
    });
    layout->addWidget(box);

    // Hinweistext setzen, dann den vorhandenen Payload laden.
    onActionChanged();
    m_payload->load(m_action->currentData().toString(),
                    config.value(QStringLiteral("payload")));
}

void MacroKeyEditor::onActionChanged()
{
    const QString type = m_action->currentData().toString();
    const ma::ActionSpec &spec = ma::spec(type);
    m_actionHint->setText(spec.tooltip);
    m_payload->setAction(type);
    // Plattform-Hinweis, falls die Aktion hier nicht laeuft.
    if (const auto hint = ma::missingDependencyHint(type))
        m_actionHint->setText(m_actionHint->text() + QStringLiteral("\n⚠ ") + *hint);
}

void MacroKeyEditor::pickIcon()
{
    const QString file = QFileDialog::getOpenFileName(
        this, _t("Icon wählen"), m_icon->text(),
        _t("Bilder") + QStringLiteral(" (*.png *.jpg *.jpeg *.bmp *.gif *.svg)"));
    if (!file.isEmpty())
        m_icon->setText(file);
}

void MacroKeyEditor::pickColor()
{
    const QColor chosen = QColorDialog::getColor(QColor(m_color->text()), this,
                                                 _t("Schriftfarbe"));
    if (chosen.isValid())
        m_color->setText(chosen.name());
}

void MacroKeyEditor::accept()
{
    const QString type = m_action->currentData().toString();
    m_result = QJsonObject{
        {QStringLiteral("label"), m_label->text()},
        {QStringLiteral("icon"), m_icon->text()},
        {QStringLiteral("label_pos"), m_labelPos->currentData().toString()},
        {QStringLiteral("font_color"), m_color->text()},
        {QStringLiteral("font_family"), m_fontFamily->text()},
        {QStringLiteral("shortcut"), m_shortcut->keySequence().toString()},
        {QStringLiteral("action_type"), type},
        {QStringLiteral("payload"), m_payload->value(type)},
    };
    QDialog::accept();
}

} // namespace ncssh::gui
