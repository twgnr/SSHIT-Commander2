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
#include <QSignalBlocker>
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
    m_replaceAll = new QCheckBox(_t("Alle Vorkommen ersetzen"), searchBox);
    m_replaceAll->setChecked(true);
    // Fertige Regex-Vorlagen — sie fuellen Modus/Suchen/Ersetzen und bleiben
    // danach frei editierbar; die Auswahl springt zurueck (Aktion, kein Zustand).
    m_preset = new QComboBox(searchBox);
    for (const auto &preset : regexPresets())
        m_preset->addItem(preset.label);
    connect(m_preset, &QComboBox::currentIndexChanged, this,
            &BulkRenameDialog::applyRegexPreset);
    m_regexError = new QLabel(searchBox);
    m_regexError->setObjectName(QStringLiteral("Muted"));
    m_regexError->setWordWrap(true);
    searchForm->addRow(_t("Suchen"), m_search);
    searchForm->addRow(_t("Ersetzen"), m_replace);
    searchForm->addRow(_t("Modus"), m_matchMode);
    searchForm->addRow(_t("Regex-Vorlage"), m_preset);
    searchForm->addRow(QString(), m_ignoreCase);
    searchForm->addRow(QString(), m_replaceAll);
    searchForm->addRow(QString(), m_regexError);
    top->addWidget(searchBox, 1);

    // --- Entfernen, Zuschneiden, Einfuegen ---
    auto *editBox = new QGroupBox(_t("Entfernen & Einfügen"), this);
    auto *editForm = new QFormLayout(editBox);
    m_removeText = new QLineEdit(editBox);
    m_removeText->setPlaceholderText(_t("Text, der entfernt wird"));
    m_trimStart = new QSpinBox(editBox);
    m_trimStart->setRange(0, 200);
    m_trimEnd = new QSpinBox(editBox);
    m_trimEnd->setRange(0, 200);
    m_insertText = new QLineEdit(editBox);
    m_insertPos = new QSpinBox(editBox);
    m_insertPos->setRange(-200, 200);
    m_insertPos->setToolTip(_t("Position; negativ zählt vom Ende"));
    m_scope = new QComboBox(editBox);
    m_scope->addItem(_t("Nur Name"), QStringLiteral("name"));
    m_scope->addItem(_t("Nur Endung"), QStringLiteral("ext"));
    m_scope->addItem(_t("Ganzer Name"), QStringLiteral("full"));
    editForm->addRow(_t("Entfernen"), m_removeText);
    editForm->addRow(_t("Vorne kürzen"), m_trimStart);
    editForm->addRow(_t("Hinten kürzen"), m_trimEnd);
    editForm->addRow(_t("Einfügen"), m_insertText);
    editForm->addRow(_t("an Position"), m_insertPos);
    editForm->addRow(_t("Geltungsbereich"), m_scope);
    top->addWidget(editBox, 1);

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
    m_numPosition->addItem(_t("an Position"), QStringLiteral("at"));
    m_numPosition->addItem(_t("an Position (ersetzt)"), QStringLiteral("at_replace"));
    m_numPos = new QSpinBox(numBox);
    m_numPos->setRange(-200, 200);
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
    numForm->addRow(_t("Zeichenposition"), m_numPos);
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
    auto *cancel = new QPushButton(_t("Schließen"), this);
    m_undoButton = new QPushButton(_t("Rückgängig"), this);
    m_undoButton->setEnabled(false);
    m_undoButton->setVisible(false);
    m_applyButton = new QPushButton(_t("Umbenennen"), this);
    m_applyButton->setDefault(true);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_undoButton, &QPushButton::clicked, this, &BulkRenameDialog::undo);
    connect(m_applyButton, &QPushButton::clicked, this, &BulkRenameDialog::apply);
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(m_undoButton);
    buttons->addWidget(m_applyButton);
    layout->addLayout(buttons);

    // Live-Vorschau an alle Eingaben haengen
    for (QLineEdit *e : {m_search, m_replace, m_prefix, m_suffix, m_numSep, m_extValue,
                         m_removeText, m_insertText})
        connect(e, &QLineEdit::textChanged, this, &BulkRenameDialog::updatePreview);
    for (QComboBox *c : {m_matchMode, m_caseMode, m_spaceMode, m_extMode, m_numPosition,
                         m_sortMode, m_scope})
        connect(c, &QComboBox::currentIndexChanged, this, &BulkRenameDialog::updatePreview);
    for (QCheckBox *c : {m_ignoreCase, m_numbering, m_autoResolve, m_replaceAll})
        connect(c, &QCheckBox::toggled, this, &BulkRenameDialog::updatePreview);
    for (QSpinBox *s : {m_start, m_step, m_width, m_trimStart, m_trimEnd, m_insertPos,
                        m_numPos})
        connect(s, &QSpinBox::valueChanged, this, &BulkRenameDialog::updatePreview);

    updatePreview();
}

// Fertige Muster fuer haeufige Aufraeumarbeiten — spart dem Nutzer das Regex.
const std::vector<BulkRenameDialog::RegexPreset> &BulkRenameDialog::regexPresets()
{
    static const std::vector<RegexPreset> presets = {
        {_t("— Vorlage wählen —"), {}, {}, false},
        {_t("Leerzeichen → Unterstrich"), QStringLiteral("\\s+"), QStringLiteral("_"), false},
        {_t("Unterstrich → Leerzeichen"), QStringLiteral("_+"), QStringLiteral(" "), false},
        {_t("Bindestrich → Leerzeichen"), QStringLiteral("-+"), QStringLiteral(" "), false},
        {_t("Mehrfache Leerzeichen → eins"), QStringLiteral(" {2,}"), QStringLiteral(" "), false},
        {_t("Leerzeichen am Rand entfernen"), QStringLiteral("^\\s+|\\s+$"), {}, false},
        {_t("Mehrfach _ oder - → eins"), QStringLiteral("([_-])\\1+"), QStringLiteral("\\1"), false},
        {_t("Klammern + Inhalt entfernen"), QStringLiteral("\\s*[\\(\\[][^\\)\\]]*[\\)\\]]"), {}, false},
        {_t("Führende Nummer entfernen"), QStringLiteral("^\\d+[\\s._-]*"), {}, false},
        {_t("Sonderzeichen → Unterstrich"), QStringLiteral("[^\\w.\\- ]+"), QStringLiteral("_"), false},
        {_t("Leerzeichen vor Großbuchstaben"), QStringLiteral("(?<=[a-z0-9])(?=[A-Z])"),
         QStringLiteral(" "), false},
        {_t("Ziffern entfernen"), QStringLiteral("\\d+"), {}, false},
        {_t("'- Kopie'-Suffix entfernen"),
         QStringLiteral("\\s*[-–]\\s*Kopie(\\s*\\(\\d+\\))?$"), {}, true},
        {_t("'copy'-Suffix entfernen"),
         QStringLiteral("[\\s_-]*copy(\\s*\\(\\d+\\))?$"), {}, true},
    };
    return presets;
}

void BulkRenameDialog::applyRegexPreset(int index)
{
    const auto &presets = regexPresets();
    if (index <= 0 || index >= int(presets.size()))
        return;
    const RegexPreset &preset = presets[size_t(index)];
    m_matchMode->setCurrentIndex(m_matchMode->findData(QStringLiteral("regex")));
    m_search->setText(preset.search);
    m_replace->setText(preset.replace);
    m_ignoreCase->setChecked(preset.ignoreCase);
    // Auswahl zuruecksetzen: die Vorlage ist eine Aktion, kein Zustand.
    QSignalBlocker blocker(m_preset);
    m_preset->setCurrentIndex(0);
}

core::RenameOptions BulkRenameDialog::collectOptions() const
{
    core::RenameOptions o;
    o.search = m_search->text();
    o.replace = m_replace->text();
    o.matchMode = m_matchMode->currentData().toString();
    o.ignoreCase = m_ignoreCase->isChecked();
    o.replaceAll = m_replaceAll->isChecked();
    o.removeText = m_removeText->text();
    o.trimStart = m_trimStart->value();
    o.trimEnd = m_trimEnd->value();
    o.insertText = m_insertText->text();
    o.insertPos = m_insertPos->value();
    o.scope = m_scope->currentData().toString();
    o.numPos = m_numPos->value();
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
    // Ungueltiges Regex direkt melden, statt eine leere Vorschau zu zeigen.
    m_regexError->clear();
    if (m_matchMode->currentData().toString() == QLatin1String("regex")
        && !m_search->text().isEmpty()) {
        if (const auto error = core::validateRegex(m_search->text())) {
            m_regexError->setText(_t("Regex-Fehler: %1").arg(*error));
            m_applyButton->setEnabled(false);
            return;
        }
    }
    // "an Position" braucht die Zeichenposition, sonst ist sie ohne Wirkung.
    const QString numPosition = m_numPosition->currentData().toString();
    m_numPos->setEnabled(numPosition.startsWith(QLatin1String("at")));

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
    // Umbenennen nur zulassen, wenn es etwas zu tun gibt und nichts kollidiert.
    m_applyButton->setEnabled(changed > 0 && collisions.isEmpty());
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
    if (changes.empty())
        return;
    if (QMessageBox::question(
            this, _t("Umbenennen"),
            _t("%1 Eintrag/Einträge wirklich umbenennen?").arg(changes.size()))
        != QMessageBox::Yes)
        return;

    // Rueckabwicklung merken (neu -> alt) — erst nach dem Erfolg aktivieren.
    std::vector<RenamePair> undoJobs;
    for (const auto &[oldName, newName] : changes)
        undoJobs.emplace_back(newName, oldName);

    const std::vector<RenamePair> steps = core::planSafeOrder(changes, existing);
    runRenames(steps, int(changes.size()), [this, undoJobs](int count) {
        m_undoJobs = undoJobs;
        m_names.clear();
        for (const auto &[oldName, newName] : m_pairs)
            m_names.push_back(newName);
        m_undoButton->setVisible(true);
        m_undoButton->setEnabled(!m_undoJobs.empty());
        updatePreview();          // Vorschau auf den neuen Stand bringen
        QMessageBox::information(
            this, _t("Fertig"),
            _t("%1 Eintrag/Einträge umbenannt.\nÜber „Rückgängig“ lässt sich das "
               "zurücknehmen.").arg(count));
    }, m_applyButton);
}

void BulkRenameDialog::undo()
{
    if (m_undoJobs.empty())
        return;
    QSet<QString> existing;
    for (const auto &[from, to] : m_undoJobs)
        existing.insert(from);
    const std::vector<RenamePair> steps = core::planSafeOrder(m_undoJobs, existing);
    const int count = int(m_undoJobs.size());
    runRenames(steps, count, [this](int done) {
        m_names.clear();
        for (const auto &[from, to] : m_undoJobs)
            m_names.push_back(to);
        m_undoJobs.clear();
        m_undoButton->setVisible(false);
        m_undoButton->setEnabled(false);
        updatePreview();
        QMessageBox::information(this, _t("Rückgängig"),
                                 _t("%1 Umbenennung(en) zurückgenommen.").arg(done));
    }, m_undoButton);
}

void BulkRenameDialog::runRenames(const std::vector<RenamePair> &steps, int count,
                                  const std::function<void(int)> &onDone,
                                  QPushButton *button)
{
    core::FileSystemProvider *provider = m_provider;
    const QString dir = m_dir;
    button->setEnabled(false);
    m_bridge->run(
        [provider, dir, steps] {
            for (const auto &[from, to] : steps)
                provider->rename(provider->join(dir, from), provider->join(dir, to));
        },
        [onDone, count] { onDone(count); },
        [this, button](const QString &err) {
            button->setEnabled(true);
            QMessageBox::critical(this, _t("Umbenennen fehlgeschlagen"), err);
        });
}

} // namespace ncssh::gui
