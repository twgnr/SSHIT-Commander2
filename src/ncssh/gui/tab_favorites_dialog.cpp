#include "ncssh/gui/tab_favorites_dialog.hpp"

#include "ncssh/core/i18n.hpp"

#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;

TabFavoritesDialog::TabFavoritesDialog(const std::vector<QJsonObject> &currentTabs,
                                       QWidget *parent)
    : QDialog(parent), m_currentTabs(currentTabs)
{
    setWindowTitle(_t("Tab-Favoriten"));
    resize(620, 440);
    m_store.load();

    auto *layout = new QVBoxLayout(this);
    auto *info = new QLabel(
        _t("Sichert die aktuell offenen Tabs als benannten Favoriten. "
           "Doppelklick stellt einen Favoriten wieder her."), this);
    info->setObjectName(QStringLiteral("Muted"));
    info->setWordWrap(true);
    layout->addWidget(info);

    m_list = new QListWidget(this);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        m_chosen = m_store.get(item->data(Qt::UserRole).toString());
        accept();
    });
    layout->addWidget(m_list, 1);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("Muted"));
    layout->addWidget(m_status);

    auto *buttons = new QHBoxLayout();
    auto *saveBtn = new QPushButton(_t("Aktuelle Tabs speichern …"), this);
    auto *renameBtn = new QPushButton(_t("Umbenennen"), this);
    auto *removeBtn = new QPushButton(_t("Löschen"), this);
    auto *cancel = new QPushButton(_t("Schließen"), this);
    auto *restoreBtn = new QPushButton(_t("Wiederherstellen"), this);
    restoreBtn->setDefault(true);
    connect(saveBtn, &QPushButton::clicked, this, &TabFavoritesDialog::saveCurrent);
    connect(renameBtn, &QPushButton::clicked, this, &TabFavoritesDialog::renameSelected);
    connect(removeBtn, &QPushButton::clicked, this, &TabFavoritesDialog::removeSelected);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(restoreBtn, &QPushButton::clicked, this, [this] {
        if (auto *item = m_list->currentItem()) {
            m_chosen = m_store.get(item->data(Qt::UserRole).toString());
            accept();
        }
    });
    buttons->addWidget(saveBtn);
    buttons->addWidget(renameBtn);
    buttons->addWidget(removeBtn);
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(restoreBtn);
    layout->addLayout(buttons);

    reload();
}

void TabFavoritesDialog::reload()
{
    m_list->clear();
    for (const QString &name : m_store.names()) {
        auto *item = new QListWidgetItem(
            _t("%1  (%2 Tabs)").arg(name).arg(m_store.count(name)), m_list);
        item->setData(Qt::UserRole, name);
    }
    m_status->setText(QStringLiteral("%1 Favorit(en)").arg(m_list->count()));
}

void TabFavoritesDialog::saveCurrent()
{
    if (m_currentTabs.empty()) {
        QMessageBox::information(this, _t("Favorit speichern"),
                                 _t("Keine Tabs zum Speichern."));
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(this, _t("Favorit speichern"),
                                               _t("Name:"), QLineEdit::Normal, QString(), &ok);
    if (!ok)
        return;
    if (name.trimmed().isEmpty()) {
        QMessageBox::warning(this, _t("Favorit speichern"),
                             _t("Name bereits vergeben oder ungültig."));
        return;
    }
    if (m_store.contains(name.trimmed())
        && QMessageBox::question(this, _t("Favorit speichern"),
                                 _t("„%1“ mit den aktuellen Tabs überschreiben?")
                                     .arg(name.trimmed())) != QMessageBox::Yes)
        return;
    m_store.put(name.trimmed(), m_currentTabs);
    m_store.save();
    reload();
}

void TabFavoritesDialog::renameSelected()
{
    auto *item = m_list->currentItem();
    if (!item)
        return;
    const QString oldName = item->data(Qt::UserRole).toString();
    bool ok = false;
    const QString newName = QInputDialog::getText(this, _t("Umbenennen"), _t("Neuer Name:"),
                                                  QLineEdit::Normal, oldName, &ok);
    if (!ok || newName.trimmed().isEmpty() || newName.trimmed() == oldName)
        return;
    if (!m_store.rename(oldName, newName.trimmed())) {
        QMessageBox::warning(this, _t("Fehler"), _t("Name bereits vergeben."));
        return;
    }
    m_store.save();
    reload();
}

void TabFavoritesDialog::removeSelected()
{
    auto *item = m_list->currentItem();
    if (!item)
        return;
    const QString name = item->data(Qt::UserRole).toString();
    if (QMessageBox::question(this, _t("Löschen"),
                              QStringLiteral("Favorit \"%1\" löschen?").arg(name))
        != QMessageBox::Yes)
        return;
    m_store.remove(name);
    m_store.save();
    reload();
}

} // namespace ncssh::gui
