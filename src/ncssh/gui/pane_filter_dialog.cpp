#include "ncssh/gui/pane_filter_dialog.hpp"

#include "ncssh/core/i18n.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;
using core::PaneFilter;

namespace {

constexpr int kSortLevels = 4;

void fillSizeUnits(QComboBox *combo)
{
    combo->addItem(QStringLiteral("B"), qint64(1));
    combo->addItem(QStringLiteral("KB"), qint64(1024));
    combo->addItem(QStringLiteral("MB"), qint64(1024) * 1024);
    combo->addItem(QStringLiteral("GB"), qint64(1024) * 1024 * 1024);
}

// Bytes in Wert + groesste glatte Einheit (1536 B -> 1,5 KB).
void setSize(QDoubleSpinBox *spin, QComboBox *unit, qint64 bytes)
{
    int index = 0;
    for (int i = unit->count() - 1; i > 0; --i) {
        if (bytes >= unit->itemData(i).toLongLong()) {
            index = i;
            break;
        }
    }
    unit->setCurrentIndex(index);
    spin->setValue(double(bytes) / double(unit->itemData(index).toLongLong()));
}

qint64 sizeOf(const QDoubleSpinBox *spin, const QComboBox *unit)
{
    return qint64(spin->value() * double(unit->currentData().toLongLong()) + 0.5);
}

} // namespace

PaneFilterDialog::PaneFilterDialog(const core::PaneFilter &filter,
                                   const QList<core::SortKey> &sortKeys, bool dirsFirst,
                                   const QList<QPair<QString, QString>> &columns,
                                   QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(_t("Filter und Sortierung"));
    auto *layout = new QVBoxLayout(this);

    // --- Filter -------------------------------------------------------------
    auto *filterBox = new QGroupBox(_t("Filter"), this);
    auto *form = new QFormLayout(filterBox);

    m_kind = new QComboBox(filterBox);
    m_kind->addItem(_t("Dateien und Ordner"), int(PaneFilter::Kind::All));
    m_kind->addItem(_t("Nur Dateien"), int(PaneFilter::Kind::FilesOnly));
    m_kind->addItem(_t("Nur Ordner"), int(PaneFilter::Kind::DirsOnly));
    form->addRow(_t("Anzeigen:"), m_kind);

    m_names = new QLineEdit(filterBox);
    m_names->setPlaceholderText(_t("z. B. bericht; *.log — Platzhalter * und ?"));
    m_names->setToolTip(_t("Mehrere Namen mit ; trennen. Ohne Platzhalter genügt es, "
                           "wenn der Name den Text enthält."));
    form->addRow(_t("Name:"), m_names);

    m_starts = new QLineEdit(filterBox);
    m_starts->setPlaceholderText(_t("mehrere mit ; trennen"));
    form->addRow(_t("Beginnt mit:"), m_starts);

    m_ends = new QLineEdit(filterBox);
    m_ends->setPlaceholderText(_t("mehrere mit ; trennen"));
    form->addRow(_t("Endet mit:"), m_ends);

    m_regex = new QLineEdit(filterBox);
    m_regex->setPlaceholderText(_t("z. B. ^IMG_\\d{4}"));
    form->addRow(_t("Regulärer Ausdruck:"), m_regex);

    m_caseSensitive = new QCheckBox(_t("Groß-/Kleinschreibung beachten"), filterBox);
    form->addRow(QString(), m_caseSensitive);

    m_extensions = new QLineEdit(filterBox);
    m_extensions->setPlaceholderText(_t("z. B. txt, log, cpp"));
    form->addRow(_t("Dateiendungen:"), m_extensions);

    // Datum: Feld + Modus, darunter je nach Modus Zeitraum oder Zeitspanne.
    auto *dateRow = new QHBoxLayout();
    m_dateField = new QComboBox(filterBox);
    m_dateField->addItem(_t("Geändert"), QStringLiteral("modified"));
    m_dateField->addItem(_t("Erstellt"), QStringLiteral("created"));
    m_dateMode = new QComboBox(filterBox);
    m_dateMode->addItem(_t("beliebig"), int(PaneFilter::DateMode::Off));
    m_dateMode->addItem(_t("im Zeitraum"), int(PaneFilter::DateMode::Between));
    m_dateMode->addItem(_t("älter als"), int(PaneFilter::DateMode::OlderThan));
    m_dateMode->addItem(_t("jünger als"), int(PaneFilter::DateMode::NewerThan));
    dateRow->addWidget(m_dateField);
    dateRow->addWidget(m_dateMode, 1);
    form->addRow(_t("Datum:"), dateRow);

    m_dateStack = new QStackedWidget(filterBox);
    m_dateStack->addWidget(new QWidget(m_dateStack));   // 0: aus
    auto *range = new QWidget(m_dateStack);             // 1: Zeitraum
    auto *rangeLayout = new QHBoxLayout(range);
    rangeLayout->setContentsMargins(0, 0, 0, 0);
    m_from = new QDateTimeEdit(range);
    m_to = new QDateTimeEdit(range);
    for (QDateTimeEdit *edit : {m_from, m_to}) {
        edit->setCalendarPopup(true);
        edit->setDisplayFormat(QStringLiteral("dd.MM.yyyy HH:mm"));
    }
    rangeLayout->addWidget(new QLabel(_t("von"), range));
    rangeLayout->addWidget(m_from, 1);
    rangeLayout->addWidget(new QLabel(_t("bis"), range));
    rangeLayout->addWidget(m_to, 1);
    m_dateStack->addWidget(range);
    auto *span = new QWidget(m_dateStack);              // 2: Zeitspanne
    auto *spanLayout = new QHBoxLayout(span);
    spanLayout->setContentsMargins(0, 0, 0, 0);
    m_amount = new QSpinBox(span);
    m_amount->setRange(0, 100000);
    m_unit = new QComboBox(span);
    m_unit->addItem(_t("Minuten"), QStringLiteral("minutes"));
    m_unit->addItem(_t("Stunden"), QStringLiteral("hours"));
    m_unit->addItem(_t("Tage"), QStringLiteral("days"));
    m_unit->addItem(_t("Wochen"), QStringLiteral("weeks"));
    m_unit->addItem(_t("Monate"), QStringLiteral("months"));
    m_unit->addItem(_t("Jahre"), QStringLiteral("years"));
    spanLayout->addWidget(m_amount);
    spanLayout->addWidget(m_unit, 1);
    m_dateStack->addWidget(span);
    form->addRow(QString(), m_dateStack);

    // Groesse: optionale Unter- und Obergrenze.
    auto makeSizeRow = [filterBox](QCheckBox *&on, QDoubleSpinBox *&spin, QComboBox *&unit,
                                   const QString &label) {
        auto *row = new QHBoxLayout();
        on = new QCheckBox(label, filterBox);
        spin = new QDoubleSpinBox(filterBox);
        spin->setRange(0, 1e9);
        spin->setDecimals(2);
        unit = new QComboBox(filterBox);
        fillSizeUnits(unit);
        unit->setCurrentIndex(2);   // MB
        row->addWidget(on);
        row->addWidget(spin, 1);
        row->addWidget(unit);
        return row;
    };
    auto *sizeLayout = new QVBoxLayout();
    sizeLayout->addLayout(makeSizeRow(m_minOn, m_min, m_minUnit, _t("ab")));
    sizeLayout->addLayout(makeSizeRow(m_maxOn, m_max, m_maxUnit, _t("bis")));
    form->addRow(_t("Größe:"), sizeLayout);

    m_applyToDirs = new QCheckBox(_t("Namens- und Datumsregeln auch auf Ordner anwenden"),
                                  filterBox);
    m_applyToDirs->setToolTip(_t("Aus: Ordner bleiben sichtbar, damit man trotz Filter "
                                 "weiter navigieren kann. Größe und Endung gelten nur "
                                 "für Dateien."));
    form->addRow(QString(), m_applyToDirs);
    layout->addWidget(filterBox);

    // --- Sortierung -----------------------------------------------------------
    auto *sortBox = new QGroupBox(_t("Sortierung"), this);
    auto *grid = new QGridLayout(sortBox);
    for (int level = 0; level < kSortLevels; ++level) {
        SortRow row;
        row.column = new QComboBox(sortBox);
        if (level > 0)
            row.column->addItem(QStringLiteral("—"), QString());
        for (const auto &[id, label] : columns)
            row.column->addItem(label, id);
        row.direction = new QComboBox(sortBox);
        row.direction->addItem(_t("aufsteigend"), true);
        row.direction->addItem(_t("absteigend"), false);
        grid->addWidget(new QLabel(level == 0 ? _t("Sortieren nach:") : _t("dann nach:"),
                                   sortBox),
                        level, 0);
        grid->addWidget(row.column, level, 1);
        grid->addWidget(row.direction, level, 2);
        connect(row.column, &QComboBox::currentIndexChanged, this, [this] { updateState(); });
        m_sortRows.push_back(row);
    }
    m_dirsFirst = new QCheckBox(_t("Ordner zuerst"), sortBox);
    grid->addWidget(m_dirsFirst, kSortLevels, 1, 1, 2);
    grid->setColumnStretch(1, 1);
    layout->addWidget(sortBox);

    m_error = new QLabel(this);
    m_error->setObjectName(QStringLiteral("FilterError"));
    m_error->setWordWrap(true);
    m_error->setStyleSheet(QStringLiteral("color: #e5534b;"));
    m_error->setVisible(false);
    layout->addWidget(m_error);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel
                                             | QDialogButtonBox::Apply
                                             | QDialogButtonBox::Reset,
                                         this);
    m_ok = buttons->button(QDialogButtonBox::Ok);
    m_apply = buttons->button(QDialogButtonBox::Apply);
    m_apply->setText(_t("Anwenden"));
    buttons->button(QDialogButtonBox::Reset)->setText(_t("Zurücksetzen"));
    buttons->button(QDialogButtonBox::Reset)->setToolTip(
        _t("Alle Filter entfernen und nach Name sortieren"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_apply, &QPushButton::clicked, this, &PaneFilterDialog::applied);
    connect(buttons->button(QDialogButtonBox::Reset), &QPushButton::clicked, this,
            [this] { load(PaneFilter{}, {core::SortKey{}}, true); });
    layout->addWidget(buttons);

    load(filter, sortKeys, dirsFirst);

    // Jede Aenderung prueft sofort (z. B. Regex) und schaltet Felder um.
    for (QLineEdit *edit : {m_names, m_starts, m_ends, m_regex, m_extensions})
        connect(edit, &QLineEdit::textChanged, this, [this] { updateState(); });
    for (QComboBox *combo : {m_kind, m_dateMode, m_minUnit, m_maxUnit})
        connect(combo, &QComboBox::currentIndexChanged, this, [this] { updateState(); });
    for (QCheckBox *box : {m_minOn, m_maxOn, m_caseSensitive})
        connect(box, &QCheckBox::toggled, this, [this] { updateState(); });
    for (QDoubleSpinBox *spin : {m_min, m_max})
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this] { updateState(); });
    for (QDateTimeEdit *edit : {m_from, m_to})
        connect(edit, &QDateTimeEdit::dateTimeChanged, this, [this] { updateState(); });
    updateState();
}

void PaneFilterDialog::load(const core::PaneFilter &filter,
                            const QList<core::SortKey> &sortKeys, bool dirsFirst)
{
    m_kind->setCurrentIndex(m_kind->findData(int(filter.kind)));
    m_names->setText(filter.names);
    m_starts->setText(filter.startsWith);
    m_ends->setText(filter.endsWith);
    m_regex->setText(filter.regex);
    m_caseSensitive->setChecked(filter.caseSensitive);
    m_extensions->setText(filter.extensions);
    m_dateField->setCurrentIndex(qMax(0, m_dateField->findData(filter.dateField)));
    m_dateMode->setCurrentIndex(m_dateMode->findData(int(filter.dateMode)));
    const QDateTime now = QDateTime::currentDateTime();
    m_from->setDateTime(filter.from.isValid() ? filter.from
                                              : QDateTime(now.date().addDays(-7), QTime(0, 0)));
    m_to->setDateTime(filter.to.isValid() ? filter.to : QDateTime(now.date(), QTime(23, 59)));
    m_amount->setValue(filter.amount);
    m_unit->setCurrentIndex(qMax(0, m_unit->findData(filter.unit)));
    m_minOn->setChecked(filter.minSize >= 0);
    if (filter.minSize >= 0)
        setSize(m_min, m_minUnit, filter.minSize);
    m_maxOn->setChecked(filter.maxSize >= 0);
    if (filter.maxSize >= 0)
        setSize(m_max, m_maxUnit, filter.maxSize);
    m_applyToDirs->setChecked(filter.applyToDirs);

    for (size_t level = 0; level < m_sortRows.size(); ++level) {
        const SortRow &row = m_sortRows[level];
        if (level < size_t(sortKeys.size())) {
            const core::SortKey &key = sortKeys.at(qsizetype(level));
            row.column->setCurrentIndex(qMax(0, row.column->findData(key.column)));
            row.direction->setCurrentIndex(key.ascending ? 0 : 1);
        } else {
            row.column->setCurrentIndex(0);   // "—" (Stufe 1: erste Spalte = Name)
            row.direction->setCurrentIndex(0);
        }
    }
    m_dirsFirst->setChecked(dirsFirst);
    updateState();
}

void PaneFilterDialog::updateState()
{
    m_dateStack->setCurrentIndex(
        m_dateMode->currentData().toInt() == int(PaneFilter::DateMode::Off)       ? 0
        : m_dateMode->currentData().toInt() == int(PaneFilter::DateMode::Between) ? 1
                                                                                   : 2);
    m_dateField->setEnabled(m_dateMode->currentData().toInt() != int(PaneFilter::DateMode::Off));
    m_min->setEnabled(m_minOn->isChecked());
    m_minUnit->setEnabled(m_minOn->isChecked());
    m_max->setEnabled(m_maxOn->isChecked());
    m_maxUnit->setEnabled(m_maxOn->isChecked());
    // Folgestufen erst waehlbar, wenn die vorige belegt ist.
    bool previousSet = true;
    for (const SortRow &row : m_sortRows) {
        row.column->setEnabled(previousSet);
        const bool set = previousSet && !row.column->currentData().toString().isEmpty();
        row.direction->setEnabled(set);
        previousSet = set;
    }

    const QString error = filter().validate();
    m_error->setText(error);
    m_error->setVisible(!error.isEmpty());
    if (m_ok)
        m_ok->setEnabled(error.isEmpty());
    if (m_apply)
        m_apply->setEnabled(error.isEmpty());
}

core::PaneFilter PaneFilterDialog::filter() const
{
    PaneFilter f;
    f.kind = PaneFilter::Kind(m_kind->currentData().toInt());
    f.names = m_names->text().trimmed();
    f.startsWith = m_starts->text().trimmed();
    f.endsWith = m_ends->text().trimmed();
    f.regex = m_regex->text();
    f.caseSensitive = m_caseSensitive->isChecked();
    f.extensions = m_extensions->text().trimmed();
    f.dateMode = PaneFilter::DateMode(m_dateMode->currentData().toInt());
    f.dateField = m_dateField->currentData().toString();
    if (f.dateMode == PaneFilter::DateMode::Between) {
        f.from = m_from->dateTime();
        f.to = m_to->dateTime();
    }
    f.amount = m_amount->value();
    f.unit = m_unit->currentData().toString();
    f.minSize = m_minOn->isChecked() ? sizeOf(m_min, m_minUnit) : -1;
    f.maxSize = m_maxOn->isChecked() ? sizeOf(m_max, m_maxUnit) : -1;
    f.applyToDirs = m_applyToDirs->isChecked();
    return f;
}

QList<core::SortKey> PaneFilterDialog::sortKeys() const
{
    QList<core::SortKey> keys;
    QStringList used;
    for (const SortRow &row : m_sortRows) {
        const QString column = row.column->currentData().toString();
        if (column.isEmpty())
            break;   // "—" beendet die Kette
        if (used.contains(column))
            continue;   // doppelte Stufe waere wirkungslos
        used << column;
        keys << core::SortKey{column, row.direction->currentData().toBool()};
    }
    if (keys.isEmpty())
        keys << core::SortKey{};
    return keys;
}

bool PaneFilterDialog::dirsFirst() const
{
    return m_dirsFirst->isChecked();
}

} // namespace ncssh::gui
