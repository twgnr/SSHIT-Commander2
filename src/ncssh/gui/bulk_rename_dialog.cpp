#include "ncssh/gui/bulk_rename_dialog.hpp"

#include "ncssh/core/i18n.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;
using core::RenamePair;

BulkRenameDialog::BulkRenameDialog(AsyncBridge *bridge, core::FileSystemProvider *provider,
                                   const QString &dir, const std::vector<QString> &names,
                                   QWidget *parent)
    : QDialog(parent), m_bridge(bridge), m_provider(provider), m_dir(dir), m_names(names)
{
    setWindowTitle(_t("Massen-Umbenennen"));
    resize(940, 660);

    auto *layout = new QVBoxLayout(this);
    auto *top = new QHBoxLayout();

    // --- Suchen & Ersetzen ---
    auto *searchBox = new QGroupBox(_t("Suchen & Ersetzen"), this);
    auto *searchForm = new QFormLayout(searchBox);
    m_search = new QLineEdit(searchBox);
    m_replace = new QLineEdit(searchBox);
    m_matchMode = new QComboBox(searchBox);
    m_matchMode->addItem(_t("Text (wörtlich)"), QStringLiteral("text"));
    m_matchMode->addItem(_t("Platzhalter (* ?)"), QStringLiteral("wildcard"));
    m_matchMode->addItem(_t("Regex"), QStringLiteral("regex"));
    m_ignoreCase = new QCheckBox(_t("Groß/Klein egal"), searchBox);
    searchForm->addRow(_t("Suchen"), m_search);
    searchForm->addRow(_t("Ersetzen"), m_replace);
    searchForm->addRow(_t("Modus"), m_matchMode);
    searchForm->addRow(QString(), m_ignoreCase);
    top->addWidget(searchBox, 1);

    // --- Praefix/Suffix, Gross/Klein, Endung ---
    auto *textBox = new QGroupBox(_t("Text & Endung"), this);
    auto *textForm = new QFormLayout(textBox);
    m_prefix = new QLineEdit(textBox);
    m_suffix = new QLineEdit(textBox);
    m_caseMode = new QComboBox(textBox);
    m_caseMode->addItem(_t("unverändert"), QStringLiteral("none"));
    m_caseMode->addItem(_t("kleinschreiben"), QStringLiteral("lower"));
    m_caseMode->addItem(_t("GROSSSCHREIBEN"), QStringLiteral("upper"));
    m_caseMode->addItem(_t("Wortanfänge Groß"), QStringLiteral("title"));
    m_caseMode->addItem(_t("Satzanfang groß"), QStringLiteral("sentence"));
    m_spaceMode = new QComboBox(textBox);
    m_spaceMode->addItem(_t("Leerzeichen behalten"), QStringLiteral("none"));
    m_spaceMode->addItem(QStringLiteral("_"), QStringLiteral("underscore"));
    m_spaceMode->addItem(QStringLiteral("-"), QStringLiteral("dash"));
    m_spaceMode->addItem(_t("entfernen"), QStringLiteral("remove"));
    m_extMode = new QComboBox(textBox);
    m_extMode->addItem(_t("Endung unverändert"), QStringLiteral("none"));
    m_extMode->addItem(_t("Endung klein"), QStringLiteral("lower"));
    m_extMode->addItem(_t("Endung GROSS"), QStringLiteral("upper"));
    m_extMode->addItem(_t("Endung setzen"), QStringLiteral("set"));
    m_extValue = new QLineEdit(textBox);
    m_extValue->setPlaceholderText(QStringLiteral("jpg"));
    textForm->addRow(_t("Präfix"), m_prefix);
    textForm->addRow(_t("Suffix"), m_suffix);
    textForm->addRow(_t("Schreibweise"), m_caseMode);
    textForm->addRow(_t("Leerzeichen"), m_spaceMode);
    textForm->addRow(_t("Endung"), m_extMode);
    textForm->addRow(_t("neue Endung"), m_extValue);
    top->addWidget(textBox, 1);

    // --- Nummerierung ---
    auto *numBox = new QGroupBox(_t("Nummerierung"), this);
    auto *numForm = new QFormLayout(numBox);
    m_numbering = new QCheckBox(_t("aktiv"), numBox);
    m_start = new QSpinBox(numBox);
    m_start->setRange(0, 999999);
    m_start->setValue(1);
    m_step = new QSpinBox(numBox);
    m_step->setRange(1, 1000);
    m_step->setValue(1);
    m_width = new QSpinBox(numBox);
    m_width->setRange(1, 10);
    m_width->setValue(2);
    m_numSep = new QLineEdit(numBox);
    m_numSep->setPlaceholderText(QStringLiteral("_"));
    m_numPosition = new QComboBox(numBox);
    m_numPosition->addItem(_t("hinten"), QStringLiteral("suffix"));
    m_numPosition->addItem(_t("vorne"), QStringLiteral("prefix"));
    m_sortMode = new QComboBox(numBox);
    m_sortMode->addItem(_t("Eingabereihenfolge"), QStringLiteral("none"));
    m_sortMode->addItem(_t("Name"), QStringLiteral("name"));
    m_sortMode->addItem(_t("Name absteigend"), QStringLiteral("name_desc"));
    m_sortMode->addItem(_t("natürlich (1,2,10)"), QStringLiteral("natural"));
    m_sortMode->addItem(_t("Endung"), QStringLiteral("ext"));
    numForm->addRow(QString(), m_numbering);
    numForm->addRow(_t("Start"), m_start);
    numForm->addRow(_t("Schritt"), m_step);
    numForm->addRow(_t("Stellen"), m_width);
    numForm->addRow(_t("Trenner"), m_numSep);
    numForm->addRow(_t("Position"), m_numPosition);
    numForm->addRow(_t("Reihenfolge"), m_sortMode);
    top->addWidget(numBox, 1);

    layout->addLayout(top);

    m_autoResolve = new QCheckBox(_t("Konflikte automatisch nummerieren"), this);
    m_autoResolve->setChecked(true);
    layout->addWidget(m_autoResolve);

    // --- Vorschau ---
    m_preview = new QTableWidget(0, 2, this);
    m_preview->setHorizontalHeaderLabels({_t("Alt"), _t("Neu")});
    m_preview->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_preview->verticalHeader()->setVisible(false);
    m_preview->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_preview->setAlternatingRowColors(true);
    layout->addWidget(m_preview, 1);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("Muted"));
    layout->addWidget(m_status);

    auto *buttons = new QHBoxLayout();
    auto *cancel = new QPushButton(_t("Abbrechen"), this);
    auto *applyBtn = new QPushButton(_t("Umbenennen"), this);
    applyBtn->setDefault(true);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(applyBtn, &QPushButton::clicked, this, &BulkRenameDialog::apply);
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(applyBtn);
    layout->addLayout(buttons);

    // Live-Vorschau an alle Eingaben haengen
    for (QLineEdit *e : {m_search, m_replace, m_prefix, m_suffix, m_numSep, m_extValue})
        connect(e, &QLineEdit::textChanged, this, &BulkRenameDialog::updatePreview);
    for (QComboBox *c : {m_matchMode, m_caseMode, m_spaceMode, m_extMode, m_numPosition, m_sortMode})
        connect(c, &QComboBox::currentIndexChanged, this, &BulkRenameDialog::updatePreview);
    for (QCheckBox *c : {m_ignoreCase, m_numbering, m_autoResolve})
        connect(c, &QCheckBox::toggled, this, &BulkRenameDialog::updatePreview);
    for (QSpinBox *s : {m_start, m_step, m_width})
        connect(s, &QSpinBox::valueChanged, this, &BulkRenameDialog::updatePreview);

    updatePreview();
}

core::RenameOptions BulkRenameDialog::collectOptions() const
{
    core::RenameOptions o;
    o.search = m_search->text();
    o.replace = m_replace->text();
    o.matchMode = m_matchMode->currentData().toString();
    o.ignoreCase = m_ignoreCase->isChecked();
    o.prefix = m_prefix->text();
    o.suffix = m_suffix->text();
    o.caseMode = m_caseMode->currentData().toString();
    o.spaceMode = m_spaceMode->currentData().toString();
    o.numbering = m_numbering->isChecked();
    o.start = m_start->value();
    o.step = m_step->value();
    o.width = m_width->value();
    o.numSep = m_numSep->text();
    o.numPosition = m_numPosition->currentData().toString();
    o.extMode = m_extMode->currentData().toString();
    o.extValue = m_extValue->text();
    return o;
}

void BulkRenameDialog::updatePreview()
{
    // Sortierreihenfolge fuer die Nummernvergabe anwenden.
    const QString sortMode = m_sortMode->currentData().toString();
    const std::vector<int> order = core::sortIndices(m_names, sortMode);
    std::vector<QString> ordered;
    ordered.reserve(order.size());
    for (int idx : order)
        ordered.push_back(m_names[idx]);

    m_pairs = core::computeRenames(ordered, collectOptions());
    if (m_autoResolve->isChecked())
        m_pairs = core::autoResolveCollisions(m_pairs);
    const QSet<QString> collisions = core::findCollisions(m_pairs);

    m_preview->setRowCount(0);
    int changed = 0;
    for (int i = 0; i < int(m_pairs.size()); ++i) {
        const auto &[oldName, newName] = m_pairs[i];
        m_preview->insertRow(i);
        m_preview->setItem(i, 0, new QTableWidgetItem(oldName));
        auto *newItem = new QTableWidgetItem(newName);
        if (collisions.contains(newName))
            newItem->setForeground(QColor(QStringLiteral("#ef4444")));
        else if (oldName != newName)
            newItem->setForeground(QColor(QStringLiteral("#3fb950")));
        m_preview->setItem(i, 1, newItem);
        if (oldName != newName)
            ++changed;
    }
    m_status->setText(
        collisions.isEmpty()
            ? QStringLiteral("%1 von %2 Namen ändern sich").arg(changed).arg(m_pairs.size())
            : QStringLiteral("%1 Änderungen · %2 KONFLIKTE").arg(changed).arg(collisions.size()));
}

void BulkRenameDialog::apply()
{
    const QSet<QString> collisions = core::findCollisions(m_pairs);
    if (!collisions.isEmpty()) {
        QMessageBox::warning(this, _t("Konflikte"),
                             _t("Es gibt doppelte Zielnamen. Bitte Konflikte auflösen."));
        return;
    }
    // Nur echte Aenderungen, in gefahrloser Reihenfolge (Zyklen via Temp-Namen).
    std::vector<RenamePair> changes;
    QSet<QString> existing;
    for (const auto &p : m_pairs) {
        existing.insert(p.first);
        if (p.first != p.second)
            changes.push_back(p);
    }
    if (changes.empty()) {
        accept();
        return;
    }
    const std::vector<RenamePair> steps = core::planSafeOrder(changes, existing);

    core::FileSystemProvider *provider = m_provider;
    const QString dir = m_dir;
    m_bridge->run(
        [provider, dir, steps] {
            for (const auto &[from, to] : steps)
                provider->rename(provider->join(dir, from), provider->join(dir, to));
        },
        [this] { accept(); },
        [this](const QString &err) { QMessageBox::warning(this, _t("Fehler"), err); });
}

} // namespace ncssh::gui
