#include "ncssh/gui/clipboard_manager.hpp"

#include "ncssh/core/i18n.hpp"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;

ClipboardManager::ClipboardManager(QObject *parent) : QObject(parent)
{
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this,
            &ClipboardManager::onClipboardChanged);
}

void ClipboardManager::onClipboardChanged()
{
    const QString text = QApplication::clipboard()->text();
    if (text.isEmpty() || text == m_lastSeen)
        return;
    m_lastSeen = text;
    // Duplikate nach oben ziehen statt doppelt zu fuehren.
    m_entries.removeAll(text);
    m_entries.prepend(text);
    while (m_entries.size() > kMaxEntries)
        m_entries.removeLast();
    emit changed();
}

void ClipboardManager::clear()
{
    m_entries.clear();
    emit changed();
}

void ClipboardManager::removeAt(int index)
{
    if (index < 0 || index >= m_entries.size())
        return;
    m_entries.removeAt(index);
    emit changed();
}

void ClipboardManager::activate(int index)
{
    if (index < 0 || index >= m_entries.size())
        return;
    const QString text = m_entries.at(index);
    m_lastSeen = text;  // eigenes Setzen nicht als neuen Eintrag werten
    QApplication::clipboard()->setText(text);
    m_entries.removeAt(index);
    m_entries.prepend(text);
    emit changed();
}

// ---------------------------------------------------------------------------

ClipboardDialog::ClipboardDialog(ClipboardManager *manager, QWidget *parent)
    : QDialog(parent), m_manager(manager)
{
    setWindowTitle(_t("Clipboard-Manager"));
    resize(760, 480);

    auto *layout = new QVBoxLayout(this);
    auto *info = new QLabel(
        _t("Doppelklick setzt den Eintrag als aktiven Inhalt der Zwischenablage."), this);
    info->setObjectName(QStringLiteral("Muted"));
    layout->addWidget(info);

    m_table = new QTableWidget(0, 2, this);
    m_table->setHorizontalHeaderLabels({QStringLiteral("#"), _t("Inhalt")});
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        m_manager->activate(row);
        m_chosenText = m_manager->entries().value(0);
        accept();
    });
    layout->addWidget(m_table, 1);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("Muted"));
    layout->addWidget(m_status);

    auto *buttons = new QHBoxLayout();
    auto *removeBtn = new QPushButton(_t("Eintrag löschen"), this);
    auto *clearBtn = new QPushButton(_t("Alle löschen"), this);
    auto *closeBtn = new QPushButton(_t("Schließen"), this);
    auto *insertBtn = new QPushButton(_t("Einfügen"), this);
    insertBtn->setDefault(true);
    connect(removeBtn, &QPushButton::clicked, this, [this] {
        m_manager->removeAt(m_table->currentRow());
    });
    connect(clearBtn, &QPushButton::clicked, this, [this] { m_manager->clear(); });
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(insertBtn, &QPushButton::clicked, this, [this] {
        const int row = m_table->currentRow();
        if (row < 0)
            return;
        m_chosenText = m_manager->entries().value(row);
        accept();
    });
    buttons->addWidget(removeBtn);
    buttons->addWidget(clearBtn);
    buttons->addStretch(1);
    buttons->addWidget(closeBtn);
    buttons->addWidget(insertBtn);
    layout->addLayout(buttons);

    connect(manager, &ClipboardManager::changed, this, &ClipboardDialog::reload);
    reload();
}

void ClipboardDialog::reload()
{
    m_table->setRowCount(0);
    const QStringList entries = m_manager->entries();
    for (int i = 0; i < entries.size(); ++i) {
        m_table->insertRow(i);
        m_table->setItem(i, 0, new QTableWidgetItem(QString::number(i + 1)));
        // Einzeilige Vorschau, damit die Tabelle kompakt bleibt.
        QString preview = entries[i];
        preview.replace(QLatin1Char('\n'), QStringLiteral(" ⏎ "));
        if (preview.size() > 200)
            preview = preview.left(200) + QStringLiteral(" …");
        auto *item = new QTableWidgetItem(preview);
        item->setToolTip(entries[i].left(2000));
        m_table->setItem(i, 1, item);
    }
    m_status->setText(QStringLiteral("%1 Einträge").arg(entries.size()));
}

} // namespace ncssh::gui
