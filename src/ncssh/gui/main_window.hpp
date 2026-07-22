// Hauptfenster: Tabs (Arbeitsbereiche), Menues, Toolbar, Statusleiste.
// (Port von gui/main_window.py, funktional zusammengefasst)
#pragma once

#include "ncssh/core/models.hpp"
#include "ncssh/gui/bridge.hpp"
#include "ncssh/net/session.hpp"

#include <QMainWindow>
#include <memory>

class QTabWidget;
class QAction;
class QLabel;

namespace ncssh::gui {

class Workspace;
class TransferManager;
class ClipboardManager;
class FileAlarmManager;
class GithubAlarmManager;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(AsyncBridge *bridge, QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildMenus();
    Workspace *currentWorkspace() const;
    Workspace *addTab();
    void openServerManager();
    void openTransfers();
    void openCommandPalette();
    void openHistory();
    void openSearch(const QString &mode);
    void openBulkRename();
    void openFileDiff();
    void openSettings();
    void openKnownHosts();
    void openTunnels();
    void openDirDiff();
    void openNetscan();
    void openVenv();
    void openEncodingConverter();
    void openSecurityAudit();
    void openPlugins();
    void openThemeEditor();
    void openClipboard();
    void openHelp(int tab);
    void openTabFavorites();
    void openFileAlarms();
    void openGithubAlarms();
    void openMacroManager();
    void saveSession();      // offene Tabs fuer die Wiederherstellung sichern
    void restoreSession();
    void applyThemeByName(const QString &name);
    void buildStatusBar();
    // Verbindungs-, Host-Key- und Tunnel-Anzeige nachziehen.
    void updateConnectionStatus();
    // Fragt fehlende Zugangsdaten ab (Benutzer, Passphrase, Passwort).
    // false = abgebrochen.
    bool prepareCredentials(core::ServerProfile &profile);
    void showAbout();
    void explainConsoleWithAi();
    // codecheck = Fehleranalyse statt Erklaerung/Frage.
    void askAiAboutFile(bool codecheck);
    void renameCurrentTab();
    void disconnectCurrentTab();
    void broadcastCommand();     // Befehl an beide Konsolen
    void exportBookmarks();
    void importBookmarks();
    // Ansichts-Zustand des Tabs in die Menue-Haken uebernehmen.
    void syncViewActions();

    AsyncBridge *m_bridge;
    QAction *m_onlyFsAction = nullptr;
    QAction *m_onlyTermAction = nullptr;
    QAction *m_vertPanesAction = nullptr;
    QLabel *m_connectionLabel = nullptr;
    QLabel *m_hostKeyLabel = nullptr;
    QLabel *m_tunnelLabel = nullptr;
    std::unique_ptr<net::SessionManager> m_sessions;
    TransferManager *m_transfers = nullptr;
    ClipboardManager *m_clipboard = nullptr;
    FileAlarmManager *m_fileAlarms = nullptr;
    GithubAlarmManager *m_githubAlarms = nullptr;
    QTabWidget *m_tabs = nullptr;
};

} // namespace ncssh::gui
