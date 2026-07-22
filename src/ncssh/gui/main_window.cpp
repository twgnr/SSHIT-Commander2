#include "ncssh/gui/main_window.hpp"

#include "ncssh/core/ai.hpp"
#include "ncssh/core/assets.hpp"
#include "ncssh/core/bookmarks.hpp"
#include "ncssh/core/i18n.hpp"
#include "ncssh/core/settings.hpp"
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
#include "ncssh/gui/known_hosts_dialog.hpp"
#include "ncssh/gui/macro_manager_dialog.hpp"
#include "ncssh/gui/search_dialog.hpp"
#include "ncssh/gui/settings_dialog.hpp"
#include "ncssh/gui/server_manager.hpp"
#include "ncssh/gui/style.hpp"
#include "ncssh/gui/transfer_dialog.hpp"
#include "ncssh/gui/transfer_manager.hpp"
#include "ncssh/gui/tunnel_dialog.hpp"
#include "ncssh/gui/workspace.hpp"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFileInfo>
#include <QInputDialog>
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
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
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
        if (failed == 0) {
            statusBar()->showMessage(_t("Übertragung abgeschlossen") + QStringLiteral(" — ")
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
    connect(m_fileAlarms, &FileAlarmManager::event, this,
            [this](const QString &kind, const QString &path, const QString &name) {
                // Art in Klartext — "created"/"modified"/"deleted" sagt im
                // Statusband wenig.
                const QString label = kind == QLatin1String("created")   ? _t("neu")
                                      : kind == QLatin1String("deleted") ? _t("gelöscht")
                                                                         : _t("geändert");
                statusBar()->showMessage(
                    QStringLiteral("%1 — %2: %3")
                        .arg(_t("Alarm Trigger: %1").arg(name), label, path),
                    15000);
            });
    m_githubAlarms = new GithubAlarmManager(bridge, this);
    connect(m_githubAlarms, &GithubAlarmManager::repoChanged, this,
            [this](const QString &fullName, const QString &pushedAt) {
                statusBar()->showMessage(
                    _t("GitHub: %1").arg(fullName) + QStringLiteral(" — ")
                        + _t("Neue Daten im Repository (%1)").arg(pushedAt),
                    15000);
            });

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
    // --- Aktionen ---
    QMenu *actions = menuBar()->addMenu(_t("&Aktionen"));
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
    actions->addAction(_t("Tab-Favoriten — Tab-Layouts speichern/öffnen"), this,
                       &MainWindow::openTabFavorites);
    actions->addSeparator();
    actions->addAction(_t("Tab umbenennen …"), this, &MainWindow::renameCurrentTab);
    actions->addAction(_t("Verbindung trennen"), this, &MainWindow::disconnectCurrentTab);
    actions->addSeparator();
    actions->addAction(_t("Lesezeichen exportieren …"), this,
                       [this] { exportBookmarks(); });
    actions->addAction(_t("Lesezeichen importieren …"), this,
                       [this] { importBookmarks(); });
    actions->addSeparator();
    QAction *quitAct = actions->addAction(_t("Beenden"), this, &QWidget::close);
    quitAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q")));

    // --- Tools ---
    QMenu *tools = menuBar()->addMenu(_t("&Tools"));
    QAction *searchName = tools->addAction(_t("Datei-Suche (Name) …"), this,
                                           [this] { openSearch(QStringLiteral("name")); });
    searchName->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+F")));
    QAction *searchContent = tools->addAction(_t("Inhalts-Suche (grep) …"), this,
                                              [this] { openSearch(QStringLiteral("content")); });
    searchContent->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+F")));
    tools->addSeparator();
    QAction *bulkAct = tools->addAction(_t("Massen-Umbenennen …"), this,
                                        &MainWindow::openBulkRename);
    bulkAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+R")));
    QAction *diffAct = tools->addAction(_t("Datei-Vergleich …"), this, &MainWindow::openFileDiff);
    diffAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+D")));
    QAction *dirDiffAct = tools->addAction(_t("Verzeichnis-Vergleich …"), this,
                                           &MainWindow::openDirDiff);
    dirDiffAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));
    tools->addSeparator();
    tools->addAction(_t("Datei-Encoding konvertieren …"), this,
                     &MainWindow::openEncodingConverter);
    tools->addAction(_t("venv verwalten …"), this, &MainWindow::openVenv);
    tools->addAction(_t("Netzwerkscanner …"), this, &MainWindow::openNetscan);
    tools->addAction(_t("Sicherheits-Audit (CVE) …"), this, &MainWindow::openSecurityAudit);
    tools->addAction(_t("Plugins"), this, &MainWindow::openPlugins);
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
    QAction *settingsAct = tools->addAction(_t("Einstellungen …"), this,
                                            &MainWindow::openSettings);
    settingsAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+,")));

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
        if (Workspace *ws = currentWorkspace())
            ws->setPanesVertical(on);
    });
    panes->addSeparator();
    panes->addAction(_t("Panes tauschen"), this, [this] {
        if (Workspace *ws = currentWorkspace())
            ws->swapPanes();
    });
    panes->addAction(_t("Panes synchronisieren"), this, [this] {
        if (Workspace *ws = currentWorkspace())
            ws->syncPanes();
    });
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
    QAction *helpAct = help->addAction(_t("Hilfe"), this, [this] { openHelp(1); });
    helpAct->setShortcut(QKeySequence(Qt::Key_F1));
    help->addAction(_t("Tastenkürzel"), this, [this] { openHelp(0); });
    help->addSeparator();
    help->addAction(_t("Über SSHIT-Commander …"), this, &MainWindow::showAbout);

    // --- Toolbar (gezeichnete Icons in der Textfarbe des Themes) ---
    auto *toolbar = addToolBar(_t("Haupt"));
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar->addAction(themedIcon(QStringLiteral("connect")), _t("Verbinden"), this,
                       &MainWindow::openServerManager);
    toolbar->addAction(themedIcon(QStringLiteral("tab")), _t("Neuer Tab"), this,
                       [this] { addTab(); });
    toolbar->addSeparator();
    toolbar->addAction(themedIcon(QStringLiteral("transfers")), _t("Übertragungen"), this,
                       &MainWindow::openTransfers);
    toolbar->addAction(themedIcon(QStringLiteral("palette")), _t("Befehle"), this,
                       &MainWindow::openCommandPalette);
    toolbar->addAction(themedIcon(QStringLiteral("history")), _t("Verlauf"), this,
                       &MainWindow::openHistory);
    toolbar->addAction(themedIcon(QStringLiteral("search")), _t("Suchen"), this,
                       [this] { openSearch(QStringLiteral("content")); });
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
    m_tabs->setCurrentIndex(index);
    return ws;
}

void MainWindow::buildStatusBar()
{
    // Dauerhafte Anzeigen rechts in der Leiste — sie ueberleben kurzlebige
    // showMessage()-Meldungen.
    m_connectionLabel = new QLabel(_t("Lokales Dateisystem"), this);
    m_hostKeyLabel = new QLabel(this);
    m_tunnelLabel = new QLabel(this);
    for (QLabel *label : {m_connectionLabel, m_hostKeyLabel, m_tunnelLabel})
        statusBar()->addPermanentWidget(label);
    updateConnectionStatus();
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
    SettingsDialog dlg(this, m_bridge);
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

void MainWindow::openMacroManager()
{
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
    auto *dlg = new MacroManagerDialog(m_bridge, sshSend, sshBroadcast, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
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
