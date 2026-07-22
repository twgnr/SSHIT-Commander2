// Konsole: Befehl -> Ausgabe mit CWD-Sync und Historie, ueber den CommandRunner
// (lokal oder remote). Remote-Befehle laufen ueber ein PTY (runTerminal), lokale
// zeilenbasiert.  (Port von gui/console_panel.py + console_widget.py, zusammengefasst)
#pragma once

#include "ncssh/core/history.hpp"
#include "ncssh/core/runner.hpp"
#include "ncssh/gui/bridge.hpp"

#include <QStringList>
#include <QWidget>
#include <memory>

class QPlainTextEdit;
class QLineEdit;
class QLabel;

namespace ncssh::gui {

class FilePanel;

class ConsolePanel : public QWidget {
    Q_OBJECT
public:
    explicit ConsolePanel(AsyncBridge *bridge, const QString &title, QWidget *parent = nullptr);

    // Runner setzen (Eigentum beim Aufrufer). cwd wird uebernommen.
    void setRunner(core::CommandRunner *runner, const QString &cwd);
    void setCwd(const QString &cwd);
    QString cwd() const { return m_cwd; }

    void runCommand(const QString &command, bool execute = true);

signals:
    void activated();
    void cwdChanged(const QString &cwd);   // durch 'cd' in der Konsole
    void statusMessage(const QString &msg);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void submit();
    void appendOutput(const QString &text);

    AsyncBridge *m_bridge;
    core::CommandRunner *m_runner = nullptr;
    QString m_cwd;

    QLabel *m_header = nullptr;
    QPlainTextEdit *m_output = nullptr;
    QLineEdit *m_input = nullptr;
    QLabel *m_prompt = nullptr;

    core::HistoryStore m_historyStore;
    QStringList m_history;
    int m_historyPos = -1;
    BridgeTask *m_running = nullptr;
};

} // namespace ncssh::gui
