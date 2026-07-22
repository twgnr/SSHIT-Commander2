// Interaktives Terminal: echter PTY-Shell-Channel (lokal via ConPTY, remote via
// SSH) mit ANSI-Farben, Scrollback, Kopieren/Einfuegen.
// (Port von gui/terminal_widget.py, zusammengefasst)
#pragma once

#include "ncssh/gui/bridge.hpp"
#include "ncssh/net/ssh.hpp"

#include <QPlainTextEdit>
#include <memory>

namespace ncssh::gui {

class AnsiRenderer;
class ShellBackend;

class TerminalWidget : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit TerminalWidget(AsyncBridge *bridge, QWidget *parent = nullptr);
    ~TerminalWidget() override;

    // Startet eine lokale Shell bzw. eine Remote-Shell ueber die Session.
    void startLocal();
    void startRemote(const net::SSHSessionPtr &session);
    void stop();
    bool isRunning() const { return m_backend != nullptr; }

    // Sendet Text an die Shell (z.B. ein 'cd' beim Verzeichniswechsel).
    void sendText(const QString &text);

    int columns() const;
    int rows() const;

signals:
    void shellClosed();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void applyThemeColors();
    void attachBackend(ShellBackend *backend);

    AsyncBridge *m_bridge;
    std::unique_ptr<AnsiRenderer> m_renderer;
    ShellBackend *m_backend = nullptr;  // Qt-Parent = this
};

} // namespace ncssh::gui
