#include "ncssh/gui/command_palette.hpp"

#include "ncssh/core/i18n.hpp"
#include "ncssh/gui/command_builder.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;
using core::CommandSpec;

CommandPalette::CommandPalette(const QString &osType, QWidget *parent)
    : QDialog(parent), m_osType(osType)
{
    setWindowTitle(_t("Befehlspalette"));
    resize(940, 560);

    auto *layout = new QVBoxLayout(this);

    auto *filterRow = new QHBoxLayout();
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(_t("Suche (Name, Kategorie, Beschreibung)…"));
    connect(m_search, &QLineEdit::textChanged, this, &CommandPalette::refill);
    m_osFilter = new QComboBox(this);
    m_osFilter->addItem(_t("Aktuelles OS"), osType);
    m_osFilter->addItem(_t("Beide"), QStringLiteral("all"));
    m_osFilter->addItem(_t("Nur Linux/Unix"), QStringLiteral("posix"));
    m_osFilter->addItem(_t("Nur Windows"), QStringLiteral("windows"));
    m_osFilter->addItem(_t("Plattformübergreifend"), QStringLiteral("any"));
    connect(m_osFilter, &QComboBox::currentIndexChanged, this, &CommandPalette::refill);
    filterRow->addWidget(m_search, 1);
    filterRow->addWidget(m_osFilter);
    layout->addLayout(filterRow);

    m_table = new QTableWidget(0, 4, this);
    m_table->setHorizontalHeaderLabels({_t("Befehl"), _t("Kategorie"), _t("Plattform"),
                                        _t("Beschreibung")});
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSortingEnabled(true);
    m_table->setAlternatingRowColors(true);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] {
        const auto spec = currentSpec();
        m_detail->setText(spec ? QStringLiteral("%1\n%2").arg(spec->templateText,
                                                             spec->example)
                               : QString());
    });
    connect(m_table, &QTableWidget::cellDoubleClicked, this,
            [this](int, int) { openBuilder(false); });
    layout->addWidget(m_table, 1);

    m_detail = new QLabel(this);
    m_detail->setObjectName(QStringLiteral("Muted"));
    m_detail->setWordWrap(true);
    m_detail->setMinimumHeight(40);
    layout->addWidget(m_detail);

    auto *buttons = new QHBoxLayout();
    auto *cancel = new QPushButton(_t("Abbrechen"), this);
    auto *builderBtn = new QPushButton(_t("Assistent …"), this);
    auto *insertBtn = new QPushButton(_t("Einfügen"), this);
    insertBtn->setDefault(true);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(builderBtn, &QPushButton::clicked, this, [this] { openBuilder(false); });
    connect(insertBtn, &QPushButton::clicked, this, [this] {
        if (const auto spec = currentSpec()) {
            m_command = spec->templateText;
            m_runDirectly = false;
            m_dangerous = spec->danger;
            accept();
        }
    });
    buttons->addWidget(cancel);
    buttons->addStretch(1);
    buttons->addWidget(builderBtn);
    buttons->addWidget(insertBtn);
    layout->addLayout(buttons);

    refill();
}

void CommandPalette::refill()
{
    const QString filter = m_osFilter->currentData().toString();
    const QString needle = m_search->text().trimmed().toLower();

    m_shown.clear();
    const std::vector<CommandSpec> source =
        (filter == QLatin1String("all")) ? core::catalog() : core::commandsFor(filter);
    for (const CommandSpec &spec : source) {
        if (!needle.isEmpty() && !spec.searchText().toLower().contains(needle))
            continue;
        m_shown.push_back(spec);
    }

    m_table->setSortingEnabled(false);
    m_table->setRowCount(0);
    for (int i = 0; i < int(m_shown.size()); ++i) {
        const CommandSpec &spec = m_shown[i];
        m_table->insertRow(i);
        auto *nameItem = new QTableWidgetItem(spec.name);
        nameItem->setData(Qt::UserRole, i);
        if (spec.danger) {  // destruktive Befehle farbig markieren
            nameItem->setForeground(QColor(QStringLiteral("#ef4444")));
            nameItem->setToolTip(_t("Achtung: destruktiver Befehl"));
        }
        m_table->setItem(i, 0, nameItem);
        m_table->setItem(i, 1, new QTableWidgetItem(spec.category));
        m_table->setItem(i, 2, new QTableWidgetItem(spec.platform));
        m_table->setItem(i, 3, new QTableWidgetItem(spec.description));
    }
    m_table->setSortingEnabled(true);
    m_detail->setText(QStringLiteral("%1 Befehle").arg(m_shown.size()));
}

std::optional<CommandSpec> CommandPalette::currentSpec() const
{
    const int row = m_table->currentRow();
    if (row < 0 || !m_table->item(row, 0))
        return std::nullopt;
    const int idx = m_table->item(row, 0)->data(Qt::UserRole).toInt();
    if (idx < 0 || idx >= int(m_shown.size()))
        return std::nullopt;
    return m_shown[idx];
}

void CommandPalette::openBuilder(bool)
{
    const auto spec = currentSpec();
    if (!spec)
        return;
    CommandBuilder builder(*spec, m_osType, this);
    if (builder.exec() == QDialog::Accepted) {
        m_command = builder.command();
        m_runDirectly = builder.runDirectly();
        m_dangerous = spec->danger;
        accept();
    }
}

} // namespace ncssh::gui
