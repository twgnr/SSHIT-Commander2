#include "ncssh/gui/dir_chooser.hpp"

#include "ncssh/core/i18n.hpp"
#include "ncssh/core/models.hpp"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;
using core::EntryType;
using core::FileEntry;

DirChooserDialog::DirChooserDialog(AsyncBridge *bridge, core::FileSystemProvider *provider,
                                   const QString &startPath, const QStringList &bookmarks,
                                   QWidget *parent)
    : QDialog(parent), m_bridge(bridge), m_provider(provider), m_path(startPath)
{
    setWindowTitle(_t("Zielordner wählen"));
    resize(820, 520);

    auto *layout = new QVBoxLayout(this);
    auto *top = new QHBoxLayout();
    m_pathLabel = new QLabel(m_path, this);
    top->addWidget(m_pathLabel, 1);
    if (!bookmarks.isEmpty()) {
        // Schnellsprung zu einem Lesezeichen.
        auto *bookmarkBtn = new QPushButton(QStringLiteral("★ ▾"), this);
        auto *menu = new QMenu(bookmarkBtn);
        for (const QString &path : bookmarks)
            menu->addAction(path, this, [this, path] { gotoPath(path); });
        bookmarkBtn->setMenu(menu);
        top->addWidget(bookmarkBtn);
    }
    layout->addLayout(top);

    m_list = new QListWidget(this);
    connect(m_list, &QListWidget::itemActivated, this, &DirChooserDialog::enter);
    layout->addWidget(m_list, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    buttons->button(QDialogButtonBox::Ok)->setText(_t("Diesen Ordner wählen"));
    buttons->button(QDialogButtonBox::Cancel)->setText(_t("Abbrechen"));
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        m_chosen = m_path;
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    load();
}

void DirChooserDialog::load()
{
    m_pathLabel->setText(m_path);
    m_list->clear();
    core::FileSystemProvider *provider = m_provider;
    const QString path = m_path;
    m_bridge->run<std::vector<FileEntry>>(
        [provider, path] { return provider->listDir(path); },
        [this](const std::vector<FileEntry> &entries) {
            auto *up = new QListWidgetItem(QStringLiteral("📁 .."), m_list);
            up->setData(Qt::UserRole, QStringLiteral(".."));
            for (const FileEntry &e : entries) {
                if (e.type == EntryType::Parent || !e.isDir())
                    continue;
                auto *item = new QListWidgetItem(QStringLiteral("📁 %1").arg(e.name), m_list);
                item->setData(Qt::UserRole, e.name);
            }
        },
        [this](const QString &err) {
            m_pathLabel->setText(_t("Fehler: %1").arg(err));
        });
}

void DirChooserDialog::enter(QListWidgetItem *item)
{
    const QString name = item->data(Qt::UserRole).toString();
    m_path = (name == QLatin1String(".."))
                 ? m_provider->parent(m_path)
                 : m_provider->join(m_path, name);
    load();
}

void DirChooserDialog::gotoPath(const QString &path)
{
    m_path = path;
    load();
}

} // namespace ncssh::gui
