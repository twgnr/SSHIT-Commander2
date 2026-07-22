// Konsole: Befehl -> Ausgabe mit CWD-Sync und Historie, ueber den CommandRunner
// (lokal oder remote). Remote-Befehle laufen ueber ein PTY (runTerminal), lokale
// zeilenbasiert.  (Port von gui/console_panel.py + console_widget.py, zusammengefasst)
#pragma once

#include "ncssh/core/history.hpp"
#include "ncssh/core/runner.hpp"
#include "ncssh/gui/bridge.hpp"

#include "ncssh/net/ssh.hpp"

#include <QStringList>
#include <QWidget>
#include <memory>

class QPlainTextEdit;
class QLineEdit;
class QLabel;
class QStackedWidget;
class QPushButton;

namespace ncssh::gui {

class FilePanel;
class TerminalWidget;

class ConsolePanel : public QWidget {
    Q_OBJECT
public:
    explicit ConsolePanel(AsyncBridge *bridge, const QString &title, QWidget *parent = nullptr);

    // Runner setzen (Eigentum beim Aufrufer). cwd wird uebernommen.
    void setRunner(core::CommandRunner *runner, const QString &cwd);
    void setCwd(const QString &cwd);
    QString cwd() const { return m_cwd; }

    // Session fuer den Terminal-Modus (leer = lokale Shell).
    void setSession(const net::SSHSessionPtr &session);

    // Beschriftung des Abdock-Knopfes umschalten.
    void setDocked(bool docked);

    void runCommand(const QString &command, bool execute = true);
    // Terminalausgabe von der KI erklaeren lassen (auch ueber das Tools-Menue).
    void explainWithAi();

signals:
    void activated();
    void cwdChanged(const QString &cwd);   // durch 'cd' in der Konsole
    void statusMessage(const QString &msg);
    // "⤢ Abdocken" / "⤵ Andocken" — der Workspace fuehrt den Wechsel aus.
    void undockRequested();
    void dockRequested();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void submit();
    void appendOutput(const QString &text);
    void switchToTerminal();
    void switchToCommands();

    AsyncBridge *m_bridge;
    core::CommandRunner *m_runner = nullptr;
    net::SSHSessionPtr m_session;
    QString m_cwd;

    QLabel *m_header = nullptr;
    QPushButton *m_dockButton = nullptr;
    bool m_docked = true;
    QStackedWidget *m_stack = nullptr;
    QWidget *m_commandPage = nullptr;
    TerminalWidget *m_terminal = nullptr;
    QPushButton *m_modeButton = nullptr;
    QPlainTextEdit *m_output = nullptr;
    QLineEdit *m_input = nullptr;
    QLabel *m_prompt = nullptr;

    core::HistoryStore m_historyStore;
    QStringList m_history;
    int m_historyPos = -1;
    BridgeTask *m_running = nullptr;
};

} // namespace ncssh::gui
