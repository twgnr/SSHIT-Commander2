// Hauptfenster: Tabs (Arbeitsbereiche), Menues, Toolbar, Statusleiste.
// (Port von gui/main_window.py, funktional zusammengefasst)
#pragma once

#include "ncssh/core/models.hpp"
#include "ncssh/gui/bridge.hpp"
#include "ncssh/net/session.hpp"

#include <QHash>
#include <QMainWindow>
#include <memory>

class QTabWidget;
class QAction;
class QLabel;
class QToolButton;

namespace ncssh::gui {

class Workspace;
class TransferManager;
class ClipboardManager;
class FileAlarmManager;
class GithubAlarmManager;
class MacroManagerDialog;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(AsyncBridge *bridge, QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    // Klicks auf die Hinweis-Beschriftungen in der Statusleiste.
    bool eventFilter(QObject *watched, QEvent *event) override;

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
    void ensureMacroDialog();     // legt m_macroDialog bei Bedarf an
    void restoreMacroManager();   // beim Start: angedockte Makroleiste zurueckholen
    void saveSession();      // offene Tabs fuer die Wiederherstellung sichern
    void restoreSession();
    void applyThemeByName(const QString &name);
    void buildStatusBar();
    // "+"-Knopf direkt rechts neben den letzten Tab setzen.
    void moveTabPlus();
    // Konfigurierte Tastenkuerzel auf die registrierten Menue-Aktionen legen.
    void applyShortcuts();
    void openKeyTools();   // SSH-Schluessel erzeugen/konvertieren
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
    QLabel *m_alarmNotice = nullptr;    // anklickbar, oeffnet die Alarm-Liste
    QLabel *m_githubNotice = nullptr;
    // Menue-Aktionen mit konfigurierbarem Kuerzel: ID -> Aktion.
    QHash<QString, QAction *> m_shortcutActions;
    int m_pendingAlarms = 0;
    int m_pendingRepos = 0;
    std::unique_ptr<net::SessionManager> m_sessions;
    TransferManager *m_transfers = nullptr;
    ClipboardManager *m_clipboard = nullptr;
    FileAlarmManager *m_fileAlarms = nullptr;
    GithubAlarmManager *m_githubAlarms = nullptr;
    QTabWidget *m_tabs = nullptr;
    QToolButton *m_tabPlus = nullptr;  // "+" direkt neben dem letzten Tab
    MacroManagerDialog *m_macroDialog = nullptr;  // einmalig; present()/andockbar
};

} // namespace ncssh::gui
