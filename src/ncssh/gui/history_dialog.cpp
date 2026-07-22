#include "ncssh/gui/history_dialog.hpp"

#include "ncssh/core/i18n.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;

HistoryDialog::HistoryDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(_t("Verlauf & Favoriten"));
    resize(680, 460);
    m_store.load();

    auto *layout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);

    m_history = new QListWidget(tabs);
    m_favorites = new QListWidget(tabs);
    for (QListWidget *list : {m_history, m_favorites}) {
        connect(list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
            m_command = item->text();
            accept();
        });
    }
    tabs->addTab(m_history, _t("Verlauf"));
    tabs->addTab(m_favorites, _t("★ Favoriten"));
    layout->addWidget(tabs, 1);

    auto *buttons = new QHBoxLayout();
    auto *favBtn = new QPushButton(_t("★ Als Favorit"), this);
    auto *unfavBtn = new QPushButton(_t("Favorit entfernen"), this);
    auto *clearBtn = new QPushButton(_t("Verlauf leeren"), this);
    auto *cancel = new QPushButton(_t("Abbrechen"), this);
    auto *insertBtn = new QPushButton(_t("Einfügen"), this);
    insertBtn->setDefault(true);

    connect(favBtn, &QPushButton::clicked, this, [this] {
        if (auto *item = m_history->currentItem()) {
            m_store.addFavorite(item->text());
            m_store.save();
            reload();
        }
    });
    connect(unfavBtn, &QPushButton::clicked, this, [this] {
        if (auto *item = m_favorites->currentItem()) {
            m_store.removeFavorite(item->text());
            m_store.save();
            reload();
        }
    });
    connect(clearBtn, &QPushButton::clicked, this, [this] {
        m_store.clearHistory();
        m_store.save();
        reload();
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(insertBtn, &QPushButton::clicked, this, [this] {
        if (auto *item = activeList()->currentItem()) {
            m_command = item->text();
            accept();
        }
    });

    buttons->addWidget(favBtn);
    buttons->addWidget(unfavBtn);
    buttons->addWidget(clearBtn);
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(insertBtn);
    layout->addLayout(buttons);

    reload();
}

QListWidget *HistoryDialog::activeList() const
{
    return m_favorites->hasFocus() || m_favorites->currentItem() ? m_favorites : m_history;
}

void HistoryDialog::reload()
{
    m_history->clear();
    // Neueste zuerst
    const QStringList hist = m_store.history();
    for (auto it = hist.rbegin(); it != hist.rend(); ++it)
        m_history->addItem(*it);
    m_favorites->clear();
    m_favorites->addItems(m_store.favorites());
}

} // namespace ncssh::gui
