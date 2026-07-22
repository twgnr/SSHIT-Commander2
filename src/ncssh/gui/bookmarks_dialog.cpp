#include "ncssh/gui/bookmarks_dialog.hpp"

#include "ncssh/core/i18n.hpp"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;

BookmarksDialog::BookmarksDialog(core::BookmarkStore *store, const QString &key, QWidget *parent)
    : QDialog(parent), m_store(store), m_key(key)
{
    setWindowTitle(_t("Lesezeichen") + QStringLiteral(" — ") + key);
    resize(640, 440);

    auto *layout = new QVBoxLayout(this);
    auto *info = new QLabel(_t("Doppelklick springt den Pfad in der aktiven Pane an."), this);
    info->setObjectName(QStringLiteral("Muted"));
    layout->addWidget(info);

    m_list = new QListWidget(this);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        m_chosenPath = item->text();
        accept();
    });
    layout->addWidget(m_list, 1);

    auto *buttons = new QHBoxLayout();
    auto *removeBtn = new QPushButton(_t("Entfernen"), this);
    auto *exportBtn = new QPushButton(_t("Exportieren …"), this);
    auto *importBtn = new QPushButton(_t("Importieren …"), this);
    auto *cancel = new QPushButton(_t("Schließen"), this);
    auto *goBtn = new QPushButton(_t("Anspringen"), this);
    goBtn->setDefault(true);
    connect(removeBtn, &QPushButton::clicked, this, &BookmarksDialog::removeSelected);
    connect(exportBtn, &QPushButton::clicked, this, &BookmarksDialog::exportBookmarks);
    connect(importBtn, &QPushButton::clicked, this, &BookmarksDialog::importBookmarks);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(goBtn, &QPushButton::clicked, this, [this] {
        if (auto *item = m_list->currentItem()) {
            m_chosenPath = item->text();
            accept();
        }
    });
    buttons->addWidget(removeBtn);
    buttons->addWidget(exportBtn);
    buttons->addWidget(importBtn);
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(goBtn);
    layout->addLayout(buttons);

    reload();
}

void BookmarksDialog::reload()
{
    m_list->clear();
    m_list->addItems(m_store->list(m_key));
}

void BookmarksDialog::removeSelected()
{
    if (auto *item = m_list->currentItem()) {
        m_store->remove(m_key, item->text());
        m_store->save();
        reload();
    }
}

void BookmarksDialog::exportBookmarks()
{
    const QString path = QFileDialog::getSaveFileName(
        this, _t("Lesezeichen exportieren"), QStringLiteral("bookmarks.json"),
        QStringLiteral("JSON (*.json)"));
    if (path.isEmpty())
        return;
    try {
        m_store->exportTo(path);
        QMessageBox::information(this, _t("Export"), _t("Lesezeichen exportiert."));
    } catch (const std::exception &exc) {
        QMessageBox::warning(this, _t("Fehler"), QString::fromUtf8(exc.what()));
    }
}

void BookmarksDialog::importBookmarks()
{
    const QString path = QFileDialog::getOpenFileName(
        this, _t("Lesezeichen importieren"), QString(), QStringLiteral("JSON (*.json)"));
    if (path.isEmpty())
        return;
    try {
        const int added = m_store->importFrom(path);
        m_store->save();
        reload();
        QMessageBox::information(this, _t("Import"),
                                 QStringLiteral("%1 neue Lesezeichen übernommen.").arg(added));
    } catch (const std::exception &exc) {
        QMessageBox::warning(this, _t("Fehler"), QString::fromUtf8(exc.what()));
    }
}

} // namespace ncssh::gui
