#include "ncssh/gui/param_dialog.hpp"

#include "ncssh/core/i18n.hpp"
#include "ncssh/gui/line_completer.hpp"

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;

namespace {

constexpr int kOptionRole = Qt::UserRole + 1;

} // namespace

ParamDialog::ParamDialog(AsyncBridge *bridge, const QString &line, const QString &osType,
                         core::FileSystemProvider *provider, const QString &cwd,
                         AppendFn append, LineFn currentLine, QWidget *parent)
    : QDialog(parent), m_bridge(bridge), m_name(core::commandName(line, osType)),
      m_osType(osType), m_provider(provider), m_cwd(cwd), m_append(std::move(append)),
      m_currentLine(std::move(currentLine))
{
    setObjectName(QStringLiteral("ParamDialog"));
    setWindowTitle(_t("Parameter — %1").arg(m_name));
    resize(720, 560);
    auto *layout = new QVBoxLayout(this);
    m_specs = core::specsForLine(line, osType);

    // --- Argumente in Befehls-Reihenfolge (aus dem Katalog) ---
    bool hasFields = false;
    for (const core::CommandSpec &spec : m_specs)
        hasFields = hasFields || !core::positionalParams(spec).empty();
    if (hasFields) {
        auto *box = new QGroupBox(_t("Argumente (in der Reihenfolge des Befehls)"), this);
        auto *boxLayout = new QVBoxLayout(box);
        if (m_specs.size() > 1) {
            m_variant = new QComboBox(box);
            for (const core::CommandSpec &spec : m_specs)
                m_variant->addItem(spec.name);
            connect(m_variant, &QComboBox::currentIndexChanged, this, [this] { buildFields(); });
            auto *row = new QHBoxLayout();
            row->addWidget(new QLabel(_t("Variante:"), box));
            row->addWidget(m_variant, 1);
            boxLayout->addLayout(row);
        }
        m_fieldsBox = new QWidget(box);
        m_fieldsForm = new QFormLayout(m_fieldsBox);
        m_fieldsForm->setContentsMargins(0, 0, 0, 0);
        boxLayout->addWidget(m_fieldsBox);
        auto *hint = new QLabel(_t("Tab vervollständigt Dateien und Verzeichnisse wie im Terminal."), box);
        hint->setObjectName(QStringLiteral("Muted"));
        boxLayout->addWidget(hint);
        auto *previewRow = new QHBoxLayout();
        m_preview = new QLabel(box);
        m_preview->setObjectName(QStringLiteral("Muted"));
        m_preview->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_appendFieldsBtn = new QPushButton(_t("Argumente anfügen"), box);
        connect(m_appendFieldsBtn, &QPushButton::clicked, this, &ParamDialog::appendPositionals);
        previewRow->addWidget(m_preview, 1);
        previewRow->addWidget(m_appendFieldsBtn);
        boxLayout->addLayout(previewRow);
        layout->addWidget(box);
        buildFields();
    }

    // --- Optionen ---
    auto *optBox = new QGroupBox(_t("Optionen (Doppelklick fügt an)"), this);
    auto *optLayout = new QVBoxLayout(optBox);
    m_filter = new QLineEdit(optBox);
    m_filter->setPlaceholderText(_t("Filtern …"));
    m_filter->setClearButtonEnabled(true);
    connect(m_filter, &QLineEdit::textChanged, this, &ParamDialog::applyFilter);
    optLayout->addWidget(m_filter);
    m_tree = new QTreeWidget(optBox);
    m_tree->setObjectName(QStringLiteral("ParamOptions"));
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({_t("Option"), _t("Beschreibung")});
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(true);
    m_tree->header()->setStretchLastSection(true);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
        if (item)
            insertOption(item->data(0, kOptionRole).toInt());
    });
    optLayout->addWidget(m_tree, 1);
    auto *optButtons = new QHBoxLayout();
    m_helpStatus = new QLabel(optBox);
    m_helpStatus->setObjectName(QStringLiteral("Muted"));
    auto *insertBtn = new QPushButton(_t("Option anfügen"), optBox);
    connect(insertBtn, &QPushButton::clicked, this, [this] {
        if (QTreeWidgetItem *item = m_tree->currentItem())
            insertOption(item->data(0, kOptionRole).toInt());
    });
    optButtons->addWidget(m_helpStatus, 1);
    optButtons->addWidget(insertBtn);
    optLayout->addLayout(optButtons);
    layout->addWidget(optBox, 1);

    auto *closeRow = new QHBoxLayout();
    closeRow->addStretch(1);
    auto *closeBtn = new QPushButton(_t("Schließen"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
    closeRow->addWidget(closeBtn);
    layout->addLayout(closeRow);

    for (const core::CommandSpec &spec : m_specs)
        addOptions(core::optionsFromSpec(spec), true);
}

void ParamDialog::buildFields()
{
    if (!m_fieldsForm)
        return;
    while (m_fieldsForm->rowCount() > 0)
        m_fieldsForm->removeRow(0);
    m_fieldNames.clear();
    m_fieldWidgets.clear();
    const int index = m_variant ? m_variant->currentIndex() : 0;
    if (index < 0 || index >= int(m_specs.size()))
        return;
    for (const core::CommandParam &p : core::positionalParams(m_specs.at(index))) {
        QWidget *field = nullptr;
        if (p.kind == QLatin1String("choice") && !p.choices.isEmpty()) {
            auto *combo = new QComboBox(m_fieldsBox);
            combo->setEditable(true);
            combo->addItems(p.choices);
            if (!p.defaultValue.isEmpty())
                combo->setCurrentText(p.defaultValue);
            connect(combo, &QComboBox::currentTextChanged, this, &ParamDialog::updatePreview);
            field = combo;
        } else {
            auto *edit = new QLineEdit(m_fieldsBox);
            edit->setText(p.defaultValue);
            edit->setPlaceholderText(p.description);
            auto *completer = new LineCompleter(m_bridge, edit, /*handleTab=*/true);
            completer->setProvider(m_provider);
            completer->setCwd(m_cwd);
            connect(edit, &QLineEdit::textChanged, this, &ParamDialog::updatePreview);
            connect(edit, &QLineEdit::returnPressed, this, &ParamDialog::appendPositionals);
            field = edit;
        }
        field->setToolTip(p.description);
        QString label = p.label.isEmpty() ? p.name : p.label;
        if (p.required)
            label += QStringLiteral(" *");
        m_fieldsForm->addRow(label + QLatin1Char(':'), field);
        m_fieldNames << p.name;
        m_fieldWidgets << field;
    }
    if (!m_fieldWidgets.isEmpty())
        m_fieldWidgets.first()->setFocus();
    updatePreview();
}

QString ParamDialog::positionalPreview() const
{
    const int index = m_variant ? m_variant->currentIndex() : 0;
    if (index < 0 || index >= int(m_specs.size()))
        return {};
    QHash<QString, QString> values;
    for (int i = 0; i < m_fieldWidgets.size(); ++i) {
        QWidget *w = m_fieldWidgets.at(i);
        if (auto *edit = qobject_cast<QLineEdit *>(w))
            values.insert(m_fieldNames.at(i), edit->text());
        else if (auto *combo = qobject_cast<QComboBox *>(w))
            values.insert(m_fieldNames.at(i), combo->currentText());
    }
    const QString line = m_currentLine ? m_currentLine() : QString();
    return core::positionalAppend(m_specs.at(index), values, line);
}

void ParamDialog::updatePreview()
{
    if (!m_preview)
        return;
    const QString text = positionalPreview();
    m_preview->setText(text.isEmpty() ? QString() : _t("Wird angefügt: %1").arg(text));
    if (m_appendFieldsBtn)
        m_appendFieldsBtn->setEnabled(!text.isEmpty());
}

void ParamDialog::appendPositionals()
{
    const QString text = positionalPreview();
    if (text.isEmpty() || !m_append)
        return;
    m_append(text);
    // Felder leeren: ein zweites Klicken haengt sonst dieselben Werte erneut an.
    for (QWidget *w : m_fieldWidgets)
        if (auto *edit = qobject_cast<QLineEdit *>(w))
            edit->clear();
    updatePreview();
}

void ParamDialog::addHelpOptions(const std::vector<core::CommandOption> &options)
{
    addOptions(options, false);
    setHelpStatus(options.empty() ? _t("Keine weiteren Optionen in der Hilfe gefunden.")
                                  : _t("%1 Optionen aus der Hilfe des Servers.").arg(options.size()));
}

void ParamDialog::setHelpStatus(const QString &text)
{
    m_helpStatus->setText(text);
}

void ParamDialog::addOptions(const std::vector<core::CommandOption> &options, bool fromCatalog)
{
    // Doppelte (Katalog + Hilfe) nur einmal zeigen.
    QSet<QString> shown;
    for (const core::CommandOption &o : m_options)
        shown << o.insertText();
    for (const core::CommandOption &o : options) {
        if (shown.contains(o.insertText()))
            continue;
        shown << o.insertText();
        const int index = int(m_options.size());
        m_options.push_back(o);
        auto *item = new QTreeWidgetItem(m_tree, {o.display(), o.description});
        item->setData(0, kOptionRole, index);
        item->setToolTip(1, o.description);
        if (fromCatalog) {
            QFont f = item->font(0);
            f.setBold(true);   // Katalog = die gaengigsten Schalter
            item->setFont(0, f);
        }
    }
    m_tree->resizeColumnToContents(0);
    if (m_tree->columnWidth(0) > 260)
        m_tree->setColumnWidth(0, 260);
    applyFilter();
}

void ParamDialog::insertOption(int index)
{
    if (index < 0 || index >= int(m_options.size()) || !m_append)
        return;
    const core::CommandOption &o = m_options.at(index);
    QString value;
    if (o.literal.isEmpty() && !o.arg.isEmpty()) {
        // Option mit Wert: gleich mit abfragen, damit sie vollstaendig landet.
        bool ok = false;
        value = QInputDialog::getText(this, _t("Wert für %1").arg(o.display()),
                                      o.description.isEmpty() ? o.arg : o.description,
                                      QLineEdit::Normal, QString(), &ok);
        if (!ok)
            return;
    }
    m_append(o.insertText(value));
}

void ParamDialog::applyFilter()
{
    const QString needle = m_filter->text().trimmed();
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_tree->topLevelItem(i);
        const bool match = needle.isEmpty() || item->text(0).contains(needle, Qt::CaseInsensitive)
                           || item->text(1).contains(needle, Qt::CaseInsensitive);
        item->setHidden(!match);
    }
}

} // namespace ncssh::gui
