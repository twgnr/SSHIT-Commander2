#include "ncssh/gui/main_window.hpp"

#include "ncssh/core/ai.hpp"
#include "ncssh/core/assets.hpp"
#include "ncssh/core/bookmarks.hpp"
#include "ncssh/core/i18n.hpp"
#include "ncssh/core/dateformat.hpp"
#include "ncssh/core/fileops.hpp"
#include "ncssh/core/filesystem.hpp"
#include "ncssh/core/keytools.hpp"
#include "ncssh/core/macros.hpp"
#include "ncssh/core/plugins.hpp"
#include "ncssh/core/profiles.hpp"
#include "ncssh/core/settings.hpp"
#include "ncssh/core/shortcuts.hpp"
#include "ncssh/gui/ai_chat_panel.hpp"
#include "ncssh/gui/file_dialogs.hpp"
#include "ncssh/gui/bulk_rename_dialog.hpp"
#include "ncssh/gui/clipboard_manager.hpp"
#include "ncssh/gui/command_palette.hpp"
#include "ncssh/gui/help_dialog.hpp"
#include "ncssh/gui/icons.hpp"
#include "ncssh/gui/theme_editor_dialog.hpp"
#include "ncssh/gui/diff_dialog.hpp"
#include "ncssh/gui/encoding_converter_dialog.hpp"
#include "ncssh/gui/file_panel.hpp"
#include "ncssh/gui/filealarm_dialog.hpp"
#include "ncssh/gui/filediff_dialog.hpp"
#include "ncssh/gui/githubalarm_dialog.hpp"
#include "ncssh/gui/netscan_dialog.hpp"
#include "ncssh/gui/plugins_dialog.hpp"
#include "ncssh/gui/security_dialog.hpp"
#include "ncssh/gui/tab_favorites_dialog.hpp"
#include "ncssh/gui/venv_dialog.hpp"
#include "ncssh/gui/history_dialog.hpp"
#include "ncssh/gui/key_dialog.hpp"
#include "ncssh/gui/known_hosts_dialog.hpp"
#include "ncssh/gui/macro_manager_dialog.hpp"
#include "ncssh/gui/search_dialog.hpp"
#include "ncssh/gui/settings_dialog.hpp"
#include "ncssh/gui/server_manager.hpp"
#include "ncssh/gui/style.hpp"
#include "ncssh/gui/transfer_dialog.hpp"
#include "ncssh/gui/transfer_manager.hpp"
#include "ncssh/gui/sftp_batch_dialog.hpp"
#include "ncssh/gui/tunnel_dialog.hpp"
#include "ncssh/gui/workspace.hpp"

#include <QAction>
#include <QApplication>
#include <QSystemTrayIcon>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHash>
#include <QInputDialog>
#include <QListWidget>
#include <QJsonArray>
#include <QIcon>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPixmap>
#include <QSignalBlocker>
#include <QSize>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

namespace ncssh::gui {

using core::_t;

MainWindow::MainWindow(AsyncBridge *bridge, QWidget *parent)
    : QMainWindow(parent), m_bridge(bridge)
{
    m_sessions = std::make_unique<net::SessionManager>();
    m_sessions->hostkeys.load();
    m_transfers = new TransferManager(bridge, this);
    // Sammelmeldung, sobald die Queue leerlaeuft — einzelne Jobs melden die
    // Workspaces selbst, hier zaehlt nur das Gesamtergebnis.
    connect(m_transfers, &TransferManager::jobUpdated, this, [this](int) {
        int running = 0, done = 0, failed = 0;
        for (const net::TransferJob &job : m_transfers->jobs()) {
            if (job.status == QLatin1String("running") || job.status == QLatin1String("pending"))
                ++running;
            else if (job.status == QLatin1String("done"))
                ++done;
            else if (job.status == QLatin1String("error"))
                ++failed;
        }
        if (running > 0 || (done == 0 && failed == 0))
            return;
        // Abschluss-Meldung kann in den Einstellungen abgeschaltet werden.
        if (!core::getSettingBool(QStringLiteral("notify_transfer_done"), true))
            return;
        if (failed == 0) {
            statusBar()->showMessage(_t("Übertragung fertig") + QStringLiteral(" — ")
                                         + _t("%1 Datei(en) übertragen").arg(done),
                                     10000);
        } else {
            statusBar()->showMessage(
                _t("%1 übertragen, %2 fehlgeschlagen — Details in den Übertragungen")
                    .arg(done).arg(failed),
                15000);
        }
    });
    m_clipboard = new ClipboardManager(this);
    // Alarme laufen im Hintergrund und melden sich in der Statusleiste.
    m_fileAlarms = new FileAlarmManager(bridge, this);
    // Remote-Alarme ueberwachen den Pfad auf der gerade aktiven Verbindung.
    m_fileAlarms->setSessionProvider([this]() -> net::SSHSessionPtr {
        Workspace *ws = currentWorkspace();
        return ws ? ws->session() : net::SSHSessionPtr();
    });
    connect(m_fileAlarms, &FileAlarmManager::event, this,
            [this](const QString &kind, const QString &path, const QString &name) {
                // Art in Klartext — "created"/"modified"/"deleted" sagt im
                // Statusband wenig.
                const QString label = kind == QLatin1String("created")   ? _t("neu")
                                      : kind == QLatin1String("deleted") ? _t("gelöscht")
                                                                         : _t("geändert");
                ++m_pendingAlarms;
                statusBar()->showMessage(
                    QStringLiteral("%1 — %2: %3")
                        .arg(_t("Alarm Trigger: %1").arg(name), label, path),
                    15000);
                // Anklickbarer Hinweis, der bis zum Oeffnen stehen bleibt.
                m_alarmNotice->setText(_t("Alarm ausgelöst — zum Anzeigen klicken"));
                m_alarmNotice->setVisible(true);
                // Desktop-Benachrichtigung (Tray-Ballon) + optionaler Ton, damit
                // Alarme auch bei minimiertem Fenster bemerkt werden.
                if (m_tray && core::getSettingBool(QStringLiteral("alarm_tray_notify"), true))
                    m_tray->showMessage(_t("Alarm Trigger: %1").arg(name),
                                        QStringLiteral("%1: %2").arg(label, path),
                                        QSystemTrayIcon::Information, 8000);
                if (core::getSettingBool(QStringLiteral("alarm_sound"), false))
                    QApplication::beep();
            });
    m_githubAlarms = new GithubAlarmManager(bridge, this);
    connect(m_githubAlarms, &GithubAlarmManager::repoChanged, this,
            [this](const QString &fullName, const QString &pushedAt) {
                ++m_pendingRepos;
                statusBar()->showMessage(
                    _t("GitHub: %1").arg(fullName) + QStringLiteral(" — ")
                        + _t("Neue Daten im Repository (%1)").arg(pushedAt),
                    15000);
                m_githubNotice->setText(
                    m_pendingRepos == 1
                        ? _t("GitHub: neue Daten — zum Anzeigen klicken")
                        : _t("%1 Repo(s) mit neuen Daten — zum Anzeigen klicken")
                              .arg(m_pendingRepos));
                m_githubNotice->setVisible(true);
            });

    setWindowTitle(QStringLiteral("SSHIT-Commander"));
    const QString iconPath = core::assetPath(QStringLiteral("sshit.png"));
    if (!iconPath.isEmpty())
        setWindowIcon(QIcon(QPixmap(iconPath)));
    resize(1180, 760);

    // Tray-Icon fuer Desktop-Benachrichtigungen (z.B. Alarm Trigger), sofern das
    // System welche unterstuetzt. Klick auf den Ballon holt das Fenster nach vorn.
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        m_tray = new QSystemTrayIcon(windowIcon(), this);
        m_tray->setToolTip(QStringLiteral("SSHIT-Commander"));
        connect(m_tray, &QSystemTrayIcon::messageClicked, this, [this] {
            showNormal();
            raise();
            activateWindow();
        });
        m_tray->show();
    }

    m_tabs = new QTabWidget(this);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
        if (m_tabs->count() <= 1)
            return;
        QWidget *w = m_tabs->widget(index);
        // Offene Verbindungen nicht stillschweigend kappen.
        if (auto *ws = qobject_cast<Workspace *>(w); ws && ws->isConnected()) {
            if (QMessageBox::question(
                    this, _t("Verbindung trennen"),
                    _t("Verbindung(en) dieses Tabs trennen?\n%1").arg(ws->connectionLabel()))
                != QMessageBox::Yes)
                return;
        }
        m_tabs->removeTab(index);
        w->deleteLater();
        QTimer::singleShot(0, this, &MainWindow::moveTabPlus);
    });
    setCentralWidget(m_tabs);
    // "+"-Knopf zum Anlegen eines Tabs — sitzt direkt rechts neben dem letzten
    // Tab (nicht in der Ecke), wird bei Layout-Aenderungen neu positioniert.
    m_tabPlus = new QToolButton(m_tabs);
    m_tabPlus->setObjectName(QStringLiteral("TabPlus"));
    m_tabPlus->setText(QStringLiteral("+"));
    m_tabPlus->setAutoRaise(true);
    m_tabPlus->setToolTip(_t("Neuer Tab"));
    connect(m_tabPlus, &QToolButton::clicked, this, [this] { addTab(); });
    m_tabs->tabBar()->installEventFilter(this);
    // Tab-Leiste: Doppelklick benennt um, Rechtsklick zeigt das Tab-Menue.
    m_tabs->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tabs->tabBar(), &QTabBar::tabBarDoubleClicked, this,
            [this](int index) {
                if (index >= 0) {
                    m_tabs->setCurrentIndex(index);
                    renameCurrentTab();
                }
            });
    connect(m_tabs->tabBar(), &QTabBar::customContextMenuRequested, this,
            [this](const QPoint &pos) {
                const int index = m_tabs->tabBar()->tabAt(pos);
                if (index < 0)
                    return;
                m_tabs->setCurrentIndex(index);
                QMenu menu(this);
                menu.addAction(_t("Tab umbenennen …"), this, &MainWindow::renameCurrentTab);
                menu.addAction(_t("Verbindung trennen"), this,
                               &MainWindow::disconnectCurrentTab);
                menu.exec(m_tabs->tabBar()->mapToGlobal(pos));
            });
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int) {
        syncViewActions();
        updateConnectionStatus();
    });

    buildMenus();
    buildStatusBar();
    statusBar()->showMessage(_t("Bereit."));
    restoreSession();
    if (m_tabs->count() == 0)
        addTab();
    // Zuletzt geoeffnete (evtl. angedockte) Makroleiste zurueckholen — erst nach
    // dem Aufbau, damit das Andocken ein fertiges Hauptfenster vorfindet.
    QTimer::singleShot(0, this, &MainWindow::restoreMacroManager);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveSession();
    QMainWindow::closeEvent(event);
}

void MainWindow::saveSession()
{
    if (!core::getSettingBool(QStringLiteral("restore_tabs"), true))
        return;
    QJsonArray tabs;
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (auto *ws = qobject_cast<Workspace *>(m_tabs->widget(i)))
            tabs.append(ws->toJson());
    }
    core::setSetting(QStringLiteral("session_tabs"), tabs);
}

void MainWindow::restoreSession()
{
    if (!core::getSettingBool(QStringLiteral("restore_tabs"), true))
        return;
    const QVariantList saved = core::getSetting(QStringLiteral("session_tabs")).toList();
    for (const QVariant &v : saved) {
        Workspace *ws = addTab();
        ws->restoreFrom(QJsonObject::fromVariantMap(v.toMap()));
    }
}

MainWindow::~MainWindow()
{
    if (m_sessions)
        m_sessions->closeAll();
}

void MainWindow::buildMenus()
{
    // Registriert eine Aktion unter ihrer Kuerzel-ID; das konfigurierte Kuerzel
    // wird spaeter ueber applyShortcuts() gesetzt. Vorher wurden die Kuerzel
    // fest verdrahtet, wodurch der Tastenkuerzel-Tab der Einstellungen ohne
    // Wirkung blieb.
    const auto reg = [this](const QString &id, QAction *action) {
        m_shortcutActions.insert(id, action);
        return action;
    };

    // --- Aktionen ---
    QMenu *actions = menuBar()->addMenu(_t("&Aktionen"));
    reg(QStringLiteral("connect"),
        actions->addAction(_t("SSH verbinden"), this, &MainWindow::openServerManager));
    reg(QStringLiteral("new_tab"),
        actions->addAction(_t("Neuer Tab"), this, [this] { addTab(); }));
    reg(QStringLiteral("transfers"),
        actions->addAction(_t("Übertragungen"), this, &MainWindow::openTransfers));
    reg(QStringLiteral("palette"),
        actions->addAction(_t("Befehlspalette"), this, &MainWindow::openCommandPalette));
    reg(QStringLiteral("history"),
        actions->addAction(_t("Verlauf & Favoriten"), this, &MainWindow::openHistory));
    reg(QStringLiteral("tunnels"),
        actions->addAction(_t("SSH-Tunnel"), this, &MainWindow::openTunnels));
    reg(QStringLiteral("sftp_batch"),
        actions->addAction(_t("SFTP-Batch / geplante Aufgaben …"), this,
                           &MainWindow::openSftpBatch));
    reg(QStringLiteral("tab_favorites"),
        actions->addAction(_t("Tab-Favoriten — Tab-Layouts speichern/öffnen"), this,
                           &MainWindow::openTabFavorites));
    actions->addSeparator();
    reg(QStringLiteral("rename_tab"),
        actions->addAction(_t("Tab umbenennen …"), this, &MainWindow::renameCurrentTab));
    reg(QStringLiteral("disconnect"),
        actions->addAction(_t("Verbindung trennen"), this,
                           &MainWindow::disconnectCurrentTab));
    actions->addSeparator();
    actions->addAction(_t("Lesezeichen exportieren …"), this,
                       [this] { exportBookmarks(); });
    actions->addAction(_t("Lesezeichen importieren …"), this,
                       [this] { importBookmarks(); });
    actions->addSeparator();
    QAction *quitAct = actions->addAction(_t("Beenden"), this, &QWidget::close);
    quitAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q")));   // fest, nicht konfigurierbar

    // --- Tools ---
    QMenu *tools = menuBar()->addMenu(_t("&Tools"));
    reg(QStringLiteral("search_name"),
        tools->addAction(_t("Datei-Suche (Name) …"), this,
                         [this] { openSearch(QStringLiteral("name")); }));
    reg(QStringLiteral("search_content"),
        tools->addAction(_t("Inhalts-Suche (grep) …"), this,
                         [this] { openSearch(QStringLiteral("content")); }));
    tools->addSeparator();
    reg(QStringLiteral("bulk_rename"),
        tools->addAction(_t("Massen-Umbenennen …"), this, &MainWindow::openBulkRename));
    reg(QStringLiteral("file_diff"),
        tools->addAction(_t("Datei-Vergleich …"), this, &MainWindow::openFileDiff));
    reg(QStringLiteral("dir_diff"),
        tools->addAction(_t("Verzeichnis-Vergleich …"), this, &MainWindow::openDirDiff));
    tools->addSeparator();
    reg(QStringLiteral("encoding_convert"),
        tools->addAction(_t("Datei-Encoding konvertieren …"), this,
                         &MainWindow::openEncodingConverter));
    reg(QStringLiteral("venv_setup"),
        tools->addAction(_t("venv verwalten …"), this, &MainWindow::openVenv));
    tools->addAction(_t("Netzwerkscanner …"), this, &MainWindow::openNetscan);
    tools->addAction(_t("Sicherheits-Audit (CVE) …"), this, &MainWindow::openSecurityAudit);
    tools->addAction(_t("SSH-Schlüssel erzeugen / konvertieren …"), this,
                     &MainWindow::openKeyTools);
    tools->addSeparator();

    // --- KI: arbeitet auf der markierten Datei der aktiven Pane ---
    QMenu *ai = tools->addMenu(_t("KI"));
    ai->addAction(_t("Terminalausgabe erklären"), this,
                  [this] { explainConsoleWithAi(); });
    ai->addAction(_t("Datei erklären / Frage zur Datei"), this,
                  [this] { askAiAboutFile(false); });
    ai->addAction(_t("KI-Fehleranalyse (Quellcode)"), this,
                  [this] { askAiAboutFile(true); });
    tools->addSeparator();

    tools->addAction(_t("Alarm Trigger …"), this, &MainWindow::openFileAlarms);
    tools->addAction(_t("GitHub Repo Alarm …"), this, &MainWindow::openGithubAlarms);
    tools->addAction(_t("Makro-Manager"), this, &MainWindow::openMacroManager);
    tools->addSeparator();
    tools->addAction(_t("Bekannte Host-Keys …"), this, &MainWindow::openKnownHosts);
    reg(QStringLiteral("settings"),
        tools->addAction(_t("Einstellungen …"), this, &MainWindow::openSettings));

    // --- Plugins (dynamisch: jedes Plugin als Schnellstart) ---
    m_pluginsMenu = menuBar()->addMenu(_t("&Plugins"));
    connect(m_pluginsMenu, &QMenu::aboutToShow, this, &MainWindow::populatePluginsMenu);
    populatePluginsMenu();

    // --- Clipboard ---
    QMenu *clipboard = menuBar()->addMenu(_t("Clipboard"));
    clipboard->addAction(_t("Clipboard-Manager"), this, &MainWindow::openClipboard);

    // --- Panes ---
    QMenu *panes = menuBar()->addMenu(_t("&Panes"));
    m_onlyFsAction = panes->addAction(_t("Nur Dateisystem anzeigen"));
    m_onlyFsAction->setCheckable(true);
    connect(m_onlyFsAction, &QAction::toggled, this, [this](bool on) {
        if (on && m_onlyTermAction->isChecked())
            m_onlyTermAction->setChecked(false);
        if (Workspace *ws = currentWorkspace())
            ws->setOnlyFilesystem(on);
    });
    m_onlyTermAction = panes->addAction(_t("Nur Terminal anzeigen"));
    m_onlyTermAction->setCheckable(true);
    connect(m_onlyTermAction, &QAction::toggled, this, [this](bool on) {
        if (on && m_onlyFsAction->isChecked())
            m_onlyFsAction->setChecked(false);
        if (Workspace *ws = currentWorkspace())
            ws->setOnlyTerminal(on);
    });
    m_vertPanesAction = panes->addAction(_t("Panes untereinander anzeigen"));
    m_vertPanesAction->setCheckable(true);
    connect(m_vertPanesAction, &QAction::toggled, this, [this](bool on) {
        // Wahl merken und auf ALLE Tabs anwenden — auch neue uebernehmen sie.
        core::setSetting(QStringLiteral("pane_orientation"),
                         on ? QStringLiteral("vertical") : QStringLiteral("horizontal"));
        for (int i = 0; i < m_tabs->count(); ++i) {
            if (auto *ws = qobject_cast<Workspace *>(m_tabs->widget(i)))
                ws->setPanesVertical(on);
        }
    });
    panes->addSeparator();
    reg(QStringLiteral("swap_panes"), panes->addAction(_t("Panes tauschen"), this, [this] {
        if (Workspace *ws = currentWorkspace())
            ws->swapPanes();
    }));
    reg(QStringLiteral("sync_panes"),
        panes->addAction(_t("Panes synchronisieren"), this, [this] {
            if (Workspace *ws = currentWorkspace())
                ws->syncPanes();
        }));
    // Strg+F9: Verzeichnis-Status der aktiven Pane / Lesezeichen (Auswahlzyklus).
    reg(QStringLiteral("pane_status"),
        panes->addAction(_t("Status anzeigen"), this, &MainWindow::paneStatusCycle));
    panes->addSeparator();
    QAction *broadcastAct = panes->addAction(_t("Befehl an beide Konsolen …"), this,
                                             &MainWindow::broadcastCommand);
    broadcastAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+B")));

    // --- Ansicht: Theme ---
    QMenu *view = menuBar()->addMenu(_t("&Ansicht"));
    QMenu *themeMenu = view->addMenu(_t("Theme"));
    for (const QString &name : themeNames()) {
        themeMenu->addAction(name, this, [this, name] { applyThemeByName(name); });
    }
    view->addAction(_t("Theme-Editor …"), this, &MainWindow::openThemeEditor);
    view->addSeparator();
    // Versteckte Dateien der aktiven Pane umschalten (wie im Original auch im Menue).
    view->addAction(_t("Versteckte Dateien"), this, [this] {
        if (Workspace *ws = currentWorkspace())
            if (FilePanel *panel = ws->activePanel())
                panel->toggleHidden();
    });
    // Kachelansicht global fuer alle Panes umschalten (Einstellung pane_grid).
    QAction *gridAct = view->addAction(_t("Kachelansicht"));
    gridAct->setCheckable(true);
    gridAct->setChecked(core::getSettingBool(QStringLiteral("pane_grid"), false));
    connect(gridAct, &QAction::toggled, this, [this](bool on) {
        for (int i = 0; i < m_tabs->count(); ++i) {
            if (auto *ws = qobject_cast<Workspace *>(m_tabs->widget(i))) {
                ws->leftPanel()->setViewMode(on);
                ws->rightPanel()->setViewMode(on);
            }
        }
    });
    QAction *previewAct = view->addAction(_t("Vorschau-Panel"));
    previewAct->setCheckable(true);
    previewAct->setShortcut(QKeySequence(Qt::Key_F2 | Qt::CTRL));
    connect(previewAct, &QAction::toggled, this, [this](bool on) {
        for (int i = 0; i < m_tabs->count(); ++i) {
            if (auto *ws = qobject_cast<Workspace *>(m_tabs->widget(i)))
                ws->setPreviewVisible(on);
        }
    });

    // --- Hilfe ---
    QMenu *help = menuBar()->addMenu(_t("&Hilfe"));
    reg(QStringLiteral("help"),
        help->addAction(_t("Hilfe"), this, [this] { openHelp(1); }));
    help->addAction(_t("Tastenkürzel"), this, [this] { openHelp(0); });
    help->addSeparator();
    help->addAction(_t("Über SSHIT-Commander …"), this, &MainWindow::showAbout);

    applyShortcuts();   // konfigurierte Kuerzel auf die registrierten Aktionen legen

    // --- Toolbar (gezeichnete Icons in der Textfarbe des Themes) ---
    // Wie im Original nur Icons (Text als Tooltip), 18px und kompakt — so passen
    // alle Aktionen in eine niedrige Leiste.
    auto *toolbar = addToolBar(_t("Aktionen"));
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar->setIconSize(QSize(18, 18));
    toolbar->setStyleSheet(QStringLiteral(
        "QToolButton{padding:2px;} QToolBar{padding:1px; spacing:2px;}"));

    // Datei-Operationen wirken auf die aktive Pane des aktuellen Tabs.
    auto fileOp = [this](const QString &id) {
        if (Workspace *ws = currentWorkspace())
            if (FilePanel *panel = ws->activePanel())
                panel->triggerOp(id);
    };
    auto reloadPanel = [this] {
        if (Workspace *ws = currentWorkspace())
            if (FilePanel *panel = ws->activePanel())
                panel->refresh();
    };

    toolbar->addAction(themedIcon(QStringLiteral("connect")), _t("SSH verbinden"), this,
                       &MainWindow::openServerManager);
    toolbar->addSeparator();
    toolbar->addAction(themedIcon(QStringLiteral("palette")), _t("Befehle"), this,
                       &MainWindow::openCommandPalette);
    toolbar->addAction(themedIcon(QStringLiteral("history")), _t("Verlauf"), this,
                       &MainWindow::openHistory);
    toolbar->addAction(themedIcon(QStringLiteral("transfers")), _t("Übertragungen"), this,
                       &MainWindow::openTransfers);
    toolbar->addAction(themedIcon(QStringLiteral("tunnels")), _t("Tunnel"), this,
                       &MainWindow::openTunnels);
    toolbar->addAction(themedIcon(QStringLiteral("search")), _t("Suchen"), this,
                       [this] { openSearch(QStringLiteral("content")); });
    toolbar->addSeparator();
    // Datei-Aktionen (wie im Original: wirken auf die aktive Pane).
    toolbar->addAction(themedIcon(QStringLiteral("view")), _t("Ansehen"), this,
                       [fileOp] { fileOp(QStringLiteral("view")); });
    toolbar->addAction(themedIcon(QStringLiteral("edit")), _t("Bearbeiten"), this,
                       [fileOp] { fileOp(QStringLiteral("edit")); });
    toolbar->addAction(themedIcon(QStringLiteral("copy")), _t("Kopieren"), this,
                       [fileOp] { fileOp(QStringLiteral("copy")); });
    toolbar->addAction(themedIcon(QStringLiteral("rename")), _t("Umbenennen"), this,
                       [fileOp] { fileOp(QStringLiteral("rename")); });
    toolbar->addAction(themedIcon(QStringLiteral("mkdir")), _t("Ordner"), this,
                       [fileOp] { fileOp(QStringLiteral("mkdir")); });
    toolbar->addAction(themedIcon(QStringLiteral("delete")), _t("Löschen"), this,
                       [fileOp] { fileOp(QStringLiteral("delete")); });
    toolbar->addAction(themedIcon(QStringLiteral("reload")), _t("Neu laden"), this,
                       [reloadPanel] { reloadPanel(); });
    toolbar->addSeparator();
    toolbar->addAction(themedIcon(QStringLiteral("clipboard")), _t("Clipboard"), this,
                       &MainWindow::openClipboard);
    toolbar->addAction(themedIcon(QStringLiteral("macro")), _t("Makro-Manager"), this,
                       &MainWindow::openMacroManager);
    toolbar->addSeparator();
    toolbar->addAction(themedIcon(QStringLiteral("settings")), _t("Einstellungen"), this,
                       &MainWindow::openSettings);
    toolbar->addAction(themedIcon(QStringLiteral("help")), _t("Hilfe"), this,
                       [this] { openHelp(1); });
}

// --- KI-Aktionen ------------------------------------------------------------

void MainWindow::explainConsoleWithAi()
{
    // Die Konsole kennt ihre eigene Ausgabe — dort liegt die Logik bereits.
    if (Workspace *ws = currentWorkspace())
        ws->explainActiveConsoleWithAi();
}

void MainWindow::askAiAboutFile(bool codecheck)
{
    if (!core::aiEnabled()) {
        QMessageBox::information(this, _t("KI"),
                                 _t("Die KI ist nicht aktiviert (Einstellungen → KI)."));
        return;
    }
    Workspace *ws = currentWorkspace();
    FilePanel *panel = ws ? ws->activePanel() : nullptr;
    const QString path = panel ? panel->selectedPath() : QString();
    if (path.isEmpty()) {
        QMessageBox::information(this, _t("KI"), _t("Bitte eine Datei auswählen."));
        return;
    }
    core::FileSystemProvider *provider = panel->provider();
    const QString name = provider->basename(path);

    QString question;
    if (!codecheck) {
        bool ok = false;
        question = QInputDialog::getText(this, _t("KI-Frage"),
                                         _t("Frage (leer = Datei erklären):"),
                                         QLineEdit::Normal, QString(), &ok);
        if (!ok)
            return;
    }

    m_bridge->run<QString>(
        [provider, path] { return provider->readText(path, core::AI_MAX_CONTEXT_CHARS * 2); },
        [this, name, path, question, codecheck](const QString &content) {
            if (content.trimmed().isEmpty()) {
                QMessageBox::information(this, _t("KI"), _t("Die Datei ist leer."));
                return;
            }
            // Kontext deckeln — bei Dateien zaehlt der Anfang.
            const auto [text, truncated] = core::truncateFile(content);
            Q_UNUSED(truncated);
            const QJsonArray messages =
                codecheck ? core::buildCodecheckMessages(name, text,
                                                         core::sourceLanguage(name))
                          : core::buildFileMessages(name, text, question);
            const QString title = codecheck ? _t("KI-Fehleranalyse: %1").arg(path)
                                            : _t("KI: %1").arg(path);
            auto *panel = new AiChatPanel(m_bridge, messages, title, this);
            panel->setAttribute(Qt::WA_DeleteOnClose);
            panel->show();
        },
        [this](const QString &err) { QMessageBox::warning(this, _t("Fehler"), err); });
}

void MainWindow::applyShortcuts()
{
    const QHash<QString, QString> shortcuts = core::getShortcuts();
    for (auto it = m_shortcutActions.begin(); it != m_shortcutActions.end(); ++it) {
        const QString key = shortcuts.value(it.key());
        it.value()->setShortcut(key.isEmpty() ? QKeySequence() : QKeySequence(key));
    }
}

void MainWindow::openKeyTools()
{
    // SSH-Schluessel erzeugen/konvertieren — vorher nur ueber den Server-Manager
    // erreichbar.
    KeyDialog dlg(m_bridge, this);
    dlg.exec();
}

void MainWindow::showAbout()
{
    QMessageBox::about(
        this, _t("Über SSHIT-Commander"),
        QStringLiteral("<b>SSHIT-Commander</b><br>"
                       "%1<br><br>%2<br>%3")
            .arg(_t("Dual-Pane-Dateimanager mit SSH/SFTP und Terminal."),
                 _t("C++/Qt6-Portierung der Python-Fassung."),
                 _t("SSH-Schicht: libssh2 · Oberfläche: Qt %1")
                     .arg(QString::fromLatin1(qVersion()))));
}

void MainWindow::renameCurrentTab()
{
    const int index = m_tabs->currentIndex();
    if (index < 0)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, _t("Tab umbenennen"), _t("Name:"),
                                               QLineEdit::Normal, m_tabs->tabText(index), &ok);
    if (ok && !name.isEmpty())
        m_tabs->setTabText(index, name);
}

void MainWindow::disconnectCurrentTab()
{
    Workspace *ws = currentWorkspace();
    if (!ws)
        return;
    if (!ws->isConnected()) {
        statusBar()->showMessage(_t("Diese Seite ist nicht verbunden."), 5000);
        return;
    }
    const QString label = ws->connectionLabel();
    if (QMessageBox::question(this, _t("Verbindung trennen"),
                              _t("Verbindung zu %1 trennen?").arg(label))
        != QMessageBox::Yes)
        return;
    ws->disconnectSession();
    statusBar()->showMessage(_t("Verbindung getrennt: %1").arg(label), 8000);
}

void MainWindow::broadcastCommand()
{
    Workspace *ws = currentWorkspace();
    if (!ws)
        return;
    bool ok = false;
    const QString command = QInputDialog::getText(this, _t("Broadcast"),
                                                  _t("Befehl an beide Konsolen:"),
                                                  QLineEdit::Normal, QString(), &ok);
    if (!ok || command.trimmed().isEmpty())
        return;
    ws->broadcastToConsoles(command, /*execute=*/true);
}

void MainWindow::exportBookmarks()
{
    const QString path = getSaveFileName(this, _t("Lesezeichen exportieren"),
                                         QStringLiteral("bookmarks.json"),
                                         _t("JSON-Dateien (*.json)"));
    if (path.isEmpty())
        return;
    core::BookmarkStore store;
    store.load();
    try {
        store.exportTo(path);
        statusBar()->showMessage(_t("Lesezeichen exportiert: %1").arg(path), 8000);
    } catch (const std::exception &exc) {
        QMessageBox::warning(this, _t("Fehler"), QString::fromUtf8(exc.what()));
    }
}

void MainWindow::importBookmarks()
{
    const QString path = getOpenFileName(this, _t("Lesezeichen importieren"), QString(),
                                         _t("JSON-Dateien (*.json)"));
    if (path.isEmpty())
        return;
    core::BookmarkStore store;
    store.load();
    try {
        const int count = store.importFrom(path);
        store.save();
        statusBar()->showMessage(_t("%1 Lesezeichen importiert.").arg(count), 8000);
    } catch (const std::exception &exc) {
        QMessageBox::warning(this, _t("Fehler"), QString::fromUtf8(exc.what()));
    }
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
        updateConnectionStatus();
    });
    // Verzeichnis-Vergleich aus dem Pane-Kontextmenue.
    connect(ws, &Workspace::dirDiffRequested, this, &MainWindow::openDirDiff);
    // Netzwerk-Modus: erneut scannen bzw. zu einem gefundenen Host verbinden.
    connect(ws, &Workspace::rescanRequested, this, &MainWindow::openNetscan);
    connect(ws, &Workspace::connectHostRequested, this, [this](const QString &host) {
        // Vorhandenes Profil bevorzugen, sonst eines aus der Adresse bauen.
        core::ProfileStore store;
        store.load();
        core::ServerProfile profile;
        bool found = false;
        for (const auto &p : store.profiles()) {
            if (p.host == host) {
                profile = p;
                found = true;
                break;
            }
        }
        if (!found) {
            profile.name = host;
            profile.host = host;
            profile.authMethod = QStringLiteral("password");
        }
        if (!prepareCredentials(profile))
            return;
        if (Workspace *target = currentWorkspace()) {
            statusBar()->showMessage(_t("Verbinde zu %1 …").arg(profile.display()), 8000);
            target->connectTo(profile);
        }
    });
    m_tabs->setCurrentIndex(index);
    QTimer::singleShot(0, this, &MainWindow::moveTabPlus);  // nach dem Layout platzieren
    return ws;
}

// Anklickbarer Hinweis in der Statusleiste: bleibt stehen, bis der zugehoerige
// Dialog geoeffnet wurde — kurzlebige showMessage()-Meldungen gehen sonst unter.
static QLabel *makeNotice(QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setVisible(false);
    label->setCursor(Qt::PointingHandCursor);
    label->setStyleSheet(QStringLiteral("color:#d29922; text-decoration:underline;"));
    return label;
}

void MainWindow::buildStatusBar()
{
    m_alarmNotice = makeNotice(this);
    m_alarmNotice->installEventFilter(this);
    m_githubNotice = makeNotice(this);
    m_githubNotice->installEventFilter(this);
    statusBar()->addPermanentWidget(m_alarmNotice);
    statusBar()->addPermanentWidget(m_githubNotice);

    // Dauerhafte Anzeigen rechts in der Leiste — sie ueberleben kurzlebige
    // showMessage()-Meldungen.
    m_connectionLabel = new QLabel(_t("Lokales Dateisystem"), this);
    m_hostKeyLabel = new QLabel(this);
    m_tunnelLabel = new QLabel(this);
    for (QLabel *label : {m_connectionLabel, m_hostKeyLabel, m_tunnelLabel})
        statusBar()->addPermanentWidget(label);
    updateConnectionStatus();
}

void MainWindow::moveTabPlus()
{
    if (!m_tabPlus)
        return;
    QTabBar *bar = m_tabs->tabBar();
    if (bar->count() == 0) {
        m_tabPlus->hide();
        return;
    }
    const QRect r = bar->tabRect(bar->count() - 1);
    // Rechte obere Ecke des letzten Tabs aus Tableisten- in QTabWidget-Koords.
    const QPoint topRight = bar->mapTo(m_tabs, r.topRight());
    const int side = std::max(20, r.height() - 6);
    m_tabPlus->setFixedSize(side, side);
    m_tabPlus->move(topRight.x() + 4, topRight.y() + (r.height() - side) / 2);
    m_tabPlus->show();
    m_tabPlus->raise();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // "+"-Knopf mit der Tab-Leiste mitfuehren.
    if (watched == m_tabs->tabBar()
        && (event->type() == QEvent::Resize || event->type() == QEvent::Move
            || event->type() == QEvent::LayoutRequest)) {
        moveTabPlus();
        return QMainWindow::eventFilter(watched, event);
    }
    if (event->type() == QEvent::MouseButtonRelease) {
        if (watched == m_alarmNotice) {
            m_pendingAlarms = 0;
            m_alarmNotice->setVisible(false);
            openFileAlarms();
            return true;
        }
        if (watched == m_githubNotice) {
            m_pendingRepos = 0;
            m_githubNotice->setVisible(false);
            openGithubAlarms();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::updateConnectionStatus()
{
    Workspace *ws = currentWorkspace();
    if (!ws || !ws->isConnected()) {
        m_connectionLabel->setText(_t("Lokales Dateisystem"));
        m_hostKeyLabel->clear();
        m_tunnelLabel->clear();
        return;
    }
    const net::SSHSessionPtr session = ws->session();
    m_connectionLabel->setText(_t("Verbunden: %1  ·  %2")
                                   .arg(session->label(), session->osType));

    // Host-Key-Zustand sichtbar machen — "ignore" ist ein echtes Risiko.
    const QString status = session->hostKeyStatus;
    if (status == QLatin1String("ignored")) {
        m_hostKeyLabel->setText(_t("Host-Key-Prüfung deaktiviert (unsicher)"));
        m_hostKeyLabel->setStyleSheet(QStringLiteral("color: #f85149;"));
    } else if (status == QLatin1String("known")) {
        m_hostKeyLabel->setText(_t("Host-Key bekannt und gepinnt"));
        m_hostKeyLabel->setStyleSheet(QString());
    } else {
        m_hostKeyLabel->setText(_t("Host-Key neu / unbestätigt"));
        m_hostKeyLabel->setStyleSheet(QStringLiteral("color: #d29922;"));
    }

    const auto tunnelCount = ws->tunnels()->tunnels().size();
    m_tunnelLabel->setText(tunnelCount > 0 ? _t("  ·  %1 Tunnel").arg(tunnelCount)
                                           : QString());
}

void MainWindow::syncViewActions()
{
    Workspace *ws = currentWorkspace();
    if (!ws)
        return;
    // Nur die Haken nachziehen — ohne die Umschalt-Aktion erneut auszuloesen.
    for (auto [action, value] : {std::pair{m_onlyFsAction, ws->onlyFilesystem()},
                                 std::pair{m_onlyTermAction, ws->onlyTerminal()},
                                 std::pair{m_vertPanesAction, ws->panesVertical()}}) {
        QSignalBlocker blocker(action);
        action->setChecked(value);
    }
}

Workspace *MainWindow::currentWorkspace() const
{
    return qobject_cast<Workspace *>(m_tabs->currentWidget());
}

void MainWindow::openServerManager()
{
    ServerManagerDialog dlg(m_bridge, this);
    if (dlg.exec() != QDialog::Accepted || !dlg.chosen())
        return;
    core::ServerProfile profile = *dlg.chosen();
    if (!prepareCredentials(profile))
        return;
    Workspace *ws = currentWorkspace();
    if (!ws)
        return;
    statusBar()->showMessage(_t("Verbinde zu %1 …").arg(profile.display()), 8000);
    ws->connectTo(profile);
}

bool MainWindow::prepareCredentials(core::ServerProfile &profile)
{
    // Gespeicherte Geheimnisse aus dem Schluesselbund nachladen.
    core::ProfileStore store;
    store.load();
    store.hydrate(profile);

    if (profile.username.isEmpty()) {
        bool ok = false;
        const QString user = QInputDialog::getText(
            this, _t("Benutzername"), _t("Benutzername für %1:").arg(profile.host),
            QLineEdit::Normal, QString(), &ok);
        if (!ok || user.trimmed().isEmpty())
            return false;
        profile.username = user.trimmed();
        // Benutzernamen dauerhaft merken — beim naechsten Mal keine Rueckfrage.
        store.upsert(profile);
        store.save();
    }

    if (profile.authMethod == QLatin1String("key") && !profile.keyPath.isEmpty()
        && profile.passphrase.isEmpty()
        && core::keyFileNeedsPassphrase(profile.keyPath)) {
        bool ok = false;
        const QString passphrase = QInputDialog::getText(
            this, _t("Key-Passphrase"),
            _t("Passphrase für den Schlüssel:\n%1").arg(profile.keyPath),
            QLineEdit::Password, QString(), &ok);
        if (!ok)
            return false;
        profile.passphrase = passphrase;
    }

    if (profile.authMethod == QLatin1String("password") && profile.password.isEmpty()) {
        bool ok = false;
        const QString password = QInputDialog::getText(
            this, _t("Passwort"), _t("Passwort für %1:").arg(profile.display()),
            QLineEdit::Password, QString(), &ok);
        if (!ok)
            return false;
        profile.password = password;
    }
    return true;
}

void MainWindow::openCommandPalette()
{
    Workspace *ws = currentWorkspace();
    if (!ws)
        return;
    CommandPalette palette(ws->activeOsType(), this);
    if (palette.exec() != QDialog::Accepted || palette.command().isEmpty())
        return;
    // Als destruktiv markierte Befehle nicht ungefragt ausfuehren.
    if (palette.runDirectly() && palette.isDangerous()) {
        if (QMessageBox::warning(
                this, _t("Destruktiver Befehl"),
                _t("Dieser Befehl kann Daten unwiderruflich verändern:\n\n%1\n\nWirklich "
                   "ausführen?").arg(palette.command()),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            != QMessageBox::Yes) {
            // Nicht verwerfen — nur einfuegen, damit man ihn noch anpassen kann.
            ws->sendToActiveConsole(palette.command(), false);
            return;
        }
    }
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
                                 _t("Links und rechts je eine Datei markieren."));
        return;
    }
    auto *dlg = new FileDiffDialog(m_bridge, provA, pathA, provB, pathB, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::openSettings()
{
    SettingsDialog dlg(this, m_bridge);
    if (dlg.exec() == QDialog::Accepted) {
        // Theme und Tastenkuerzel greifen sofort, Sprache/Schriftgroessen nach
        // Neustart.
        applyTheme(qApp, core::getSettingString(QStringLiteral("theme"), defaultTheme()));
        applyShortcuts();
        // Auch die Pane-Kuerzel (view/edit/…) neu belegen.
        for (int i = 0; i < m_tabs->count(); ++i) {
            if (auto *ws = qobject_cast<Workspace *>(m_tabs->widget(i))) {
                ws->leftPanel()->applyShortcuts();
                ws->rightPanel()->applyShortcuts();
            }
        }
        statusBar()->showMessage(
            _t("Einstellungen gespeichert (Pane-Schrift sofort; Terminal/Editor ab nächstem "
               "Öffnen)."),
            8000);
    }
}

void MainWindow::openKnownHosts()
{
    KnownHostsDialog dlg(&m_sessions->hostkeys, this);
    dlg.exec();
}

void MainWindow::openDirDiff()
{
    Workspace *ws = currentWorkspace();
    if (!ws || !ws->leftPanel()->provider() || !ws->rightPanel()->provider())
        return;
    auto *dlg = new DiffDialog(m_bridge, m_transfers,
                               ws->leftPanel()->provider(), ws->leftPanel()->currentPath(),
                               ws->rightPanel()->provider(), ws->rightPanel()->currentPath(),
                               this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::openNetscan()
{
    auto *dlg = new NetscanDialog(m_bridge, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    // Beim Schliessen die gefundenen Hosts in die aktive Pane uebernehmen.
    connect(dlg, &QDialog::accepted, this, [this, dlg] {
        if (dlg->hosts().empty())
            return;
        if (Workspace *ws = currentWorkspace())
            ws->showNetworkHosts(dlg->hosts(), dlg->targetPane());
    });
    dlg->show();
}

void MainWindow::openVenv()
{
    Workspace *ws = currentWorkspace();
    if (!ws)
        return;
    FilePanel *panel = ws->activePanel();
    VenvDialog dlg(m_bridge, panel->currentPath(), ws->activeOsType(), this);
    if (dlg.exec() == QDialog::Accepted) {
        // Befehlsfolge nacheinander in die aktive Konsole schicken.
        for (const QString &cmd : dlg.commands())
            ws->sendToActiveConsole(cmd, true);
        statusBar()->showMessage(
            _t("venv wird im Terminal angelegt: %1").arg(panel->currentPath()), 8000);
    }
}

void MainWindow::openEncodingConverter()
{
    Workspace *ws = currentWorkspace();
    if (!ws)
        return;
    FilePanel *panel = ws->activePanel();
    const QString path = panel->selectedPath();
    if (path.isEmpty() || !panel->provider()) {
        QMessageBox::information(this, _t("Datei-Encoding konvertieren"),
                                 _t("Bitte zuerst eine Datei markieren."));
        return;
    }
    auto *dlg = new EncodingConverterDialog(m_bridge, panel->provider(), path, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &QDialog::accepted, panel, &FilePanel::refresh);
    dlg->show();
}

void MainWindow::openSecurityAudit()
{
    Workspace *ws = currentWorkspace();
    if (!ws)
        return;
    if (!ws->session()) {
        QMessageBox::information(this, _t("Sicherheits-Audit (CVE)"),
                                 _t("Dafür muss der Tab mit einem Linux-Server verbunden sein."));
        return;
    }
    auto *dlg = new SecurityDialog(m_bridge, ws->session(), this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::openPlugins()
{
    PluginsDialog dlg(this);
    dlg.exec();
}

void MainWindow::populatePluginsMenu()
{
    // Bei jedem Oeffnen neu einlesen, damit Aenderungen ohne Neustart wirken.
    m_pluginsMenu->clear();
    const std::vector<core::plugins::Plugin> items = core::plugins::loadAll();
    for (const core::plugins::Plugin &p : items) {
        const QString label = p.name.isEmpty() ? p.exe : p.name;
        m_pluginsMenu->addAction(label, this, [this, id = p.id] {
            const auto all = core::plugins::loadAll();
            if (const core::plugins::Plugin *p = core::plugins::byId(all, id)) {
                try {
                    core::plugins::launch(*p);
                } catch (const std::exception &e) {
                    QMessageBox::warning(this, _t("Plugin"),
                                         QString::fromUtf8(e.what()));
                }
            }
        });
    }
    if (!items.empty())
        m_pluginsMenu->addSeparator();
    m_pluginsMenu->addAction(_t("Plugins verwalten …"), this, &MainWindow::openPlugins);
    m_pluginsMenu->addAction(_t("Plugin-Ordner öffnen"), this, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(core::plugins::pluginsDir()));
    });
}

// --- Strg+F9: Status-/Lesezeichen-Zyklus -----------------------------------

namespace {

QString humanBytes(qint64 bytes)
{
    double f = double(bytes);
    static const char *units[] = {"B", "K", "M", "G", "T"};
    for (int i = 0; i < 5; ++i) {
        if (f < 1024.0 || i == 4) {
            if (i == 0)
                return QStringLiteral("%1 B").arg(qint64(f));
            return QStringLiteral("%1 %2").arg(f, 0, 'f', 1).arg(QLatin1String(units[i]));
        }
        f /= 1024.0;
    }
    return {};
}

QString whenPair(const std::optional<std::pair<QString, QDateTime>> &p, const QString &fmt)
{
    if (!p)
        return QStringLiteral("—");
    return p->first.toHtmlEscaped() + QStringLiteral(" — ") + core::formatDt(p->second, fmt);
}

QString statusLoadingHtml(const QString &title)
{
    return QStringLiteral("<h2>%1</h2><p style='color:gray'>%2</p>")
        .arg(title.toHtmlEscaped(), core::_t("Wird berechnet …"));
}

QString statusErrorHtml(const QString &title, const QString &msg)
{
    return QStringLiteral("<h2>%1</h2><p style='color:#c33'>%2 %3</p>")
        .arg(title.toHtmlEscaped(), core::_t("Fehler:"), msg.toHtmlEscaped());
}

QString statusHtml(const QString &title, const QString &path,
                   const core::DirStats &d, bool recursive)
{
    const QString fmt = core::getSettingString(QStringLiteral("date_format"),
                                               QStringLiteral("DD.MM.YYYY HH24:MI"));
    QStringList rows;
    const auto row = [&](const QString &k, const QString &v) {
        rows << QStringLiteral("<tr><td style='padding:2px 12px 2px 0;color:gray'>%1</td>"
                               "<td style='padding:2px 0'>%2</td></tr>")
                    .arg(k, v);
    };
    row(core::_t("Pfad"), path.toHtmlEscaped());
    row(core::_t("Gesamtgröße"),
        humanBytes(d.size) + (d.truncated ? QStringLiteral(" +") : QString()));
    row(core::_t("Dateien"), QString::number(d.files));
    row(core::_t("Verzeichnisse"), QString::number(d.dirs));
    row(core::_t("Versteckte Dateien"), QString::number(d.hidden));
    row(core::_t("Zuletzt erstellt"), whenPair(d.newestCreated, fmt));
    row(core::_t("Zuletzt geändert"), whenPair(d.newestModified, fmt));
    row(core::_t("Älteste (geändert)"), whenPair(d.oldestModified, fmt));
    row(core::_t("Größte Datei"),
        d.largest ? d.largest->first.toHtmlEscaped() + QStringLiteral(" — ")
                        + humanBytes(d.largest->second)
                  : QStringLiteral("—"));
    if (!d.topExt.empty()) {
        QStringList tops;
        for (const auto &e : d.topExt)
            tops << QStringLiteral("%1 (%2)").arg(e.first.toHtmlEscaped()).arg(e.second);
        row(core::_t("Häufigste Endungen"), tops.join(QStringLiteral(", ")));
    }
    const QString scope = recursive ? core::_t("rekursiv") : core::_t("nur direkte Ebene");
    return QStringLiteral("<h2>%1</h2><p style='color:gray'>%2 (%3)</p><table>%4</table>"
                          "<p style='color:gray'>%5</p>")
        .arg(title.toHtmlEscaped(), core::_t("Verzeichnis-Status"), scope,
             rows.join(QString()),
             core::_t("Strg+F9 oder Navigieren schließt die Anzeige."));
}

// Flache Statistik aus einer Verzeichnisliste (nur direkte Ebene, fuer Remote).
core::DirStats shallowStats(const std::vector<core::FileEntry> &entries)
{
    core::DirStats d;
    QHash<QString, int> ext;
    for (const core::FileEntry &e : entries) {
        if (e.type == core::EntryType::Parent)
            continue;
        if (e.hidden)
            ++d.hidden;
        if (e.type == core::EntryType::Dir) {
            ++d.dirs;
        } else {
            ++d.files;
            d.size += e.size;
            const int dot = e.name.lastIndexOf(QLatin1Char('.'));
            const QString en = (dot > 0) ? e.name.mid(dot).toLower() : core::_t("(ohne)");
            ext[en] += 1;
            if (!d.largest || e.size > d.largest->second)
                d.largest = std::make_pair(e.name, e.size);
        }
        if (e.modified.isValid()) {
            if (!d.newestModified || e.modified > d.newestModified->second)
                d.newestModified = std::make_pair(e.name, e.modified);
            if (!d.oldestModified || e.modified < d.oldestModified->second)
                d.oldestModified = std::make_pair(e.name, e.modified);
        }
        if (e.created.isValid()
            && (!d.newestCreated || e.created > d.newestCreated->second))
            d.newestCreated = std::make_pair(e.name, e.created);
    }
    // Top-5-Endungen nach Haeufigkeit, dann alphabetisch.
    std::vector<std::pair<QString, int>> all(ext.constKeyValueBegin(), ext.constKeyValueEnd());
    std::sort(all.begin(), all.end(), [](const auto &a, const auto &b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    if (all.size() > 5)
        all.resize(5);
    d.topExt = all;
    return d;
}

} // namespace

FilePanel *MainWindow::otherPanel() const
{
    Workspace *ws = currentWorkspace();
    if (!ws)
        return nullptr;
    FilePanel *active = ws->activePanel();
    return (active == ws->leftPanel()) ? ws->rightPanel() : ws->leftPanel();
}

void MainWindow::paneStatusCycle()
{
    if (!m_cycleTimer) {
        m_cycleTimer = new QTimer(this);
        m_cycleTimer->setSingleShot(true);
        connect(m_cycleTimer, &QTimer::timeout, this, &MainWindow::cycleCommit);
    }
    if (m_cyclePopup && m_cyclePopup->isVisible())
        cycleAdvance();
    else
        cycleStart();
    m_cycleTimer->start(2000);
}

void MainWindow::cycleStart()
{
    Workspace *ws = currentWorkspace();
    FilePanel *active = ws ? ws->activePanel() : nullptr;
    if (!active)
        return;
    m_cycleLabels.clear();
    m_cycleKinds.clear();
    m_cycleValues.clear();
    m_cycleLabels << _t("Status anzeigen");
    m_cycleKinds << QStringLiteral("status");
    m_cycleValues << QString();
    for (const QString &p : active->bookmarkList()) {
        m_cycleLabels << p;
        m_cycleKinds << QStringLiteral("bookmark");
        m_cycleValues << p;
    }
    m_cycleIndex = 0;
    cycleShow();
}

void MainWindow::cycleShow()
{
    if (!m_cyclePopup) {
        m_cyclePopup = new QFrame(this);
        m_cyclePopup->setObjectName(QStringLiteral("CyclePopup"));
        m_cyclePopup->setFrameShape(QFrame::StyledPanel);
        m_cyclePopup->setStyleSheet(QStringLiteral(
            "#CyclePopup{background:palette(window);border:1px solid palette(mid);}"));
        auto *lay = new QVBoxLayout(m_cyclePopup);
        lay->setContentsMargins(6, 6, 6, 6);
        m_cycleList = new QListWidget(m_cyclePopup);
        m_cycleList->setFocusPolicy(Qt::NoFocus);
        connect(m_cycleList, &QListWidget::itemClicked, this,
                [this](QListWidgetItem *) { cycleCommit(); });
        lay->addWidget(m_cycleList);
    }
    m_cycleList->clear();
    for (int i = 0; i < m_cycleLabels.size(); ++i)
        m_cycleList->addItem((m_cycleKinds.at(i) == QLatin1String("bookmark")
                                  ? QStringLiteral("★ ")
                                  : QString())
                             + m_cycleLabels.at(i));
    m_cycleList->setCurrentRow(m_cycleIndex);
    m_cyclePopup->adjustSize();
    const int w = qMax(260, m_cyclePopup->sizeHint().width());
    const int h = qMin(320, 40 + 22 * m_cycleLabels.size());
    m_cyclePopup->resize(w, h);
    Workspace *ws = currentWorkspace();
    QWidget *ref = (ws && ws->activePanel()) ? static_cast<QWidget *>(ws->activePanel())
                                             : static_cast<QWidget *>(this);
    const QPoint center = ref->mapTo(this, ref->rect().center());
    m_cyclePopup->move(qMax(0, center.x() - w / 2), qMax(0, center.y() - h / 2));
    m_cyclePopup->show();
    m_cyclePopup->raise();
}

void MainWindow::cycleAdvance()
{
    if (m_cycleLabels.isEmpty())
        return;
    m_cycleIndex = (m_cycleIndex + 1) % m_cycleLabels.size();
    m_cycleList->setCurrentRow(m_cycleIndex);
}

void MainWindow::cycleCommit()
{
    if (m_cycleTimer)
        m_cycleTimer->stop();
    if (m_cyclePopup)
        m_cyclePopup->hide();
    if (m_cycleIndex < 0 || m_cycleIndex >= m_cycleLabels.size())
        return;
    const QString kind = m_cycleKinds.at(m_cycleIndex);
    if (kind == QLatin1String("status")) {
        showPaneStatus();
    } else if (kind == QLatin1String("bookmark")) {
        if (Workspace *ws = currentWorkspace())
            if (FilePanel *active = ws->activePanel())
                active->navigateTo(m_cycleValues.at(m_cycleIndex));
    }
}

void MainWindow::showPaneStatus()
{
    Workspace *ws = currentWorkspace();
    if (!ws)
        return;
    FilePanel *active = ws->activePanel();
    FilePanel *other = otherPanel();
    if (!active || !other)
        return;
    core::FileSystemProvider *prov = active->provider();
    if (!prov)
        return;
    const core::FileEntry *sel = active->selectedEntry();
    QString target, title;
    if (sel && sel->type == core::EntryType::Dir) {
        target = prov->join(active->currentPath(), sel->name);
        title = sel->name;
    } else {
        target = active->currentPath();
        title = prov->basename(target);
        if (title.isEmpty())
            title = target;
    }
    other->showStatus(statusLoadingHtml(title));

    if (prov->isRemote) {
        // Remote/Netzwerk: nur die direkte Ebene auswerten.
        m_bridge->run<std::vector<core::FileEntry>>(
            [prov, target] { return prov->listDir(target); },
            [other, title, target](const std::vector<core::FileEntry> &entries) {
                other->showStatus(statusHtml(title, target, shallowStats(entries), false));
            },
            [other, title](const QString &m) { other->showStatus(statusErrorHtml(title, m)); });
        return;
    }
    m_bridge->run<core::DirStats>(
        [target] { return core::dirStats(target); },
        [other, title, target](const core::DirStats &d) {
            other->showStatus(statusHtml(title, target, d, true));
        },
        [other, title](const QString &m) { other->showStatus(statusErrorHtml(title, m)); });
}

void MainWindow::openThemeEditor()
{
    ThemeEditorDialog dlg(this);
    dlg.exec();
    // Ein gerade gespeichertes Theme sofort anwenden.
    if (!dlg.savedTheme().isEmpty())
        applyThemeByName(dlg.savedTheme());
}

void MainWindow::openClipboard()
{
    ClipboardDialog dlg(m_clipboard, this);
    if (dlg.exec() == QDialog::Accepted && !dlg.chosenText().isEmpty()) {
        // Gewaehlten Text in die aktive Konsole einfuegen.
        if (Workspace *ws = currentWorkspace())
            ws->sendToActiveConsole(dlg.chosenText(), false);
        statusBar()->showMessage(
            _t("Aktiver Text in der Zwischenablage — mit Strg+V einfügen."), 8000);
    }
}

void MainWindow::openFileAlarms()
{
    FileAlarmDialog dlg(m_fileAlarms, this);
    dlg.exec();
}

void MainWindow::openGithubAlarms()
{
    GithubAlarmDialog dlg(m_githubAlarms, this);
    dlg.exec();
}

void MainWindow::ensureMacroDialog()
{
    // Einmalig anlegen und danach nur noch hervorholen — so bleibt der Zustand
    // (angedockt/schwebend, Modus) erhalten.
    if (m_macroDialog)
        return;
    // Makros koennen Befehle an die aktive bzw. alle Konsolen schicken.
    auto sshSend = [this](const QString &command, bool run) {
        if (Workspace *ws = currentWorkspace())
            ws->sendToActiveConsole(command, run);
    };
    auto sshBroadcast = [this](const QString &command, bool run) {
        for (int i = 0; i < m_tabs->count(); ++i) {
            if (auto *ws = qobject_cast<Workspace *>(m_tabs->widget(i)))
                ws->sendToActiveConsole(command, run);
        }
    };
    m_macroDialog = new MacroManagerDialog(m_bridge, sshSend, sshBroadcast, this);
}

void MainWindow::openMacroManager()
{
    // Toolbar-Knopf: zum Bearbeiten die schwebende Oberflaeche nach vorne holen
    // (eine angedockte Leiste zeigt nur die Tasten und wird dafuer abgeloest).
    ensureMacroDialog();
    m_macroDialog->openManager();
}

void MainWindow::restoreMacroManager()
{
    // Beim Start: War die Makroleiste zuletzt geoeffnet, holen wir sie zurueck
    // (angedockte Leiste erscheint dann gleich am gemerkten Rand).
    if (core::macros::load().open) {
        ensureMacroDialog();
        m_macroDialog->present();
    }
}

void MainWindow::openTabFavorites()
{
    std::vector<QJsonObject> currentTabs;
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (auto *ws = qobject_cast<Workspace *>(m_tabs->widget(i)))
            currentTabs.push_back(ws->toJson());
    }
    TabFavoritesDialog dlg(currentTabs, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    // Favorit wiederherstellen: fuer jeden gesicherten Tab einen neuen anlegen.
    for (const QJsonObject &state : dlg.chosenTabs()) {
        Workspace *ws = addTab();
        ws->restoreFrom(state);
    }
}

void MainWindow::openHelp(int tab)
{
    auto *dlg = new HelpDialog(tab, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
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

void MainWindow::openSftpBatch()
{
    Workspace *ws = currentWorkspace();
    if (!ws)
        return;
    if (!ws->session()) {
        QMessageBox::information(this, _t("SFTP-Batch / geplante Aufgaben"),
                                 _t("Dafür muss der Tab mit einem Server verbunden sein."));
        return;
    }
    // Nicht-modal, damit geplante Wiederholungen laufen koennen, waehrend man
    // weiterarbeitet. Der Dialog haelt die Sitzung ueber einen shared_ptr.
    auto *dlg = new SftpBatchDialog(m_bridge, ws->session(), this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
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
