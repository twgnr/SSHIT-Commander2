#include "ncssh/gui/known_hosts_dialog.hpp"

#include "ncssh/core/i18n.hpp"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;

KnownHostsDialog::KnownHostsDialog(core::HostKeyStore *store, QWidget *parent)
    : QDialog(parent), m_store(store)
{
    setWindowTitle(_t("Bekannte Host-Keys"));
    resize(780, 460);

    auto *layout = new QVBoxLayout(this);
    auto *info = new QLabel(
        _t("Diese Fingerprints gelten als vertrauenswürdig (Trust-on-First-Use). "
           "Einen Eintrag entfernen, wenn der Server neu aufgesetzt wurde."), this);
    info->setObjectName(QStringLiteral("Muted"));
    info->setWordWrap(true);
    layout->addWidget(info);

    m_table = new QTableWidget(0, 2, this);
    m_table->setHorizontalHeaderLabels({_t("Host / Port / Algorithmus"), _t("Fingerprint")});
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    layout->addWidget(m_table, 1);

    auto *buttons = new QHBoxLayout();
    auto *removeBtn = new QPushButton(_t("Eintrag entfernen"), this);
    auto *closeBtn = new QPushButton(_t("Schließen"), this);
    closeBtn->setDefault(true);
    connect(removeBtn, &QPushButton::clicked, this, &KnownHostsDialog::removeSelected);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(removeBtn);
    buttons->addStretch(1);
    buttons->addWidget(closeBtn);
    layout->addLayout(buttons);

    reload();
}

void KnownHostsDialog::reload()
{
    m_table->setRowCount(0);
    const auto entries = m_store->entries();
    QStringList keys = entries.keys();
    keys.sort();
    int row = 0;
    for (const QString &key : keys) {
        m_table->insertRow(row);
        auto *keyItem = new QTableWidgetItem(key);
        keyItem->setData(Qt::UserRole, key);
        m_table->setItem(row, 0, keyItem);
        m_table->setItem(row, 1, new QTableWidgetItem(entries.value(key)));
        ++row;
    }
}

void KnownHostsDialog::removeSelected()
{
    const int row = m_table->currentRow();
    if (row < 0)
        return;
    const QString key = m_table->item(row, 0)->data(Qt::UserRole).toString();
    if (QMessageBox::question(this, _t("Entfernen"),
                              QStringLiteral("Eintrag \"%1\" entfernen?").arg(key))
        != QMessageBox::Yes)
        return;
    m_store->removeKey(key);
    m_store->save();
    reload();
}

} // namespace ncssh::gui
