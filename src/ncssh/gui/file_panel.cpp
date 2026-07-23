#include "ncssh/gui/file_panel.hpp"

#include "ncssh/core/dateformat.hpp"
#include "ncssh/core/execfile.hpp"
#include "ncssh/core/fileops.hpp"
#include "ncssh/core/i18n.hpp"
#include "ncssh/core/natsort.hpp"
#include "ncssh/core/netscan.hpp"
#include "ncssh/core/openwith.hpp"
#include "ncssh/core/profiles.hpp"
#include "ncssh/core/shortcuts.hpp"
#include "ncssh/core/settings.hpp"
#include "ncssh/gui/file_icons.hpp"
#include "ncssh/gui/bookmarks_dialog.hpp"
#include "ncssh/gui/confirm_dialog.hpp"
#include "ncssh/gui/dir_chooser.hpp"
#include "ncssh/gui/editor_dialog.hpp"
#include "ncssh/gui/properties_dialog.hpp"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDrag>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHash>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QIcon>
#include <QImage>
#include <QLayoutItem>
#include <QListView>
#include <QProcess>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStorageInfo>
#include <QSize>
#include <QStackedWidget>
#include <QToolButton>
#include <QVariantMap>
#include <QSet>
#include <QTimer>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMimeData>
#include <QMouseEvent>
#include <QUrl>
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

// Bekannte Bild-Endungen (Vorschau F3 und Miniaturansichten).
static bool isImageFile(const QString &name);

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
    m_sudoChip->setToolTip(_t("Diese Pane mit sudo-Rechten (root) anzeigen"));
    connect(m_sudoChip, &QPushButton::toggled, this, [this](bool on) {
        m_sudoActive = on;
        emit sudoToggled(on);
    });
    headerRow->addWidget(m_header, 1);
    headerRow->addWidget(m_sudoChip);
    layout->addLayout(headerRow);

    // Tippsuche: Puffer nach kurzer Tipppause verwerfen.
    m_typeAheadTimer = new QTimer(this);
    m_typeAheadTimer->setSingleShot(true);
    m_typeAheadTimer->setInterval(900);
    connect(m_typeAheadTimer, &QTimer::timeout, this, [this] { m_typeAheadBuffer.clear(); });

    auto *pathRow = new QHBoxLayout();
    // Laufwerk/Mountpunkt wechseln — ohne das muss man den Pfad tippen.
    m_driveCombo = new QComboBox(this);
    m_driveCombo->setToolTip(_t("Laufwerk / Mountpunkt wechseln"));
    m_driveCombo->setFixedWidth(72);
    connect(m_driveCombo, &QComboBox::activated, this, [this](int index) {
        const QString root = m_driveCombo->itemData(index).toString();
        if (!root.isEmpty() && root != m_path)
            navigateTo(root);
    });
    auto *back = new QPushButton(QStringLiteral("←"), this);
    back->setFixedWidth(30);
    back->setToolTip(_t("Zurück (Alt+←)"));
    connect(back, &QPushButton::clicked, this, &FilePanel::goBack);
    auto *forward = new QPushButton(QStringLiteral("→"), this);
    forward->setFixedWidth(30);
    forward->setToolTip(_t("Vor (Alt+→)"));
    connect(forward, &QPushButton::clicked, this, &FilePanel::goForward);
    auto *up = new QPushButton(QStringLiteral("↑"), this);
    up->setFixedWidth(34);
    up->setToolTip(_t("Übergeordneter Ordner"));
    connect(up, &QPushButton::clicked, this, &FilePanel::goUp);
    // Pfad wahlweise als klickbare Breadcrumb-Leiste oder als Eingabefeld.
    m_crumbScroll = new QScrollArea(this);
    m_crumbScroll->setWidgetResizable(true);
    m_crumbScroll->setFrameShape(QFrame::NoFrame);
    m_crumbScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_crumbScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_crumbScroll->setFixedHeight(30);
    m_crumbScroll->setToolTip(_t("Rechts in die leere Fläche klicken, um den Pfad einzugeben"));
    auto *crumbHost = new QWidget(m_crumbScroll);
    m_crumbLayout = new QHBoxLayout(crumbHost);
    m_crumbLayout->setContentsMargins(2, 0, 2, 0);
    m_crumbLayout->setSpacing(1);
    m_crumbScroll->setWidget(crumbHost);
    crumbHost->installEventFilter(this);   // Klick auf die leere Flaeche -> editieren

    m_pathEdit = new QLineEdit(this);
    m_pathEdit->setVisible(false);
    m_pathEdit->installEventFilter(this);
    connect(m_pathEdit, &QLineEdit::returnPressed, this, [this] {
        const QString target = m_pathEdit->text();
        endPathEdit();
        navigateTo(target);
    });
    auto *reload = new QPushButton(QStringLiteral("⟳"), this);
    reload->setFixedWidth(34);
    reload->setToolTip(_t("Neu laden"));
    connect(reload, &QPushButton::clicked, this, &FilePanel::refresh);
    // Lesezeichen: aktuellen Pfad merken (☆/★) bzw. Liste oeffnen
    m_starButton = new QPushButton(QStringLiteral("☆"), this);
    m_starButton->setFixedWidth(34);
    m_starButton->setToolTip(_t("Aktuellen Pfad als Lesezeichen (pro Server)"));
    connect(m_starButton, &QPushButton::clicked, this, &FilePanel::toggleBookmark);
    auto *bookmarksBtn = new QPushButton(QStringLiteral("▾"), this);
    bookmarksBtn->setFixedWidth(28);
    bookmarksBtn->setToolTip(_t("Lesezeichen dieses Servers"));
    // Aufklappmenue mit den gemerkten Pfaden — schneller als der Dialog.
    connect(bookmarksBtn, &QPushButton::clicked, this, [this, bookmarksBtn] {
        QMenu menu(this);
        const QStringList paths = m_bookmarks.list(m_bookmarkKey);
        if (paths.isEmpty()) {
            menu.addAction(_t("(keine Lesezeichen)"))->setEnabled(false);
        } else {
            for (const QString &path : paths)
                menu.addAction(path, this, [this, path] { navigateTo(path); });
        }
        menu.addSeparator();
        menu.addAction(_t("★ Aktuellen Pfad merken"), this, &FilePanel::toggleBookmark);
        menu.addAction(_t("Verwalten…"), this, &FilePanel::openBookmarks);
        menu.exec(bookmarksBtn->mapToGlobal(QPoint(0, bookmarksBtn->height())));
    });
    pathRow->addWidget(m_driveCombo);
    pathRow->addWidget(back);
    pathRow->addWidget(forward);
    pathRow->addWidget(up);
    pathRow->addWidget(m_crumbScroll, 1);
    pathRow->addWidget(m_pathEdit, 1);
    pathRow->addWidget(m_starButton);
    pathRow->addWidget(bookmarksBtn);
    pathRow->addWidget(reload);
    layout->addLayout(pathRow);
    m_bookmarks.load();
    applyShortcuts();   // konfigurierte Datei-Op-Kuerzel laden

    m_table = new QTableWidget(0, 1, this);
    setTableHeaders();
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->verticalHeader()->setVisible(false);
    // Rechtsklick auf die Kopfzeile: Spaltenauswahl
    m_table->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table->horizontalHeader(), &QHeaderView::customContextMenuRequested,
            this, &FilePanel::showHeaderMenu);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->setSortingEnabled(false);   // eigene Sortierung (Ordner zuerst)
    connect(m_table, &QTableWidget::cellDoubleClicked, this, &FilePanel::onDoubleClick);
    connect(m_table, &QTableWidget::customContextMenuRequested, this,
            &FilePanel::openContextMenu);
    // Auswahl an die Vorschau melden und die Statuszeile nachziehen.
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] {
        emit selectionChanged(selectedPath());
        updateSelectionStatus();
    });
    // Klickbare Spalten-Sortierung
    m_table->horizontalHeader()->setSectionsClickable(true);
    connect(m_table->horizontalHeader(), &QHeaderView::sectionClicked, this,
            &FilePanel::sortBy);
    m_table->installEventFilter(this);
    m_table->viewport()->installEventFilter(this);
    // Drag & Drop: Ziehen aus der Pane heraus, Fallenlassen hinein.
    m_table->setDragEnabled(true);
    m_table->setDragDropMode(QAbstractItemView::DragOnly);
    setAcceptDrops(true);

    // Kachelansicht: eigene QListView, die Model UND Auswahl der Tabelle teilt —
    // damit bleibt die gesamte Markier-Logik unveraendert gueltig.
    m_gridMode = core::getSettingBool(QStringLiteral("pane_grid"), false);
    m_grid = new QListView(this);
    m_grid->setModel(m_table->model());
    m_grid->setSelectionModel(m_table->selectionModel());
    m_grid->setModelColumn(0);
    m_grid->setViewMode(QListView::IconMode);
    m_grid->setResizeMode(QListView::Adjust);
    m_grid->setMovement(QListView::Static);
    m_grid->setWrapping(true);
    m_grid->setUniformItemSizes(true);
    m_grid->setWordWrap(true);
    m_grid->setIconSize(QSize(64, 64));
    m_grid->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_grid->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_grid->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_grid, &QListView::customContextMenuRequested, this,
            &FilePanel::openContextMenu);
    connect(m_grid, &QListView::activated, this,
            [this](const QModelIndex &idx) { onDoubleClick(idx.row(), 0); });
    m_grid->installEventFilter(this);

    m_viewStack = new QStackedWidget(this);
    m_viewStack->addWidget(m_table);   // 0 = Detail
    m_viewStack->addWidget(m_grid);    // 1 = Kachel
    m_viewStack->setCurrentIndex(m_gridMode ? 1 : 0);
    layout->addWidget(m_viewStack, 1);

    // Miniaturansichten nur fuer sichtbare Zeilen laden, beim Scrollen nach.
    m_thumbTimer = new QTimer(this);
    m_thumbTimer->setSingleShot(true);
    m_thumbTimer->setInterval(80);
    connect(m_thumbTimer, &QTimer::timeout, this, &FilePanel::loadVisibleThumbs);
    connect(m_table->verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this] { m_thumbTimer->start(); });
    connect(m_grid->verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this] { m_thumbTimer->start(); });

    // Wildcard-Filter (Strg+F blendet ihn ein)
    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setObjectName(QStringLiteral("PaneFilter"));
    m_filterEdit->setPlaceholderText(
        _t("Anzeige filtern (Ctrl+F) — Wildcards * ? möglich, z.B. *.py"));
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

// --- Spalten ---------------------------------------------------------------

QStringList FilePanel::optionalColumns()
{
    return {QStringLiteral("size"),  QStringLiteral("modified"), QStringLiteral("created"),
            QStringLiteral("accessed"), QStringLiteral("type"),  QStringLiteral("ext"),
            QStringLiteral("perm"),  QStringLiteral("owner")};
}

QString FilePanel::columnLabel(const QString &id)
{
    static const QHash<QString, QString> labels = {
        {QStringLiteral("size"), _t("Größe")},       {QStringLiteral("modified"), _t("Geändert")},
        {QStringLiteral("created"), _t("Erstellt")}, {QStringLiteral("accessed"), _t("Zugriff")},
        {QStringLiteral("type"), _t("Typ")},         {QStringLiteral("ext"), _t("Endung")},
        {QStringLiteral("perm"), _t("Rechte")},      {QStringLiteral("owner"), _t("Eigner")},
    };
    return labels.value(id, id);
}

QStringList FilePanel::visibleColumns() const
{
    const QVariant stored = core::getSetting(QStringLiteral("pane_columns"));
    QStringList chosen = stored.toStringList();
    if (chosen.isEmpty()) {
        chosen = {QStringLiteral("size"), QStringLiteral("modified"),
                  QStringLiteral("perm"), QStringLiteral("owner")};
    }
    // Nur gueltige Spalten, in kanonischer Reihenfolge.
    QStringList out;
    for (const QString &id : optionalColumns()) {
        if (chosen.contains(id))
            out << id;
    }
    return out;
}

void FilePanel::setTableHeaders()
{
    // Im Netzwerk-Modus zeigen die Spalten Host-Eigenschaften statt Dateidaten.
    if (hostMode()) {
        m_fileCols = {QStringLiteral("name"),   QStringLiteral("ip"),
                      QStringLiteral("mac"),    QStringLiteral("vendor"),
                      QStringLiteral("os"),     QStringLiteral("latency"),
                      QStringLiteral("services"), QStringLiteral("shares"),
                      QStringLiteral("web")};
        m_table->setColumnCount(m_fileCols.size());
        m_table->setHorizontalHeaderLabels({_t("Name"), _t("IP"), _t("MAC"), _t("Hersteller"),
                                            _t("Betriebssystem"), _t("Latenz"), _t("Dienste"),
                                            _t("Freigaben"), _t("Web")});
        QHeaderView *header = m_table->horizontalHeader();
        header->setSectionResizeMode(0, QHeaderView::Stretch);
        for (int c = 1; c < m_fileCols.size(); ++c)
            header->setSectionResizeMode(c, QHeaderView::ResizeToContents);
        return;
    }

    m_fileCols = QStringList{QStringLiteral("name")} + visibleColumns();
    m_table->setColumnCount(m_fileCols.size());
    QStringList labels{_t("Name")};
    for (int i = 1; i < m_fileCols.size(); ++i)
        labels << columnLabel(m_fileCols.at(i));
    m_table->setHorizontalHeaderLabels(labels);
    // Breiten frei einstellbar; gesetzt wird erst am Ende von populate().
    QHeaderView *hh = m_table->horizontalHeader();
    for (int c = 0; c < m_fileCols.size(); ++c)
        hh->setSectionResizeMode(c, QHeaderView::Interactive);
}

void FilePanel::applyColumnWidths()
{
    static const QHash<QString, int> defaults = {
        {QStringLiteral("name"), 280},     {QStringLiteral("size"), 80},
        {QStringLiteral("modified"), 135}, {QStringLiteral("created"), 135},
        {QStringLiteral("accessed"), 135}, {QStringLiteral("type"), 70},
        {QStringLiteral("ext"), 60},       {QStringLiteral("perm"), 95},
        {QStringLiteral("owner"), 120},
    };
    const QVariantMap widths =
        core::getSetting(QStringLiteral("pane_col_widths")).toMap();
    for (int c = 0; c < m_fileCols.size(); ++c) {
        const QString id = m_fileCols.at(c);
        m_table->setColumnWidth(c, widths.value(id, defaults.value(id, 100)).toInt());
    }
}

void FilePanel::showHeaderMenu(const QPoint &pos)
{
    const QStringList visible = visibleColumns();
    QMenu menu(this);
    QAction *title = menu.addAction(_t("Spalten anzeigen"));
    title->setEnabled(false);
    menu.addSeparator();
    for (const QString &id : optionalColumns()) {
        QAction *action = menu.addAction(columnLabel(id));
        action->setCheckable(true);
        action->setChecked(visible.contains(id));
        connect(action, &QAction::triggered, this,
                [this, id](bool on) { toggleColumn(id, on); });
    }
    menu.exec(m_table->horizontalHeader()->mapToGlobal(pos));
}

void FilePanel::toggleColumn(const QString &id, bool on)
{
    QStringList cols = visibleColumns();
    if (on && !cols.contains(id))
        cols << id;
    else if (!on)
        cols.removeAll(id);
    QStringList canonical;                       // kanonische Reihenfolge halten
    for (const QString &c : optionalColumns()) {
        if (cols.contains(c))
            canonical << c;
    }
    core::setSetting(QStringLiteral("pane_columns"),
                     QJsonArray::fromStringList(canonical));
    setTableHeaders();
    populate(m_entries);
}

QString FilePanel::columnValue(const QString &id, const FileEntry &e) const
{
    // Host-Spalten im Netzwerk-Modus kommen aus den Scanner-Metadaten.
    if (hostMode()) {
        if (id == QLatin1String("latency")) {
            const double ms = e.extra.value(QStringLiteral("latency")).toDouble();
            return ms > 0 ? QStringLiteral("%1 ms").arg(ms, 0, 'f', 1) : QString();
        }
        if (id == QLatin1String("services")) {
            QStringList ports;
            for (const QVariant &p : e.extra.value(QStringLiteral("ports")).toList())
                ports << QStringLiteral("%1 (%2)").arg(p.toInt()).arg(core::serviceName(p.toInt()));
            return ports.join(QStringLiteral(", "));
        }
        if (id == QLatin1String("shares"))
            return e.extra.value(QStringLiteral("shares")).toBool() ? _t("ja") : QString();
        if (id == QLatin1String("web")) {
            const QVariantList web = e.extra.value(QStringLiteral("web")).toList();
            return web.isEmpty() ? QString() : web.first().toString();
        }
        if (id == QLatin1String("os"))
            return e.extra.value(QStringLiteral("os")).toString();
        return e.extra.value(id).toString();
    }

    if (id == QLatin1String("size"))
        return e.isDir() ? QStringLiteral("<DIR>") : humanSize(e.size);
    if (id == QLatin1String("modified"))
        return e.modified.isValid() ? core::formatDt(e.modified, QString()) : QString();
    if (id == QLatin1String("created"))
        return e.created.isValid() ? core::formatDt(e.created, QString()) : QString();
    if (id == QLatin1String("accessed"))
        return e.accessed.isValid() ? core::formatDt(e.accessed, QString()) : QString();
    if (id == QLatin1String("type")) {
        if (e.type == EntryType::Symlink)
            return _t("Symlink");
        return e.isDir() ? _t("Ordner") : _t("Datei");
    }
    if (id == QLatin1String("ext")) {
        if (e.isDir())
            return {};
        const int dot = e.name.lastIndexOf(QLatin1Char('.'));
        return dot > 0 ? e.name.mid(dot + 1) : QString();
    }
    if (id == QLatin1String("perm"))
        return e.permString();
    if (id == QLatin1String("owner")) {
        if (e.owner.isEmpty() && e.group.isEmpty())
            return {};
        return e.owner + QLatin1Char(':') + e.group;
    }
    return {};
}

// --- Breadcrumb-Pfadleiste --------------------------------------------------

std::vector<std::pair<QString, QString>> FilePanel::breadcrumbParts() const
{
    std::vector<std::pair<QString, QString>> parts;
    if (!m_provider)
        return parts;
    QString p = m_path;
    QSet<QString> seen;
    while (!p.isEmpty() && !seen.contains(p)) {
        seen.insert(p);
        const QString label = m_provider->basename(p);
        parts.emplace_back(label.isEmpty() ? p : label, p);
        const QString parent = m_provider->parent(p);
        if (parent.isEmpty() || parent == p)
            break;
        p = parent;
    }
    std::reverse(parts.begin(), parts.end());
    return parts;
}

void FilePanel::buildBreadcrumb()
{
    while (QLayoutItem *item = m_crumbLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    const auto parts = breadcrumbParts();
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            auto *sep = new QLabel(QStringLiteral("›"));
            sep->setObjectName(QStringLiteral("Muted"));
            // Klicks gehen an die Flaeche darunter -> Pfad editieren
            sep->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            m_crumbLayout->addWidget(sep);
        }
        auto *btn = new QToolButton();
        btn->setText(parts[i].first);
        btn->setAutoRaise(true);
        btn->setCursor(Qt::PointingHandCursor);
        const QString full = parts[i].second;
        connect(btn, &QToolButton::clicked, this, [this, full] { navigateTo(full); });
        m_crumbLayout->addWidget(btn);
    }
    m_crumbLayout->addStretch();
}

// --- Ansicht: Detail / Kachel ----------------------------------------------

QAbstractItemView *FilePanel::activeView() const
{
    return m_gridMode ? static_cast<QAbstractItemView *>(m_grid)
                      : static_cast<QAbstractItemView *>(m_table);
}

void FilePanel::setViewMode(bool grid)
{
    m_gridMode = grid;
    m_viewStack->setCurrentIndex(grid ? 1 : 0);
    core::setSetting(QStringLiteral("pane_grid"), grid);
    const int row = m_table->currentRow();
    if (row >= 0)
        activeView()->scrollTo(m_table->model()->index(row, 0));
    loadVisibleThumbs();   // Miniaturen der neuen Ansicht nachladen
}

// --- Miniaturansichten ------------------------------------------------------

// Geteilter Zwischenspeicher: "pfad\nmtime" -> Icon.
static QHash<QString, QIcon> &thumbCache()
{
    static QHash<QString, QIcon> cache;
    return cache;
}

bool FilePanel::thumbsEnabled() const
{
    if (!m_provider || m_provider->isRemote)
        return false;   // nur lokal — remote waere jede Miniatur ein Download
    return core::getSettingBool(QStringLiteral("pane_thumbnails"), false)
           && core::getSettingBool(QStringLiteral("show_file_icons"), true);
}

std::pair<int, int> FilePanel::visibleRows(int buffer) const
{
    QAbstractItemView *view = activeView();
    QAbstractItemModel *model = m_table->model();
    const int n = model->rowCount();
    if (n == 0)
        return {0, 0};
    const QRect vp = view->viewport()->rect();
    const QModelIndex start = view->indexAt(vp.topLeft());
    const int first = start.isValid() ? start.row() : 0;
    int last = first;
    for (int r = first; r < n; ++r) {
        if (view->visualRect(model->index(r, 0)).top() > vp.bottom())
            break;
        last = r;
    }
    return {std::max(0, first - buffer), std::min(n, last + 1 + buffer)};
}

void FilePanel::loadVisibleThumbs()
{
    if (!thumbsEnabled())
        return;
    const int size = std::max(16, activeView()->iconSize().width());
    const quint64 token = m_thumbToken;
    const auto [from, to] = visibleRows();
    for (int row = from; row < to; ++row) {
        if (row < 0 || row >= int(m_rows.size()))
            continue;
        const FileEntry &e = m_rows[size_t(row)];
        if (e.isDir() || !isImageFile(e.name))
            continue;
        const QString path = m_provider->join(m_path, e.name);
        const qint64 mtime = e.modified.isValid() ? e.modified.toSecsSinceEpoch() : 0;
        const QString key = path + QLatin1Char('\n') + QString::number(mtime);
        if (auto it = thumbCache().constFind(key); it != thumbCache().constEnd()) {
            if (QTableWidgetItem *item = m_table->item(row, 0))
                item->setIcon(*it);
            continue;
        }
        if (m_thumbRequested.contains(key))
            continue;
        m_thumbRequested.insert(key);
        const QString name = e.name;
        m_bridge->run<QImage>(
            [path, size]() -> QImage {
                // QImage ist thread-fest; QPixmap/QIcon erst im GUI-Thread.
                QImage img(path);
                if (img.isNull())
                    return {};
                return img.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            },
            [this, row, name, key, token](const QImage &img) {
                if (token != m_thumbToken || img.isNull())
                    return;   // Verzeichnis hat inzwischen gewechselt
                const QIcon icon(QPixmap::fromImage(img));
                if (thumbCache().size() > 800)
                    thumbCache().clear();
                thumbCache().insert(key, icon);
                if (row < int(m_rows.size()) && m_rows[size_t(row)].name == name) {
                    if (QTableWidgetItem *item = m_table->item(row, 0))
                        item->setIcon(icon);
                }
            },
            [](const QString &) {});
    }
}

// Fuellt die Laufwerksauswahl. Nur lokal sinnvoll — remote gibt es keine
// Laufwerksbuchstaben, dort bleibt sie ausgeblendet.
void FilePanel::updateDriveCombo()
{
    if (!m_driveCombo)
        return;
    if (!m_provider || m_provider->isRemote || hostMode()) {
        m_driveCombo->setVisible(false);
        return;
    }
    m_driveCombo->setVisible(true);
    QSignalBlocker blocker(m_driveCombo);
    m_driveCombo->clear();
    for (const QStorageInfo &volume : QStorageInfo::mountedVolumes()) {
        if (!volume.isValid() || !volume.isReady())
            continue;
        const QString root = QDir::toNativeSeparators(volume.rootPath());
        QString label = root;
#ifdef Q_OS_WIN
        label = root.left(2);   // "C:"
#endif
        m_driveCombo->addItem(label, volume.rootPath());
        m_driveCombo->setItemData(m_driveCombo->count() - 1,
                                  QStringLiteral("%1  %2 / %3")
                                      .arg(root, humanSize(volume.bytesAvailable()),
                                           humanSize(volume.bytesTotal())),
                                  Qt::ToolTipRole);
    }
    // Aktuelles Laufwerk auswaehlen.
    const QStorageInfo current(m_path);
    const int index = m_driveCombo->findData(current.rootPath());
    if (index >= 0)
        m_driveCombo->setCurrentIndex(index);
}

void FilePanel::beginPathEdit()
{
    m_pathEdit->setText(m_path);
    m_crumbScroll->setVisible(false);
    m_pathEdit->setVisible(true);
    m_pathEdit->setFocus();
    m_pathEdit->selectAll();
}

void FilePanel::endPathEdit()
{
    if (!m_pathEdit->isVisible())
        return;
    m_pathEdit->setVisible(false);
    m_crumbScroll->setVisible(true);
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
        loadDir(m_path, /*record=*/false);
}

void FilePanel::goBack()
{
    if (!canGoBack())
        return;
    --m_histPos;
    loadDir(m_history.at(int(m_histPos)), /*record=*/false);
}

void FilePanel::goForward()
{
    if (!canGoForward())
        return;
    ++m_histPos;
    loadDir(m_history.at(int(m_histPos)), /*record=*/false);
}

void FilePanel::goUp()
{
    if (!m_provider || m_path.isEmpty())
        return;
    const QString parent = m_provider->parent(m_path);
    if (!parent.isEmpty() && parent != m_path)
        navigateTo(parent);
}

void FilePanel::loadDir(const QString &path, bool record)
{
    core::FileSystemProvider *provider = m_provider;
    m_bridge->run<std::vector<FileEntry>>(
        [provider, path] { return provider->listDir(path); },
        [this, path, record](const std::vector<FileEntry> &entries) {
            if (record && path != m_path) {
                // Verlauf: alles nach der aktuellen Position verwerfen
                if (m_histPos >= 0 && m_histPos + 1 < m_history.size())
                    m_history.erase(m_history.begin() + int(m_histPos) + 1, m_history.end());
                m_history.append(path);
                m_histPos = m_history.size() - 1;
            }
            const bool modeChanged = hostMode() != path.startsWith(QLatin1String("net://"));
            m_path = path;
            m_pathEdit->setText(path);
            endPathEdit();
            // Spaltensatz haengt am Modus (Dateien vs. Hosts).
            if (modeChanged)
                setTableHeaders();
            buildBreadcrumb();
            updateDriveCombo();
            m_entries = entries;
            m_typeAheadBuffer.clear();   // Suchpuffer beim Verzeichniswechsel verwerfen
            ++m_thumbToken;              // Miniaturen des alten Ordners verwerfen
            m_thumbRequested.clear();
            populate(entries);
            loadVisibleThumbs();
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
    // Namen wahlweise natuerlich (datei2 vor datei10), wie im Original.
    const bool natural = core::getSettingBool(QStringLiteral("natural_sort"), true);
    const QString key = m_sortKey;
    auto nameLess = [natural](const FileEntry &a, const FileEntry &b) {
        return natural ? core::naturalLess(a.name, b.name)
                       : a.name.toLower() < b.name.toLower();
    };
    auto extOf = [](const FileEntry &e) {
        const int dot = e.name.lastIndexOf(QLatin1Char('.'));
        return dot > 0 ? e.name.mid(dot + 1).toLower() : QString();
    };

    std::vector<FileEntry> sorted = entries;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [&](const FileEntry &a, const FileEntry &b) {
        if (a.type == EntryType::Parent) return true;
        if (b.type == EntryType::Parent) return false;
        if (a.isDir() != b.isDir()) return a.isDir();
        bool less;
        if (key == QLatin1String("size"))          less = a.size < b.size;
        else if (key == QLatin1String("modified")) less = a.modified < b.modified;
        else if (key == QLatin1String("created"))  less = a.created < b.created;
        else if (key == QLatin1String("accessed")) less = a.accessed < b.accessed;
        else if (key == QLatin1String("type"))     less = int(a.type) < int(b.type);
        else if (key == QLatin1String("ext"))      less = extOf(a) < extOf(b);
        else if (key == QLatin1String("perm"))     less = a.permissions < b.permissions;
        else if (key == QLatin1String("owner"))    less = a.owner.toLower() < b.owner.toLower();
        else                                       less = nameLess(a, b);
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

    const bool showIcons = core::getSettingBool(QStringLiteral("show_file_icons"), true);
    const bool execHighlight = core::getSettingBool(QStringLiteral("exec_highlight"), true);
    const QColor execColor(core::getSettingString(QStringLiteral("exec_color"),
                                                  QStringLiteral("#3fb950")));
    m_table->setRowCount(0);
    m_rows.clear();
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
        auto *nameItem = new QTableWidgetItem(e.name);
        nameItem->setData(Qt::UserRole, e.name);
        // Programm-Logos vor den Dateinamen (abschaltbar in den Einstellungen).
        if (showIcons) {
            if (e.type == EntryType::Parent)
                nameItem->setText(QStringLiteral("↩ ") + e.name);
            else if (e.isDir())
                nameItem->setIcon(fileIcons().folder());
            else
                nameItem->setIcon(fileIcons().forName(e.name));
        } else {
            const QString prefix = e.type == EntryType::Parent ? QStringLiteral("↩ ")
                                   : e.isDir()                 ? QStringLiteral("📁 ")
                                   : e.type == EntryType::Symlink ? QStringLiteral("🔗 ")
                                                                  : QStringLiteral("📄 ");
            nameItem->setText(prefix + e.name);
        }
        // Ausfuehrbare Dateien farbig markieren (Farbe/Schalter aus den Einstellungen).
        if (execHighlight && core::isExecutable(e))
            nameItem->setForeground(execColor);
        m_table->setItem(row, 0, nameItem);
        for (int c = 1; c < m_fileCols.size(); ++c) {
            m_table->setItem(row, c,
                             new QTableWidgetItem(e.type == EntryType::Parent
                                                      ? QString()
                                                      : columnValue(m_fileCols.at(c), e)));
        }
        if (e.type != EntryType::Parent) {
            if (e.isDir()) ++dirCount; else { ++fileCount; totalSize += e.size; }
        }
        m_rows.push_back(e);
        ++row;
    }
    m_baseStatus = _t("%1 Einträge (%2 Ordner, %3 Dateien)")
                       .arg(dirCount + fileCount).arg(dirCount).arg(fileCount)
                   + QStringLiteral(" · ") + humanSize(totalSize);
    if (filtering)
        m_baseStatus += QStringLiteral(" · Filter: %1").arg(m_filter);
    updateSelectionStatus();
    // Erst hier, sonst verstellt der folgende Layout-Schritt die Breiten wieder.
    applyColumnWidths();
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

// Der aktuell markierte Eintrag (nullptr, wenn nichts oder ".." markiert ist).
const FileEntry *FilePanel::selectedEntry() const
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= int(m_rows.size()))
        return nullptr;
    const FileEntry &e = m_rows[size_t(row)];
    return e.type == EntryType::Parent ? nullptr : &e;
}

// Im Netzwerk-Modus (net://) zeigt die Pane Hosts statt Dateien — dort ergeben
// die Datei-Operationen keinen Sinn, dafuer host-spezifische Aktionen.
bool FilePanel::hostMode() const
{
    return m_path.startsWith(QLatin1String("net://"))
           && m_path.count(QLatin1Char('/')) <= 2;
}

void FilePanel::openHostMenu(const QPoint &pos)
{
    const FileEntry *entry = selectedEntry();
    QMenu menu(this);

    if (entry) {
        const QVariantMap extra = entry->extra;
        const QString ip = extra.value(QStringLiteral("ip")).toString();
        const QString mac = extra.value(QStringLiteral("mac")).toString();
        const QVariantList webList = extra.value(QStringLiteral("web")).toList();
        QVariantList portList = extra.value(QStringLiteral("ports")).toList();
        QVector<int> ports;
        for (const QVariant &p : portList)
            ports.append(p.toInt());

        // SSH nur anbieten, wenn Port 22 offen ist — sonst laeuft der Versuch
        // in einen Timeout.
        const bool sshOpen = ports.contains(22);
        QAction *connectAct = menu.addAction(
            sshOpen ? _t("SSH verbinden") : _t("SSH verbinden (Port 22 nicht offen)"));
        connectAct->setEnabled(sshOpen && !ip.isEmpty());
        connect(connectAct, &QAction::triggered, this,
                [this, ip] { emit connectToHostRequested(ip); });

        menu.addSeparator();
        if (!webList.isEmpty()) {
            QAction *web = menu.addAction(_t("Weboberfläche öffnen"));
            web->setToolTip(_t("Kontextmenü: Weboberfläche im Browser öffnen"));
            const QString url = webList.first().toString();
            connect(web, &QAction::triggered, this,
                    [url] { QDesktopServices::openUrl(QUrl(url)); });
        }
        QAction *rdp = menu.addAction(_t("RDP öffnen"));
        rdp->setEnabled(ports.contains(3389) && !ip.isEmpty());
        connect(rdp, &QAction::triggered, this, [this, ip] {
#ifdef Q_OS_WIN
            QProcess::startDetached(QStringLiteral("mstsc.exe"),
                                    {QStringLiteral("/v:") + ip});
#else
            Q_UNUSED(ip);
            QMessageBox::information(this, _t("RDP öffnen"),
                                     _t("RDP wird nur unter Windows unterstützt."));
#endif
        });

        menu.addSeparator();
        QAction *wol = menu.addAction(_t("Wake-on-LAN senden"));
        wol->setEnabled(!mac.isEmpty());
        connect(wol, &QAction::triggered, this, [this, mac] {
            if (core::wakeOnLan(mac))
                emit statusMessage(_t("Wake-on-LAN gesendet an %1").arg(mac));
            else
                QMessageBox::warning(this, _t("Wake-on-LAN"),
                                     _t("Wake-on-LAN fehlgeschlagen."));
        });

        menu.addSeparator();
        QAction *saveProfile = menu.addAction(_t("Als Server-Profil speichern"));
        saveProfile->setEnabled(!ip.isEmpty());
        connect(saveProfile, &QAction::triggered, this, [this, entry, ip] {
            core::ProfileStore store;
            store.load();
            core::ServerProfile profile;
            const QString name = entry->name.isEmpty() ? ip : entry->name;
            profile.name = store.get(name) ? name + QStringLiteral(" (") + ip
                                                 + QStringLiteral(")")
                                           : name;
            profile.host = ip;
            profile.authMethod = QStringLiteral("password");
            store.upsert(profile);
            store.save();
            emit statusMessage(_t("Server-Profil gespeichert: %1").arg(profile.name));
        });
        QAction *copyIp = menu.addAction(_t("IP kopieren"));
        copyIp->setEnabled(!ip.isEmpty());
        connect(copyIp, &QAction::triggered, this,
                [ip] { QApplication::clipboard()->setText(ip); });
        if (extra.value(QStringLiteral("shares")).toBool()) {
            QAction *shares = menu.addAction(_t("Freigaben anzeigen"));
            connect(shares, &QAction::triggered, this, [this, entry] {
                emit statusMessage(_t("Suche Freigaben auf %1 …").arg(entry->name));
                navigateTo(m_provider->join(m_path, entry->name));
            });
        }
        menu.addSeparator();
    }

    menu.addAction(_t("Erneut scannen"), this, [this] { emit rescanRequested(); });
    menu.addAction(_t("Netzwerkscanner schließen"), this,
                   [this] { emit exitNetworkModeRequested(); });
    menu.exec(m_table->viewport()->mapToGlobal(pos));
}

void FilePanel::openContextMenu(const QPoint &pos)
{
    emit activated();   // diese Pane aktiv setzen
    if (hostMode()) {
        openHostMenu(pos);
        return;
    }
    QMenu menu(this);
    const QString sel = selectedPath();
    const bool hasSel = !sel.isEmpty();
    const FileEntry *entry = selectedEntry();
    const bool isFile = entry != nullptr && !entry->isDir();
    const bool isDirSel = entry != nullptr && entry->isDir();
    const bool local = m_provider && !m_provider->isRemote;

    menu.addAction(_t("Ansehen (F3)"), this, &FilePanel::opView)->setEnabled(hasSel);
    menu.addAction(_t("Bearbeiten (F4)"), this, &FilePanel::opEdit)->setEnabled(hasSel);
    menu.addAction(_t("Ausführen (Standardprogramm)"), this, &FilePanel::opExecute)
        ->setEnabled(isFile);
    addOpenWithMenu(&menu, isFile, entry);
    menu.addSeparator();

    menu.addAction(_t("Kopieren (Ctrl+C)"), this, &FilePanel::clipCopy)->setEnabled(hasSel);
    menu.addAction(_t("Ausschneiden (Ctrl+X)"), this, &FilePanel::clipCut)->setEnabled(hasSel);
    const QMimeData *osClip = QApplication::clipboard()->mimeData();
    menu.addAction(_t("Einfügen (Ctrl+V)"), this, &FilePanel::clipPaste)
        ->setEnabled(!s_clipPaths.isEmpty() || (osClip && osClip->hasUrls()));
    menu.addSeparator();

    menu.addAction(_t("Kopieren → andere Pane (F5)"), this, [this] {
        const QString p = selectedPath();
        if (!p.isEmpty())
            emit transferRequested(p);
    })->setEnabled(hasSel);
    menu.addAction(_t("Verschieben → andere Pane"), this, [this] {
        const QString p = selectedPath();
        if (!p.isEmpty())
            emit moveRequested(p);
    })->setEnabled(hasSel);
    menu.addAction(_t("Umbenennen / Verschieben (F6)"), this, &FilePanel::opRename)
        ->setEnabled(hasSel);
    menu.addAction(_t("Rechte ändern …"), this, &FilePanel::opProperties)->setEnabled(hasSel);
    menu.addAction(_t("Löschen (F8)"), this, &FilePanel::opDelete)->setEnabled(hasSel);
    menu.addSeparator();

    QMenu *marks = menu.addMenu(_t("Markieren"));
    marks->addAction(_t("Nach Muster markieren … (Num +)"), this, [this] { markByPattern(true); });
    marks->addAction(_t("Markierung nach Muster aufheben … (Num -)"), this,
                     [this] { markByPattern(false); });
    marks->addAction(_t("Auswahl umkehren (Num *)"), this, &FilePanel::invertMarks);
    marks->addAction(_t("Alles markieren (Strg+A)"), this, &FilePanel::selectAllMarks);

    menu.addAction(_t("Prüfsumme berechnen …"), this, &FilePanel::opChecksum)->setEnabled(hasSel);
    if (local) {
        menu.addAction(_t("ZIP-Archiv erstellen …"), this, &FilePanel::opMakeZip)
            ->setEnabled(hasSel);
        if (isFile && core::isArchive(entry->name))
            menu.addAction(_t("Archiv entpacken …"), this, &FilePanel::opExtract);
    }
    menu.addSeparator();

    menu.addAction(_t("Neuer Ordner (F7)"), this, &FilePanel::opMkdir);
    menu.addAction(_t("Neue Datei …"), this, &FilePanel::opNewFile);
    menu.addAction(_t("Pfad kopieren"), this, &FilePanel::copyPathToClipboard)->setEnabled(hasSel);
    menu.addAction(_t("Eigenschaften …"), this, &FilePanel::opProperties)->setEnabled(hasSel);
    menu.addSeparator();

    if (isDirSel && local) {
        menu.addAction(_t("Alarm Trigger für Verzeichnis setzen …"), this,
                       [this, sel] { emit dirAlarmRequested(sel); });
    }
    menu.addAction(_t("Verzeichnisse vergleichen …"), this,
                   [this] { emit dirDiffRequested(); });
    menu.addSeparator();

    QAction *gridAction = menu.addAction(_t("Kachelansicht"), this,
                                         [this] { setViewMode(!m_gridMode); });
    gridAction->setCheckable(true);
    gridAction->setChecked(m_gridMode);
    QAction *hiddenAction = menu.addAction(_t("Versteckte Dateien"), this,
                                           &FilePanel::toggleHidden);
    hiddenAction->setCheckable(true);
    hiddenAction->setChecked(m_showHidden);
    menu.addAction(_t("Aktualisieren (Ctrl+R)"), this, &FilePanel::refresh);
    menu.exec(m_table->viewport()->mapToGlobal(pos));
}

void FilePanel::addOpenWithMenu(QMenu *menu, bool isFile, const FileEntry *entry)
{
    QMenu *sub = menu->addMenu(_t("Öffnen mit"));
    sub->setEnabled(isFile);
    if (!isFile || !entry)
        return;
    const int dot = entry->name.lastIndexOf(QLatin1Char('.'));
    const QString ext = dot > 0 ? entry->name.mid(dot) : QString();
    for (const auto &[name, exe] : core::programsForExtension(ext))
        sub->addAction(name, this, [this, exe] { openWithProgram(exe); });
    if (!sub->isEmpty())
        sub->addSeparator();
    sub->addAction(_t("Standardprogramm"), this, &FilePanel::opExecute);
#ifdef Q_OS_WIN
    sub->addAction(_t("Anderes Programm wählen …"), this, &FilePanel::openWithChooser);
#endif
}

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
    // Umbenennen UND/ODER Verschieben: Name und Zielordner sind frei waehlbar,
    // die Vorschau zeigt den resultierenden Zielpfad.
    RenameDialog dlg(
        _t("Umbenennen / Verschieben"), provider->basename(path), path, m_path,
        [provider](const QString &dir, const QString &name) {
            return provider->join(dir, name);
        },
        [this, provider](const QString &current) -> QString {
            DirChooserDialog chooser(m_bridge, provider, current,
                                     m_bookmarks.list(m_bookmarkKey), this);
            return chooser.exec() == QDialog::Accepted ? chooser.chosen() : QString();
        },
        m_bookmarks.list(m_bookmarkKey), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QString target = dlg.resultPath();
    if (target.isEmpty() || target == path)
        return;
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
    // Betroffene Pfade auflisten (keine Ziel-Spalte beim Loeschen).
    std::vector<PathPair> pairs;
    for (const QString &p : paths)
        pairs.emplace_back(p, QString());
    PathConfirmDialog::Options options;
    options.showTarget = false;
    options.confirmText = _t("Löschen");
    options.sourceHeader = _t("Wird gelöscht");
    if (!PathConfirmDialog::confirm(
            _t("Löschen"),
            _t("%1 Objekt(e) unwiderruflich löschen?").arg(paths.size()),
            pairs, options, this))
        return;
    core::FileSystemProvider *provider = m_provider;
    m_bridge->run(
        [provider, paths] {
            for (const QString &p : paths)
                provider->remove(p, true);
        },
        [this, count = paths.size()] {
            refresh();
            emit statusMessage(_t("%1 gelöscht").arg(count));
        },
        [this](const QString &err) {
            // Der Ordner kann teilweise geleert sein — neu einlesen.
            refresh();
            QMessageBox::warning(this, _t("Löschen fehlgeschlagen"), err);
        });
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

void FilePanel::opNewFile()
{
    if (!m_provider)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, _t("Neue Datei"),
                                               _t("Name der Datei:"), QLineEdit::Normal,
                                               QString(), &ok);
    if (!ok || name.isEmpty())
        return;
    core::FileSystemProvider *provider = m_provider;
    const QString target = provider->join(m_path, name);
    m_bridge->run(
        [provider, target] { provider->writeBytes(target, QByteArray()); },
        [this] { refresh(); },
        [this](const QString &err) { QMessageBox::warning(this, _t("Fehler"), err); });
}

// --- "Oeffnen mit" ---------------------------------------------------------
// Remote-Dateien muessen erst lokal vorliegen; fn bekommt immer einen lokalen Pfad.
void FilePanel::withLocalCopy(const std::function<void(const QString &)> &fn)
{
    const QString path = selectedPath();
    if (path.isEmpty() || !m_provider)
        return;
    if (!m_provider->isRemote) {
        fn(path);
        return;
    }
    core::FileSystemProvider *provider = m_provider;
    const QString name = provider->basename(path);
    m_bridge->run<QByteArray>(
        [provider, path] { return provider->readBytes(path, 100'000'000); },
        [this, name, fn](const QByteArray &data) {
            const QString dir = QDir::tempPath() + QStringLiteral("/sshit-open");
            QDir().mkpath(dir);
            const QString local = dir + QLatin1Char('/') + name;
            QFile f(local);
            if (!f.open(QIODevice::WriteOnly)) {
                QMessageBox::warning(this, _t("Fehler"),
                                     _t("Temporäre Datei nicht schreibbar."));
                return;
            }
            f.write(data);
            f.close();
            fn(local);
        },
        [this](const QString &err) { QMessageBox::warning(this, _t("Fehler"), err); });
}

void FilePanel::opExecute()
{
    withLocalCopy([](const QString &local) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(local));
    });
}

void FilePanel::openWithProgram(const QString &exe)
{
    withLocalCopy([this, exe](const QString &local) {
        if (!QProcess::startDetached(exe, {local}))
            QMessageBox::warning(this, _t("Fehler"),
                                 _t("Programm konnte nicht gestartet werden: %1").arg(exe));
    });
}

void FilePanel::openWithChooser()
{
    withLocalCopy([](const QString &local) {
        // Windows-Standarddialog "Öffnen mit".
        QProcess::startDetached(QStringLiteral("rundll32.exe"),
                                {QStringLiteral("shell32.dll,OpenAs_RunDLL"), local});
    });
}

// --- Pruefsumme / Archive ---------------------------------------------------

void FilePanel::opChecksum()
{
    const QString path = selectedPath();
    if (path.isEmpty() || !m_provider)
        return;
    const QStringList algos = {QStringLiteral("sha256"), QStringLiteral("sha1"),
                               QStringLiteral("md5"), QStringLiteral("sha512")};
    bool ok = false;
    const QString algo = QInputDialog::getItem(this, _t("Prüfsumme"), _t("Verfahren:"),
                                               algos, 0, false, &ok);
    if (!ok || algo.isEmpty())
        return;

    core::FileSystemProvider *provider = m_provider;
    const bool remote = provider->isRemote;
    m_bridge->run<QString>(
        [provider, path, algo, remote]() -> QString {
            // Lokal gestreamt; remote ueber die Provider-Bytes (gedeckelt).
            if (!remote)
                return core::hashFile(path, algo);
            return core::hashBytes(provider->readBytes(path, 100'000'000), algo);
        },
        [this, algo, path](const QString &digest) {
            auto *box = new QMessageBox(QMessageBox::Information, _t("Prüfsumme"),
                                        QStringLiteral("%1\n\n%2: %3")
                                            .arg(path, algo.toUpper(), digest),
                                        QMessageBox::Ok, this);
            box->setAttribute(Qt::WA_DeleteOnClose);
            box->setTextInteractionFlags(Qt::TextSelectableByMouse);
            box->show();
        },
        [this](const QString &err) { QMessageBox::warning(this, _t("Fehler"), err); });
}

void FilePanel::opMakeZip()
{
    if (!m_provider || m_provider->isRemote)
        return;
    const auto paths = selectedPaths();
    if (paths.empty()) {
        QMessageBox::information(this, _t("ZIP erstellen"), _t("Bitte Dateien/Ordner auswählen."));
        return;
    }
    QStringList names;
    for (const QString &p : paths)
        names << m_provider->basename(p);

    bool ok = false;
    const QString suggestion = names.size() == 1
                                   ? names.first() + QStringLiteral(".zip")
                                   : QFileInfo(m_path).fileName() + QStringLiteral(".zip");
    const QString archiveName = QInputDialog::getText(this, _t("ZIP-Archiv erstellen"),
                                                      _t("Archivname:"), QLineEdit::Normal,
                                                      suggestion, &ok);
    if (!ok || archiveName.isEmpty())
        return;
    const QString archive = m_provider->join(m_path, archiveName);
    const QString base = m_path;
    m_bridge->run<int>(
        [archive, base, names] { return core::makeZip(archive, base, names); },
        [this, archiveName](int count) {
            emit statusMessage(_t("ZIP erstellt: %1 (%2 Einträge)").arg(archiveName).arg(count));
            refresh();
        },
        [this](const QString &err) { QMessageBox::warning(this, _t("Fehler"), err); });
}

void FilePanel::opExtract()
{
    const QString path = selectedPath();
    if (path.isEmpty() || !m_provider || m_provider->isRemote)
        return;
    const QString stem = core::archiveStem(m_provider->basename(path));
    bool ok = false;
    const QString target = QInputDialog::getText(this, _t("Archiv entpacken"), _t("Zielpfad:"),
                                                 QLineEdit::Normal,
                                                 m_provider->join(m_path, stem), &ok);
    if (!ok || target.isEmpty())
        return;
    m_bridge->run<int>(
        [path, target] { return core::extractArchive(path, target); },
        [this, target](int count) {
            emit statusMessage(_t("%1 Eintrag/Einträge entpackt nach %2").arg(count).arg(target));
            refresh();
        },
        [this](const QString &err) { QMessageBox::warning(this, _t("Fehler"), err); });
}

// --- Zwischenablage ---------------------------------------------------------

core::FileSystemProvider *FilePanel::s_clipProvider = nullptr;
QStringList FilePanel::s_clipPaths;
bool FilePanel::s_clipMove = false;

void FilePanel::copyPathToClipboard()
{
    const QString path = selectedPath();
    if (!path.isEmpty())
        QApplication::clipboard()->setText(path);
}

void FilePanel::clipCopy()
{
    const auto paths = selectedPaths();
    if (paths.empty() || !m_provider)
        return;
    s_clipProvider = m_provider;
    s_clipPaths.clear();
    for (const QString &p : paths)
        s_clipPaths << p;
    s_clipMove = false;

    QClipboard *cb = QApplication::clipboard();
    if (!m_provider->isRemote) {
        // Lokale Dateien zusaetzlich als URLs — dann nimmt sie auch der Explorer.
        auto *mime = new QMimeData();
        QList<QUrl> urls;
        for (const QString &p : s_clipPaths)
            urls << QUrl::fromLocalFile(p);
        mime->setUrls(urls);
        cb->setMimeData(mime);
    } else {
        // Remote: keine OS-URLs setzen, sonst schlaegt eine aeltere lokale
        // Auswahl beim Einfuegen durch. Stattdessen die Pfade als Text.
        cb->setText(s_clipPaths.join(QLatin1Char('\n')));
    }
    emit statusMessage(_t("%1 Eintrag/Einträge kopiert.").arg(s_clipPaths.size()));
}

void FilePanel::clipCut()
{
    const auto paths = selectedPaths();
    if (paths.empty() || !m_provider)
        return;
    s_clipProvider = m_provider;
    s_clipPaths.clear();
    for (const QString &p : paths)
        s_clipPaths << p;
    s_clipMove = true;
    emit statusMessage(_t("%1 Eintrag/Einträge zum Verschieben vorgemerkt.")
                           .arg(s_clipPaths.size()));
}

void FilePanel::clipPaste()
{
    // Die interne Zwischenablage hat Vorrang — sie kennt auch Remote-Pfade
    // und den Unterschied zwischen Kopieren und Ausschneiden.
    if (!s_clipPaths.isEmpty()) {
        emit pasteRequested(s_clipMove);
        return;
    }
    const QMimeData *mime = QApplication::clipboard()->mimeData();
    if (mime && mime->hasUrls()) {
        QStringList local;
        for (const QUrl &url : mime->urls()) {
            if (url.isLocalFile())
                local << url.toLocalFile();
        }
        if (!local.isEmpty())
            emit filesDropped(local, true);
    }
}

// --- Markieren --------------------------------------------------------------

std::vector<int> FilePanel::markableRows() const
{
    std::vector<int> rows;
    for (int i = 0; i < int(m_rows.size()); ++i) {
        if (m_rows[size_t(i)].type != EntryType::Parent)
            rows.push_back(i);
    }
    return rows;
}

void FilePanel::applySelection(const std::vector<int> &rows, bool select)
{
    QItemSelectionModel *sel = m_table->selectionModel();
    const auto flag = (select ? QItemSelectionModel::Select : QItemSelectionModel::Deselect)
                      | QItemSelectionModel::Rows;
    for (int r : rows)
        sel->select(m_table->model()->index(r, 0), flag);
}

void FilePanel::clearMarks()
{
    m_table->clearSelection();
}

void FilePanel::selectAllMarks()
{
    applySelection(markableRows(), true);
}

void FilePanel::invertMarks()
{
    QSet<int> chosen;
    for (const QModelIndex &idx : m_table->selectionModel()->selectedRows())
        chosen.insert(idx.row());
    m_table->clearSelection();
    std::vector<int> rest;
    for (int r : markableRows()) {
        if (!chosen.contains(r))
            rest.push_back(r);
    }
    applySelection(rest, true);
}

void FilePanel::markByPattern(bool select)
{
    const QString title = select ? _t("Nach Muster markieren")
                                 : _t("Markierung nach Muster aufheben");
    bool ok = false;
    const QString pattern = QInputDialog::getText(this, title,
                                                  _t("Muster (Wildcards, z.B. *.jpg):"),
                                                  QLineEdit::Normal, QStringLiteral("*"), &ok);
    if (!ok)
        return;
    const QString trimmed = pattern.trimmed();
    if (trimmed.isEmpty())
        return;
    const QRegularExpression rx(
        QRegularExpression::wildcardToRegularExpression(trimmed),
        QRegularExpression::CaseInsensitiveOption);
    if (!rx.isValid())
        return;
    std::vector<int> hits;
    for (int r : markableRows()) {
        if (rx.match(m_rows[size_t(r)].name).hasMatch())
            hits.push_back(r);
    }
    applySelection(hits, select);
}

void FilePanel::markCurrent(bool select, bool toggle)
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= int(m_rows.size()))
        return;
    QItemSelectionModel *sel = m_table->selectionModel();
    const QModelIndex idx = m_table->model()->index(row, 0);
    if (m_rows[size_t(row)].type != EntryType::Parent) {
        auto command = toggle ? QItemSelectionModel::Toggle
                              : (select ? QItemSelectionModel::Select
                                        : QItemSelectionModel::Deselect);
        sel->select(idx, command | QItemSelectionModel::Rows);
    }
    // Cursor weiterbewegen, ohne die bestehende Auswahl zu veraendern.
    if (row + 1 < m_table->rowCount())
        sel->setCurrentIndex(m_table->model()->index(row + 1, 0), QItemSelectionModel::NoUpdate);
}

// --- Tippsuche (Type-Ahead) -------------------------------------------------

void FilePanel::typeAhead(const QString &ch)
{
    m_typeAheadBuffer += ch.toLower();
    m_typeAheadTimer->start();
    selectMatch(m_typeAheadBuffer);
}

void FilePanel::selectMatch(const QString &query)
{
    const auto rows = markableRows();
    int match = -1;
    for (int r : rows) {   // Praefix bevorzugt
        if (m_rows[size_t(r)].name.toLower().startsWith(query)) {
            match = r;
            break;
        }
    }
    if (match < 0) {       // sonst Teiltreffer
        for (int r : rows) {
            if (m_rows[size_t(r)].name.toLower().contains(query)) {
                match = r;
                break;
            }
        }
    }
    if (match < 0)
        return;
    m_table->selectRow(match);
    if (QTableWidgetItem *item = m_table->item(match, 0))
        m_table->scrollToItem(item);
}

void FilePanel::setSudoAvailable(bool available)
{
    m_sudoAvailable = available;
    m_sudoChip->setVisible(available);
    if (!available && m_sudoChip->isChecked())
        setSudoActive(false);
}

void FilePanel::applyShortcuts()
{
    // Nur die Pane-Operationen aus dem Katalog uebernehmen; Navigations- und
    // Markier-Tasten bleiben fest (siehe eventFilter).
    const QHash<QString, QString> shortcuts = core::getShortcuts();
    static const char *const ops[] = {"view",   "edit",  "copy",  "rename",
                                      "mkdir",  "delete", "hidden"};
    m_opShortcuts.clear();
    for (const char *id : ops) {
        const QString key = shortcuts.value(QString::fromLatin1(id));
        if (!key.isEmpty())
            m_opShortcuts.insert(QString::fromLatin1(id), QKeySequence(key));
    }
}

void FilePanel::triggerOp(const QString &id)
{
    if (id == QLatin1String("view")) opView();
    else if (id == QLatin1String("edit")) opEdit();
    else if (id == QLatin1String("copy")) {
        const QString p = selectedPath();
        if (!p.isEmpty())
            emit transferRequested(p);
    } else if (id == QLatin1String("rename")) opRename();
    else if (id == QLatin1String("mkdir")) opMkdir();
    else if (id == QLatin1String("delete")) opDelete();
    else if (id == QLatin1String("hidden")) toggleHidden();
}

void FilePanel::setSudoActive(bool active)
{
    // Ohne Blocker wuerde das Zuruecksetzen ein erneutes Umschalten ausloesen.
    QSignalBlocker blocker(m_sudoChip);
    m_sudoChip->setChecked(active);
    m_sudoActive = active;
}

void FilePanel::sortBy(int column)
{
    if (column < 0 || column >= m_fileCols.size())
        return;
    const QString key = m_fileCols.at(column);
    if (key == m_sortKey) {
        m_sortAscending = !m_sortAscending;
    } else {
        m_sortKey = key;
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

// Zeigt neben der Verzeichnis-Zusammenfassung, was gerade markiert ist.
void FilePanel::updateSelectionStatus()
{
    QSet<int> rows;
    for (const QModelIndex &idx : m_table->selectionModel()->selectedRows())
        rows.insert(idx.row());
    // ".." zaehlt nicht als Auswahl.
    qint64 size = 0;
    int count = 0;
    for (int r : rows) {
        if (r < 0 || r >= int(m_rows.size()))
            continue;
        const FileEntry &e = m_rows[size_t(r)];
        if (e.type == EntryType::Parent)
            continue;
        ++count;
        size += e.size;
    }
    if (count == 0) {
        m_status->setText(m_baseStatus);
    } else if (count == 1) {
        const FileEntry *entry = selectedEntry();
        m_status->setText(entry ? _t("%1 · ausgewählt: %2 (%3)")
                                      .arg(m_baseStatus, entry->name, humanSize(entry->size))
                                : m_baseStatus);
    } else {
        m_status->setText(_t("%1 markiert · %2").arg(count).arg(humanSize(size))
                          + QStringLiteral("  ·  ") + m_baseStatus);
    }
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

// --- Drag & Drop -----------------------------------------------------------
// Angenommen werden Datei-URLs (aus dem Explorer) und unsere eigene MIME-Form
// "application/x-sshit-paths" (Ziehen zwischen den Panes, auch remote).

static const char *const kPathsMime = "application/x-sshit-paths";

void FilePanel::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls() || event->mimeData()->hasFormat(kPathsMime))
        event->acceptProposedAction();
}

void FilePanel::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasUrls() || event->mimeData()->hasFormat(kPathsMime))
        event->acceptProposedAction();
}

void FilePanel::dropEvent(QDropEvent *event)
{
    const QMimeData *mime = event->mimeData();
    // Eigene Form hat Vorrang (kennt auch Remote-Pfade).
    if (mime->hasFormat(kPathsMime)) {
        const QStringList paths =
            QString::fromUtf8(mime->data(kPathsMime)).split(QLatin1Char('\n'),
                                                            Qt::SkipEmptyParts);
        if (!paths.isEmpty()) {
            emit filesDropped(paths, false);
            event->acceptProposedAction();
        }
        return;
    }
    if (mime->hasUrls()) {
        QStringList paths;
        for (const QUrl &url : mime->urls()) {
            if (url.isLocalFile())
                paths << url.toLocalFile();
        }
        if (!paths.isEmpty()) {
            emit filesDropped(paths, true);
            event->acceptProposedAction();
        }
    }
}

bool FilePanel::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::FocusIn || event->type() == QEvent::MouseButtonPress) {
        emit activated();
    }
    // Klick auf die freie Flaeche neben den Breadcrumbs -> Pfad eingeben.
    if (m_crumbScroll && obj == m_crumbScroll->widget()
        && event->type() == QEvent::MouseButtonPress) {
        beginPathEdit();
        return true;
    }
    // Pfadfeld verlassen -> zurueck zur Breadcrumb-Ansicht.
    if (obj == m_pathEdit && event->type() == QEvent::FocusOut) {
        endPathEdit();
        return false;
    }
    // Ziehen aus der Pane starten (auch fuer Remote-Pfade und den Explorer).
    if (obj == m_table->viewport() && event->type() == QEvent::MouseMove) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->buttons() & Qt::LeftButton) {
            const auto paths = selectedPaths();
            if (!paths.empty() && m_provider) {
                QStringList list;
                for (const QString &p : paths)
                    list << p;
                auto *mime = new QMimeData();
                mime->setData(kPathsMime, list.join(QLatin1Char('\n')).toUtf8());
                // Lokale Pfade zusaetzlich als URLs, damit der Explorer sie annimmt.
                if (!m_provider->isRemote) {
                    QList<QUrl> urls;
                    for (const QString &p : list)
                        urls << QUrl::fromLocalFile(p);
                    mime->setUrls(urls);
                }
                auto *drag = new QDrag(this);
                drag->setMimeData(mime);
                drag->exec(Qt::CopyAction);
                return true;
            }
        }
    }
    if (event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        // Konfigurierbare Datei-Operationen zuerst: das in den Einstellungen
        // hinterlegte Kuerzel gewinnt gegen die feste F3..F8-Vorgabe.
        if (obj != m_filterEdit) {
            const QKeySequence pressed(ke->keyCombination());
            for (auto it = m_opShortcuts.constBegin(); it != m_opShortcuts.constEnd(); ++it) {
                if (!it.value().isEmpty() && pressed == it.value()) {
                    triggerOp(it.key());
                    return true;
                }
            }
        }
        switch (ke->key()) {
        case Qt::Key_Backspace: goUp(); return true;
        case Qt::Key_Left:
            if (ke->modifiers() & Qt::AltModifier) { goBack(); return true; }
            break;
        case Qt::Key_Right:
            if (ke->modifiers() & Qt::AltModifier) { goForward(); return true; }
            break;
        case Qt::Key_Insert:
            if (obj != m_filterEdit) { markCurrent(false, /*toggle=*/true); return true; }
            break;
        case Qt::Key_Plus:                // Num + : nach Muster markieren
            if (obj != m_filterEdit) { markByPattern(true); return true; }
            break;
        case Qt::Key_Minus:               // Num - : Markierung aufheben
            if (obj != m_filterEdit) { markByPattern(false); return true; }
            break;
        case Qt::Key_Asterisk:            // Num * : Auswahl umkehren
            if (obj != m_filterEdit) { invertMarks(); return true; }
            break;
        case Qt::Key_A:
            if (ke->modifiers() & Qt::ControlModifier) { selectAllMarks(); return true; }
            break;
        case Qt::Key_C:
            if (ke->modifiers() & Qt::ControlModifier) { clipCopy(); return true; }
            break;
        case Qt::Key_X:
            if (ke->modifiers() & Qt::ControlModifier) { clipCut(); return true; }
            break;
        case Qt::Key_V:
            if (ke->modifiers() & Qt::ControlModifier) { clipPaste(); return true; }
            break;
        case Qt::Key_R:
            if (ke->modifiers() & Qt::ControlModifier) { refresh(); return true; }
            break;
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
        // Tippsuche: druckbare Zeichen springen zum passenden Eintrag.
        if (obj == m_table && !(ke->modifiers() & (Qt::ControlModifier | Qt::AltModifier))) {
            const QString text = ke->text();
            if (!text.isEmpty() && text.at(0).isPrint() && text.at(0) != QLatin1Char(' ')) {
                typeAhead(text);
                return true;
            }
            if (ke->key() == Qt::Key_Space) {   // Leertaste = markieren + weiter
                markCurrent(false, /*toggle=*/true);
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

} // namespace ncssh::gui
