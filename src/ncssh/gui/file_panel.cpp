#include "ncssh/gui/file_panel.hpp"

#include "ncssh/core/dateformat.hpp"
#include "ncssh/core/i18n.hpp"
#include "ncssh/gui/bookmarks_dialog.hpp"
#include "ncssh/gui/editor_dialog.hpp"
#include "ncssh/gui/properties_dialog.hpp"

#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QTableWidget>
#include <QTextEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <algorithm>

namespace ncssh::gui {

using core::_t;
using core::EntryType;
using core::FileEntry;

static QString humanSize(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    double v = bytes;
    const char *units[] = {"KB", "MB", "GB", "TB"};
    int i = -1;
    do { v /= 1024.0; ++i; } while (v >= 1024.0 && i < 3);
    return QStringLiteral("%1 %2").arg(v, 0, 'f', 1).arg(QString::fromLatin1(units[i]));
}

FilePanel::FilePanel(AsyncBridge *bridge, const QString &title, QWidget *parent)
    : QWidget(parent), m_bridge(bridge)
{
    buildUi(title);
}

void FilePanel::buildUi(const QString &title)
{
    setObjectName(QStringLiteral("Pane"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // Kopfzeile mit sudo-Chip (nur remote/Linux aktivierbar)
    auto *headerRow = new QHBoxLayout();
    m_header = new QLabel(title, this);
    m_header->setObjectName(QStringLiteral("PaneHeader"));
    m_sudoChip = new QPushButton(QStringLiteral("sudo"), this);
    m_sudoChip->setObjectName(QStringLiteral("Chip"));
    m_sudoChip->setCheckable(true);
    m_sudoChip->setVisible(false);
    m_sudoChip->setToolTip(_t("Operationen als root über sudo ausführen"));
    connect(m_sudoChip, &QPushButton::toggled, this, [this](bool on) {
        m_sudoActive = on;
        emit sudoToggled(on);
    });
    headerRow->addWidget(m_header, 1);
    headerRow->addWidget(m_sudoChip);
    layout->addLayout(headerRow);

    auto *pathRow = new QHBoxLayout();
    auto *up = new QPushButton(QStringLiteral("↑"), this);
    up->setFixedWidth(34);
    up->setToolTip(_t("Hoch"));
    connect(up, &QPushButton::clicked, this, &FilePanel::goUp);
    m_pathEdit = new QLineEdit(this);
    connect(m_pathEdit, &QLineEdit::returnPressed, this,
            [this] { navigateTo(m_pathEdit->text()); });
    auto *reload = new QPushButton(QStringLiteral("⟳"), this);
    reload->setFixedWidth(34);
    reload->setToolTip(_t("Neu laden"));
    connect(reload, &QPushButton::clicked, this, &FilePanel::refresh);
    // Lesezeichen: aktuellen Pfad merken (☆/★) bzw. Liste oeffnen
    m_starButton = new QPushButton(QStringLiteral("☆"), this);
    m_starButton->setFixedWidth(34);
    m_starButton->setToolTip(_t("Pfad als Lesezeichen merken"));
    connect(m_starButton, &QPushButton::clicked, this, &FilePanel::toggleBookmark);
    auto *bookmarksBtn = new QPushButton(QStringLiteral("▾"), this);
    bookmarksBtn->setFixedWidth(28);
    bookmarksBtn->setToolTip(_t("Lesezeichen"));
    connect(bookmarksBtn, &QPushButton::clicked, this, &FilePanel::openBookmarks);
    pathRow->addWidget(up);
    pathRow->addWidget(m_pathEdit, 1);
    pathRow->addWidget(m_starButton);
    pathRow->addWidget(bookmarksBtn);
    pathRow->addWidget(reload);
    layout->addLayout(pathRow);
    m_bookmarks.load();

    m_table = new QTableWidget(0, 4, this);
    m_table->setHorizontalHeaderLabels(
        {_t("Name"), _t("Größe"), _t("Geändert"), _t("Rechte")});
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->setSortingEnabled(false);   // eigene Sortierung (Ordner zuerst)
    connect(m_table, &QTableWidget::cellDoubleClicked, this, &FilePanel::onDoubleClick);
    connect(m_table, &QTableWidget::customContextMenuRequested, this,
            &FilePanel::openContextMenu);
    // Klickbare Spalten-Sortierung
    m_table->horizontalHeader()->setSectionsClickable(true);
    connect(m_table->horizontalHeader(), &QHeaderView::sectionClicked, this,
            &FilePanel::sortBy);
    m_table->installEventFilter(this);
    m_table->viewport()->installEventFilter(this);
    layout->addWidget(m_table, 1);

    // Wildcard-Filter (Strg+F blendet ihn ein)
    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText(_t("Filter (z. B. *.log) — Esc blendet aus"));
    m_filterEdit->setVisible(false);
    connect(m_filterEdit, &QLineEdit::textChanged, this, &FilePanel::applyFilter);
    m_filterEdit->installEventFilter(this);
    layout->addWidget(m_filterEdit);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("Muted"));
    layout->addWidget(m_status);
}

void FilePanel::setHeaderTitle(const QString &title)
{
    m_header->setText(title);
}

void FilePanel::setProvider(core::FileSystemProvider *provider, const QString &startPath)
{
    m_provider = provider;
    if (!provider)
        return;
    if (!startPath.isEmpty()) {
        navigateTo(startPath);
    } else {
        // Home des Providers ermitteln (blockierend im Worker).
        m_bridge->run<QString>(
            [provider] { return provider->home(); },
            [this](const QString &home) { navigateTo(home); },
            [this](const QString &err) { emit statusMessage(err); });
    }
}

void FilePanel::navigateTo(const QString &path)
{
    if (!m_provider || path.isEmpty())
        return;
    loadDir(path);
}

void FilePanel::refresh()
{
    if (!m_path.isEmpty())
        loadDir(m_path);
}

void FilePanel::goUp()
{
    if (!m_provider || m_path.isEmpty())
        return;
    const QString parent = m_provider->parent(m_path);
    if (!parent.isEmpty() && parent != m_path)
        navigateTo(parent);
}

void FilePanel::loadDir(const QString &path)
{
    core::FileSystemProvider *provider = m_provider;
    m_bridge->run<std::vector<FileEntry>>(
        [provider, path] { return provider->listDir(path); },
        [this, path](const std::vector<FileEntry> &entries) {
            m_path = path;
            m_pathEdit->setText(path);
            m_entries = entries;
            populate(entries);
            updateBookmarkButton();
            emit pathChanged(path);
        },
        [this](const QString &err) {
            emit statusMessage(err);
            QMessageBox::warning(this, _t("Fehler"), err);
        });
}

void FilePanel::populate(const std::vector<FileEntry> &entries)
{
    // Sortieren nach der gewaehlten Spalte — ".." und Ordner bleiben oben.
    std::vector<FileEntry> sorted = entries;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [this](const FileEntry &a, const FileEntry &b) {
        if (a.type == EntryType::Parent) return true;
        if (b.type == EntryType::Parent) return false;
        if (a.isDir() != b.isDir()) return a.isDir();
        bool less;
        switch (m_sortColumn) {
        case 1:  less = a.size < b.size; break;
        case 2:  less = a.modified < b.modified; break;
        case 3:  less = a.permissions < b.permissions; break;
        default: less = a.name.toLower() < b.name.toLower(); break;
        }
        return m_sortAscending ? less : !less;
    });

    // Wildcard-Filter (leer = alles).
    QRegularExpression filterRe;
    const bool filtering = !m_filter.isEmpty();
    if (filtering) {
        filterRe = QRegularExpression(
            QRegularExpression::wildcardToRegularExpression(
                m_filter.contains(QLatin1Char('*')) || m_filter.contains(QLatin1Char('?'))
                    ? m_filter
                    : QStringLiteral("*%1*").arg(m_filter)),
            QRegularExpression::CaseInsensitiveOption);
    }

    m_table->setRowCount(0);
    int row = 0;
    qint64 totalSize = 0;
    int fileCount = 0, dirCount = 0;
    for (const FileEntry &e : sorted) {
        if (!m_showHidden && e.hidden && e.type != EntryType::Parent)
            continue;
        if (filtering && e.type != EntryType::Parent
            && !filterRe.match(e.name).hasMatch())
            continue;
        m_table->insertRow(row);
        QString icon = e.type == EntryType::Parent ? QStringLiteral("↩ ")
                       : e.isDir()                 ? QStringLiteral("📁 ")
                       : e.type == EntryType::Symlink ? QStringLiteral("🔗 ")
                                                      : QStringLiteral("📄 ");
        auto *nameItem = new QTableWidgetItem(icon + e.name);
        nameItem->setData(Qt::UserRole, e.name);
        m_table->setItem(row, 0, nameItem);
        m_table->setItem(row, 1, new QTableWidgetItem(
                                     e.isDir() ? QString() : humanSize(e.size)));
        m_table->setItem(row, 2, new QTableWidgetItem(
                                     e.modified.isValid()
                                         ? core::formatDt(e.modified, QString())
                                         : QString()));
        m_table->setItem(row, 3, new QTableWidgetItem(e.permString()));
        if (e.type != EntryType::Parent) {
            if (e.isDir()) ++dirCount; else { ++fileCount; totalSize += e.size; }
        }
        ++row;
    }
    QString status = QStringLiteral("%1 Ordner, %2 Dateien · %3")
                         .arg(dirCount).arg(fileCount).arg(humanSize(totalSize));
    if (filtering)
        status += QStringLiteral(" · Filter: %1").arg(m_filter);
    m_status->setText(status);
}

void FilePanel::onDoubleClick(int row, int)
{
    if (row < 0 || !m_provider)
        return;
    const QString name = m_table->item(row, 0)->data(Qt::UserRole).toString();
    if (name == QLatin1String("..")) {
        goUp();
        return;
    }
    // Eintrag suchen
    for (const FileEntry &e : m_entries) {
        if (e.name == name) {
            if (e.isDir()) {
                navigateTo(m_provider->join(m_path, name));
            } else {
                opView();
            }
            return;
        }
    }
}

QString FilePanel::selectedPath() const
{
    const auto items = m_table->selectedItems();
    if (items.isEmpty() || !m_provider)
        return {};
    const QString name = m_table->item(items.first()->row(), 0)->data(Qt::UserRole).toString();
    if (name == QLatin1String(".."))
        return {};
    return m_provider->join(m_path, name);
}

std::vector<QString> FilePanel::selectedPaths() const
{
    std::vector<QString> out;
    if (!m_provider)
        return out;
    QSet<int> rows;
    for (auto *item : m_table->selectedItems())
        rows.insert(item->row());
    for (int r : rows) {
        const QString name = m_table->item(r, 0)->data(Qt::UserRole).toString();
        if (name != QLatin1String(".."))
            out.push_back(m_provider->join(m_path, name));
    }
    return out;
}

void FilePanel::openContextMenu(const QPoint &pos)
{
    QMenu menu(this);
    menu.addAction(_t("Ansehen") + QStringLiteral("\tF3"), this, &FilePanel::opView);
    menu.addAction(_t("Bearbeiten") + QStringLiteral("\tF4"), this, &FilePanel::opEdit);
    menu.addSeparator();
    menu.addAction(_t("Kopieren/Übertragen") + QStringLiteral("\tF5"), this,
                   [this] { const QString p = selectedPath(); if (!p.isEmpty()) emit transferRequested(p); });
    menu.addAction(_t("Umbenennen") + QStringLiteral("\tF6"), this, &FilePanel::opRename);
    menu.addAction(_t("Neuer Ordner") + QStringLiteral("\tF7"), this, &FilePanel::opMkdir);
    menu.addAction(_t("Löschen") + QStringLiteral("\tF8"), this, &FilePanel::opDelete);
    menu.addSeparator();
    menu.addAction(_t("Eigenschaften"), this, &FilePanel::opProperties);
    menu.exec(m_table->viewport()->mapToGlobal(pos));
}

// Bekannte Bild-Endungen fuer die Vorschau (F3).
static bool isImageFile(const QString &name)
{
    static const QStringList exts = {
        QStringLiteral(".png"), QStringLiteral(".jpg"), QStringLiteral(".jpeg"),
        QStringLiteral(".gif"), QStringLiteral(".bmp"), QStringLiteral(".webp"),
        QStringLiteral(".svg"), QStringLiteral(".ico"),
    };
    const QString low = name.toLower();
    for (const QString &ext : exts) {
        if (low.endsWith(ext))
            return true;
    }
    return false;
}

void FilePanel::opView()
{
    const QString path = selectedPath();
    if (path.isEmpty() || !m_provider)
        return;
    core::FileSystemProvider *provider = m_provider;

    // Bilder als Vorschau statt als Text anzeigen.
    if (isImageFile(provider->basename(path))) {
        m_bridge->run<QByteArray>(
            [provider, path] { return provider->readBytes(path, 25'000'000); },
            [this, path, provider](const QByteArray &data) {
                QPixmap pixmap;
                if (!pixmap.loadFromData(data)) {
                    QMessageBox::warning(this, _t("Fehler"), _t("Bild nicht lesbar."));
                    return;
                }
                auto *dlg = new QDialog(this);
                dlg->setAttribute(Qt::WA_DeleteOnClose);
                dlg->setWindowTitle(_t("Ansehen") + QStringLiteral(" — ")
                                    + provider->basename(path));
                auto *lay = new QVBoxLayout(dlg);
                auto *scroll = new QScrollArea(dlg);
                scroll->setWidgetResizable(true);
                auto *label = new QLabel(scroll);
                label->setAlignment(Qt::AlignCenter);
                label->setPixmap(pixmap);
                scroll->setWidget(label);
                lay->addWidget(scroll);
                auto *info = new QLabel(
                    QStringLiteral("%1 × %2 · %3")
                        .arg(pixmap.width()).arg(pixmap.height()).arg(humanSize(data.size())),
                    dlg);
                info->setObjectName(QStringLiteral("Muted"));
                lay->addWidget(info);
                dlg->resize(qMin(pixmap.width() + 60, 1100),
                            qMin(pixmap.height() + 90, 800));
                dlg->show();
            },
            [this](const QString &err) { QMessageBox::warning(this, _t("Fehler"), err); });
        return;
    }

    m_bridge->run<QString>(
        [provider, path] { return provider->readText(path); },
        [this, path](const QString &text) {
            auto *dlg = new QDialog(this);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->setWindowTitle(_t("Ansehen") + QStringLiteral(" — ") + m_provider->basename(path));
            dlg->resize(760, 560);
            auto *lay = new QVBoxLayout(dlg);
            auto *edit = new QTextEdit(dlg);
            edit->setReadOnly(true);
            edit->setPlainText(text);
            edit->setLineWrapMode(QTextEdit::NoWrap);
            lay->addWidget(edit);
            dlg->show();
        },
        [this](const QString &err) { QMessageBox::warning(this, _t("Fehler"), err); });
}

void FilePanel::opEdit()
{
    const QString path = selectedPath();
    if (path.isEmpty() || !m_provider)
        return;
    auto *dlg = new EditorDialog(m_bridge, m_provider, path, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &QDialog::finished, this, &FilePanel::refresh);
    dlg->show();
}

void FilePanel::opMkdir()
{
    if (!m_provider)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, _t("Neuer Ordner"),
                                               _t("Name des Ordners:"), QLineEdit::Normal,
                                               QString(), &ok);
    if (!ok || name.isEmpty())
        return;
    core::FileSystemProvider *provider = m_provider;
    const QString target = provider->join(m_path, name);
    m_bridge->run(
        [provider, target] { provider->mkdir(target); },
        [this] { refresh(); },
        [this](const QString &err) { QMessageBox::warning(this, _t("Fehler"), err); });
}

void FilePanel::opRename()
{
    const QString path = selectedPath();
    if (path.isEmpty() || !m_provider)
        return;
    core::FileSystemProvider *provider = m_provider;
    bool ok = false;
    const QString oldName = provider->basename(path);
    const QString name = QInputDialog::getText(this, _t("Umbenennen"),
                                               _t("Neuer Name:"), QLineEdit::Normal, oldName, &ok);
    if (!ok || name.isEmpty() || name == oldName)
        return;
    const QString target = provider->join(m_path, name);
    m_bridge->run(
        [provider, path, target] { provider->rename(path, target); },
        [this] { refresh(); },
        [this](const QString &err) { QMessageBox::warning(this, _t("Fehler"), err); });
}

void FilePanel::opDelete()
{
    const auto paths = selectedPaths();
    if (paths.empty() || !m_provider)
        return;
    const auto reply = QMessageBox::question(
        this, _t("Löschen"),
        QStringLiteral("%1 Element(e) löschen?").arg(paths.size()),
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes)
        return;
    core::FileSystemProvider *provider = m_provider;
    m_bridge->run(
        [provider, paths] {
            for (const QString &p : paths)
                provider->remove(p, true);
        },
        [this] { refresh(); },
        [this](const QString &err) { QMessageBox::warning(this, _t("Fehler"), err); });
}

void FilePanel::opProperties()
{
    const QString path = selectedPath();
    if (path.isEmpty() || !m_provider)
        return;
    const QString name = m_provider->basename(path);
    for (const FileEntry &e : m_entries) {
        if (e.name == name) {
            auto *dlg = new PropertiesDialog(m_bridge, m_provider, path, e, this);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            connect(dlg, &QDialog::accepted, this, &FilePanel::refresh);
            dlg->show();
            return;
        }
    }
}

void FilePanel::setSudoAvailable(bool available)
{
    m_sudoAvailable = available;
    m_sudoChip->setVisible(available);
    if (!available && m_sudoChip->isChecked())
        m_sudoChip->setChecked(false);
}

void FilePanel::sortBy(int column)
{
    if (column == m_sortColumn)
        m_sortAscending = !m_sortAscending;
    else {
        m_sortColumn = column;
        m_sortAscending = true;
    }
    populate(m_entries);
}

void FilePanel::applyFilter(const QString &pattern)
{
    m_filter = pattern.trimmed();
    populate(m_entries);
}

void FilePanel::setBookmarkKey(const QString &key)
{
    m_bookmarkKey = key;
    updateBookmarkButton();
}

void FilePanel::updateBookmarkButton()
{
    if (!m_starButton)
        return;
    const bool marked = !m_path.isEmpty() && m_bookmarks.contains(m_bookmarkKey, m_path);
    m_starButton->setText(marked ? QStringLiteral("★") : QStringLiteral("☆"));
}

void FilePanel::toggleBookmark()
{
    if (m_path.isEmpty())
        return;
    const bool nowMarked = m_bookmarks.toggle(m_bookmarkKey, m_path);
    m_bookmarks.save();
    updateBookmarkButton();
    emit statusMessage(nowMarked ? _t("Lesezeichen gesetzt.") : _t("Lesezeichen entfernt."));
}

void FilePanel::openBookmarks()
{
    BookmarksDialog dlg(&m_bookmarks, m_bookmarkKey, this);
    if (dlg.exec() == QDialog::Accepted && !dlg.chosenPath().isEmpty())
        navigateTo(dlg.chosenPath());
    updateBookmarkButton();
}

void FilePanel::toggleHidden()
{
    m_showHidden = !m_showHidden;
    populate(m_entries);
}

bool FilePanel::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::FocusIn || event->type() == QEvent::MouseButtonPress) {
        emit activated();
    }
    if (event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        switch (ke->key()) {
        case Qt::Key_F3: opView(); return true;
        case Qt::Key_F4: opEdit(); return true;
        case Qt::Key_F5: { const QString p = selectedPath(); if (!p.isEmpty()) emit transferRequested(p); return true; }
        case Qt::Key_F6: opRename(); return true;
        case Qt::Key_F7: opMkdir(); return true;
        case Qt::Key_F8: opDelete(); return true;
        case Qt::Key_Backspace: goUp(); return true;
        case Qt::Key_F:
            if (ke->modifiers() & Qt::ControlModifier) {  // Pane-Filter einblenden
                m_filterEdit->setVisible(true);
                m_filterEdit->setFocus();
                m_filterEdit->selectAll();
                return true;
            }
            break;
        case Qt::Key_Escape:
            if (obj == m_filterEdit) {                    // Filter ausblenden
                m_filterEdit->clear();
                m_filterEdit->setVisible(false);
                m_table->setFocus();
                return true;
            }
            break;
        case Qt::Key_Down:
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (obj == m_filterEdit) {  // aus dem Filter zurueck in die Liste
                m_table->setFocus();
                if (m_table->rowCount() > 0 && m_table->currentRow() < 0)
                    m_table->selectRow(0);
                return true;
            }
            break;
        default: break;
        }
    }
    return QWidget::eventFilter(obj, event);
}

} // namespace ncssh::gui
