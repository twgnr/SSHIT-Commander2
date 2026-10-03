// Konsole: Befehl -> Ausgabe mit CWD-Sync und Historie, ueber den CommandRunner
// (lokal oder remote). Remote-Befehle laufen ueber ein PTY (runTerminal), lokale
// zeilenbasiert.
#pragma once

#include "ncssh/core/history.hpp"
#include "ncssh/core/runner.hpp"
#include "ncssh/gui/bridge.hpp"

#include "ncssh/net/ssh.hpp"

#include <QHash>
#include <QPointer>
#include <QStringList>
#include <QWidget>
#include <memory>

class QPlainTextEdit;
class QLineEdit;
class QLabel;
class QStackedWidget;
class QTabWidget;
class QPushButton;

class QVBoxLayout;
class QTimer;

namespace ncssh::core { class FileSystemProvider; }

namespace ncssh::gui {

class FilePanel;
class LineCompleter;
class TerminalWidget;

class ConsolePanel : public QWidget {
    Q_OBJECT
public:
    explicit ConsolePanel(AsyncBridge *bridge, const QString &title, QWidget *parent = nullptr);

    // Runner setzen (Eigentum beim Aufrufer). cwd wird uebernommen.
    void setRunner(core::CommandRunner *runner, const QString &cwd);
    void setCwd(const QString &cwd);
    // Dateisystem fuer die Tab-Pfadvervollstaendigung im Befehlsmodus.
    void setCompletionProvider(core::FileSystemProvider *provider);
    QString cwd() const { return m_cwd; }

    // Session fuer den Terminal-Modus (leer = lokale Shell).
    void setSession(const net::SSHSessionPtr &session);

    // Laufendes Terminal (PTY + Lesethread) synchron stoppen. Teil des
    // geordneten Herunterfahrens: MUSS laufen, BEVOR die SSH-Session
    // geschlossen wird — sonst liest der Terminal-Thread von einer
    // freigegebenen libssh2-Session.
    void shutdownShell();

    // Beschriftung des Abdock-Knopfes umschalten.
    void setDocked(bool docked);

    // Kopfzeile der Konsole (folgt der Verbindung, nicht der Bildschirmseite).
    void setHeaderTitle(const QString &title);

    // Aktiv-Markierung (blauer Rahmen ueber #ConsolePanel[active="true"]).
    void setActive(bool active);
    // sudo-Markierung (oranger Rahmen ueber #ConsolePanel[sudo="true"]) —
    // folgt der Pane derselben Seite.
    void setSudo(bool sudo);

    // Befehl einfuegen (execute=false) bzw. ausfuehren. Im Terminal-Modus geht
    // er direkt ins laufende Terminal, sonst in die Eingabezeile.
    void runCommand(const QString &command, bool execute = true);
    // Statuszeile der Anwendung (z. B. Verbindungsfortschritt) in die Konsole
    // schreiben — sichtbar im Befehls- wie im Terminal-Modus.
    void printInfo(const QString &text, bool error = false);
    // "Abbrechen"-Chip in der Kopfzeile, solange ein Verbindungsaufbau laeuft.
    void setConnectCancelVisible(bool visible);
    // Terminalausgabe von der KI erklaeren lassen (auch ueber das Tools-Menue).
    void explainWithAi();

signals:
    void activated();
    void cwdChanged(const QString &cwd);   // durch 'cd' in der Konsole
    void statusMessage(const QString &msg);
    // "⤢ Abdocken" / "⤵ Andocken" — der Workspace fuehrt den Wechsel aus.
    void undockRequested();
    void dockRequested();
    void connectCancelRequested();
    // Knoepfe im Konsolenkopf: Befehlspalette / Verlauf fuer DIESE Konsole.
    void paletteRequested();
    void historyRequested();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    bool event(QEvent *event) override;   // Theme-Wechsel: Symbole neu zeichnen

private:
    void submit();
    void appendOutput(const QString &text);
    void switchToTerminal();
    void switchToCommands();
    void setBusy(bool busy);      // Statuszeile + Stop-Knopf nachfuehren
    void showSearch();            // Strg+F: Ausgabe durchsuchen
    void hideSearch();
    void searchStep(bool forward);
    void cancelRunning();         // laufenden Befehl abbrechen (Strg+C / Esc)
    // Mehrere Terminals je Konsole (Reiter, "+" im Kopf). m_terminal zeigt
    // immer auf den aktuellen Reiter.
    bool terminalMode() const;
    TerminalWidget *createTerminal();
    void startTerminal(TerminalWidget *terminal);   // lokal bzw. ueber m_session
    void addTerminal();
    void closeTerminal(int index);
    void renameTerminal(int index);   // Doppelklick auf den Reiter: Titel direkt editieren
    QList<TerminalWidget *> terminals() const;
    // Parameter-Hilfe: Knopf zeigen, wenn die getippte Zeile mit einem
    // bekannten Befehl beginnt; Klick oeffnet das Parameter-Fenster.
    QString osType() const;
    QString currentCommandLine() const;   // getippt, noch nicht ausgefuehrt
    void updateParamButton();
    void openParamDialog();
    void appendToCommand(const QString &text);
    void refreshIcons();

    AsyncBridge *m_bridge;
    core::CommandRunner *m_runner = nullptr;
    core::FileSystemProvider *m_completionProvider = nullptr;
    LineCompleter *m_completer = nullptr;   // Tab-Vervollstaendigung der Eingabezeile
    net::SSHSessionPtr m_session;
    QString m_cwd;

    QLabel *m_header = nullptr;
    QPushButton *m_dockButton = nullptr;
    QPushButton *m_connectCancelButton = nullptr;
    bool m_docked = true;
    QStackedWidget *m_stack = nullptr;
    QWidget *m_commandPage = nullptr;
    TerminalWidget *m_terminal = nullptr;
    QWidget *m_terminalPage = nullptr;
    QTabWidget *m_termTabs = nullptr;
    QPushButton *m_addTerminalButton = nullptr;
    QPushButton *m_paletteButton = nullptr;
    QPushButton *m_historyButton = nullptr;
    QPushButton *m_paramButton = nullptr;
    QTimer *m_paramTimer = nullptr;       // entprellt die Befehlserkennung
    QPointer<QWidget> m_paramDialog;
    QHash<QString, QString> m_helpCache;  // Hilfe-Befehl -> Ausgabe (je Verbindung)
    int m_terminalSeq = 0;        // fortlaufende Nummer fuer Reitertitel
    QPushButton *m_modeButton = nullptr;
    QPlainTextEdit *m_output = nullptr;
    QLineEdit *m_input = nullptr;
    QVBoxLayout *m_commandLayout = nullptr;
    QWidget *m_searchBar = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QLabel *m_prompt = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_stopButton = nullptr;

    core::HistoryStore m_historyStore;
    QStringList m_history;
    int m_historyPos = -1;
    QString m_historyDraft;   // halb getippte Zeile beim Blättern in der Historie
    BridgeTask *m_running = nullptr;
    quint64 m_runSeq = 0;     // Laufnummer des aktuellen Befehls (veraltete Rueckrufe erkennen)
};

} // namespace ncssh::gui
