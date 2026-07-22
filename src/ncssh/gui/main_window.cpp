#include "ncssh/gui/main_window.hpp"

#include "ncssh/core/assets.hpp"
#include "ncssh/core/i18n.hpp"
#include "ncssh/core/settings.hpp"
#include "ncssh/gui/bulk_rename_dialog.hpp"
#include "ncssh/gui/command_palette.hpp"
#include "ncssh/gui/file_panel.hpp"
#include "ncssh/gui/filediff_dialog.hpp"
#include "ncssh/gui/history_dialog.hpp"
#include "ncssh/gui/known_hosts_dialog.hpp"
#include "ncssh/gui/search_dialog.hpp"
#include "ncssh/gui/settings_dialog.hpp"
#include "ncssh/gui/server_manager.hpp"
#include "ncssh/gui/style.hpp"
#include "ncssh/gui/transfer_dialog.hpp"
#include "ncssh/gui/transfer_manager.hpp"
#include "ncssh/gui/tunnel_dialog.hpp"
#include "ncssh/gui/workspace.hpp"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QMenuBar>
#include <QMessageBox>
#include <QPixmap>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>

namespace ncssh::gui {

using core::_t;

MainWindow::MainWindow(AsyncBridge *bridge, QWidget *parent)
    : QMainWindow(parent), m_bridge(bridge)
{
    m_sessions = std::make_unique<net::SessionManager>();
    m_sessions->hostkeys.load();
    m_transfers = new TransferManager(bridge, this);

    setWindowTitle(QStringLiteral("SSHIT-Commander"));
    const QString iconPath = core::assetPath(QStringLiteral("sshit.png"));
    if (!iconPath.isEmpty())
        setWindowIcon(QIcon(QPixmap(iconPath)));
    resize(1180, 760);

    m_tabs = new QTabWidget(this);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
        if (m_tabs->count() > 1) {
            QWidget *w = m_tabs->widget(index);
            m_tabs->removeTab(index);
            w->deleteLater();
        }
    });
    setCentralWidget(m_tabs);

    buildMenus();
    statusBar()->showMessage(_t("Bereit."));
    addTab();
}

MainWindow::~MainWindow()
{
    if (m_sessions)
        m_sessions->closeAll();
}

void MainWindow::buildMenus()
{
    // --- Aktionen ---
    QMenu *actions = menuBar()->addMenu(_t("Aktionen"));
    QAction *connectAct = actions->addAction(_t("SSH verbinden"), this,
                                             &MainWindow::openServerManager);
    connectAct->setShortcut(QKeySequence(Qt::Key_F9));
    QAction *newTabAct = actions->addAction(_t("Neuer Tab"), this, [this] { addTab(); });
    newTabAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+N")));
    QAction *transfersAct = actions->addAction(_t("Übertragungen"), this,
                                               &MainWindow::openTransfers);
    transfersAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+T")));
    QAction *paletteAct = actions->addAction(_t("Befehlspalette"), this,
                                             &MainWindow::openCommandPalette);
    paletteAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+P")));
    QAction *historyAct = actions->addAction(_t("Verlauf & Favoriten"), this,
                                             &MainWindow::openHistory);
    historyAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+H")));
    QAction *tunnelAct = actions->addAction(_t("SSH-Tunnel"), this, &MainWindow::openTunnels);
    tunnelAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+T")));
    actions->addSeparator();
    QAction *quitAct = actions->addAction(_t("Beenden"), this, &QWidget::close);
    quitAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q")));

    // --- Tools ---
    QMenu *tools = menuBar()->addMenu(_t("Tools"));
    QAction *searchName = tools->addAction(_t("Datei-Suche (Name)"), this,
                                           [this] { openSearch(QStringLiteral("name")); });
    searchName->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+F")));
    QAction *searchContent = tools->addAction(_t("Inhalts-Suche (grep)"), this,
                                              [this] { openSearch(QStringLiteral("content")); });
    searchContent->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+F")));
    tools->addSeparator();
    QAction *bulkAct = tools->addAction(_t("Massen-Umbenennen"), this,
                                        &MainWindow::openBulkRename);
    bulkAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+R")));
    QAction *diffAct = tools->addAction(_t("Datei-Vergleich"), this, &MainWindow::openFileDiff);
    diffAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+D")));
    tools->addSeparator();
    tools->addAction(_t("Bekannte Host-Keys"), this, &MainWindow::openKnownHosts);
    QAction *settingsAct = tools->addAction(_t("Einstellungen"), this,
                                            &MainWindow::openSettings);
    settingsAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+,")));

    // --- Ansicht: Theme ---
    QMenu *view = menuBar()->addMenu(_t("Ansicht"));
    QMenu *themeMenu = view->addMenu(_t("Theme"));
    for (const QString &name : themeNames()) {
        themeMenu->addAction(name, this, [this, name] { applyThemeByName(name); });
    }

    // --- Hilfe ---
    QMenu *help = menuBar()->addMenu(_t("Hilfe"));
    help->addAction(_t("Über"), this, [this] {
        QMessageBox::about(this, QStringLiteral("SSHIT-Commander"),
                           QStringLiteral("<b>SSHIT-Commander</b> (C++/Qt6-Port)<br>"
                                          "Dual-Pane-Dateimanager mit SSH/SFTP-Terminal.<br>"
                                          "Backend: libssh2."));
    });

    // --- Toolbar ---
    auto *toolbar = addToolBar(_t("Haupt"));
    toolbar->setMovable(false);
    toolbar->addAction(_t("★ Verbinden"), this, &MainWindow::openServerManager);
    toolbar->addAction(_t("➕ Tab"), this, [this] { addTab(); });
}

Workspace *MainWindow::addTab()
{
    auto *ws = new Workspace(m_bridge, m_sessions.get(), m_transfers, this);
    connect(ws, &Workspace::statusMessage, this,
            [this](const QString &msg) { statusBar()->showMessage(msg, 8000); });
    const int index = m_tabs->addTab(ws, _t("Sitzung"));
    connect(ws, &Workspace::connectionChanged, this, [this, ws] {
        const int i = m_tabs->indexOf(ws);
        if (i >= 0)
            m_tabs->setTabText(i, ws->connectionLabel());
    });
    m_tabs->setCurrentIndex(index);
    return ws;
}

Workspace *MainWindow::currentWorkspace() const
{
    return qobject_cast<Workspace *>(m_tabs->currentWidget());
}

void MainWindow::openServerManager()
{
    ServerManagerDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted && dlg.chosen()) {
        Workspace *ws = currentWorkspace();
        if (ws)
            ws->connectTo(*dlg.chosen());
    }
}

void MainWindow::openCommandPalette()
{
    Workspace *ws = currentWorkspace();
    if (!ws)
        return;
    CommandPalette palette(ws->activeOsType(), this);
    if (palette.exec() == QDialog::Accepted && !palette.command().isEmpty())
        ws->sendToActiveConsole(palette.command(), palette.runDirectly());
}

void MainWindow::openHistory()
{
    Workspace *ws = currentWorkspace();
    if (!ws)
        return;
    HistoryDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted && !dlg.command().isEmpty())
        ws->sendToActiveConsole(dlg.command(), false);
}

void MainWindow::openSearch(const QString &mode)
{
    Workspace *ws = currentWorkspace();
    if (!ws)
        return;
    FilePanel *panel = ws->activePanel();
    // Die Suchmaschine arbeitet lokal; bei Remote-Panes den lokalen Pfad nehmen.
    const QString root = (panel->provider() && !panel->provider()->isRemote)
                             ? panel->currentPath()
                             : QDir::homePath();
    auto *dlg = new SearchDialog(m_bridge, mode, root, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &QDialog::accepted, this, [dlg, ws] {
        if (!dlg->chosenPath().isEmpty())
            ws->activePanel()->navigateTo(QFileInfo(dlg->chosenPath()).path());
    });
    dlg->show();
}

void MainWindow::openBulkRename()
{
    Workspace *ws = currentWorkspace();
    if (!ws)
        return;
    FilePanel *panel = ws->activePanel();
    if (!panel->provider())
        return;
    // Auswahl oder — falls leer — alle Dateien der Pane.
    std::vector<QString> names;
    for (const QString &p : panel->selectedPaths())
        names.push_back(panel->provider()->basename(p));
    if (names.empty()) {
        QMessageBox::information(this, _t("Massen-Umbenennen"),
                                 _t("Bitte zuerst Dateien markieren."));
        return;
    }
    BulkRenameDialog dlg(m_bridge, panel->provider(), panel->currentPath(), names, this);
    if (dlg.exec() == QDialog::Accepted)
        panel->refresh();
}

void MainWindow::openFileDiff()
{
    Workspace *ws = currentWorkspace();
    if (!ws)
        return;
    // Zwei markierte Dateien: entweder beide in einer Pane oder je eine pro Pane.
    const auto leftSel = ws->leftPanel()->selectedPaths();
    const auto rightSel = ws->rightPanel()->selectedPaths();
    core::FileSystemProvider *provA = nullptr, *provB = nullptr;
    QString pathA, pathB;
    if (leftSel.size() >= 2) {
        provA = provB = ws->leftPanel()->provider();
        pathA = leftSel[0];
        pathB = leftSel[1];
    } else if (rightSel.size() >= 2) {
        provA = provB = ws->rightPanel()->provider();
        pathA = rightSel[0];
        pathB = rightSel[1];
    } else if (!leftSel.empty() && !rightSel.empty()) {
        provA = ws->leftPanel()->provider();
        pathA = leftSel[0];
        provB = ws->rightPanel()->provider();
        pathB = rightSel[0];
    } else {
        QMessageBox::information(this, _t("Datei-Vergleich"),
                                 _t("Bitte zwei Dateien markieren (je eine pro Pane oder zwei in einer)."));
        return;
    }
    auto *dlg = new FileDiffDialog(m_bridge, provA, pathA, provB, pathB, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::openSettings()
{
    SettingsDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        // Theme greift sofort, Sprache/Schriftgroessen nach Neustart.
        applyTheme(qApp, core::getSettingString(QStringLiteral("theme"), defaultTheme()));
        statusBar()->showMessage(
            _t("Einstellungen gespeichert (Sprache/Schriftgrößen nach Neustart)."), 8000);
    }
}

void MainWindow::openKnownHosts()
{
    KnownHostsDialog dlg(&m_sessions->hostkeys, this);
    dlg.exec();
}

void MainWindow::openTunnels()
{
    Workspace *ws = currentWorkspace();
    if (!ws)
        return;
    if (!ws->session()) {
        QMessageBox::information(this, _t("SSH-Tunnel"),
                                 _t("Dafür muss der Tab mit einem Server verbunden sein."));
        return;
    }
    TunnelDialog dlg(ws->session(), ws->tunnels(), this);
    dlg.exec();
}

void MainWindow::openTransfers()
{
    auto *dlg = new TransferDialog(m_transfers, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::applyThemeByName(const QString &name)
{
    applyTheme(qApp, name);
    core::setSetting(QStringLiteral("theme"), name);
}

} // namespace ncssh::gui
