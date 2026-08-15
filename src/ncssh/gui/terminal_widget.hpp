// Interaktives Terminal: echter PTY-Shell-Channel (lokal via ConPTY, remote via
// SSH) mit ANSI-Farben, Scrollback, Kopieren/Einfuegen.
#pragma once

#include "ncssh/core/terminal_emulator.hpp"
#include "ncssh/gui/bridge.hpp"
#include "ncssh/net/ssh.hpp"

#include <QColor>
#include <QPlainTextEdit>
#include <memory>
#include <vector>

class QLineEdit;
class QLabel;
class QFile;
class QTimer;

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

    // --- Suche im Rollpuffer ---
    void setSearch(const QString &pattern);
    void searchStep(bool forward);
    void clearSearch();
    QString searchStatus() const;      // z.B. "3/17" oder leer

    // --- Mitschnitt in eine Datei ---
    bool startLogging(const QString &path);
    void stopLogging();
    bool isLogging() const { return m_logFile != nullptr; }
    QString logPath() const;

signals:
    void shellClosed();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    // Read-only blendet den Standard-Cursor aus -> Block-Cursor selbst zeichnen.
    void paintEvent(QPaintEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    void applyThemeColors();
    void attachBackend(ShellBackend *backend);
    // Leitet Ausgabe entweder an den Zeilen-Renderer (Primaerschirm) oder an den
    // Zellengitter-Emulator (Alternate-Screen: vim/htop/tmux) weiter.
    void feedOutput(const QString &data);
    void paintEmulator();
    void recomputeMatches();
    void highlightMatches();
    void showSearchBar();
    void hideSearchBar();
    void layoutSearchBar();
    void toggleLogging();
    // URL unter der Position (leer, wenn dort keine steht).
    QString urlAt(const QPoint &pos) const;

    AsyncBridge *m_bridge;
    std::unique_ptr<AnsiRenderer> m_renderer;
    ShellBackend *m_backend = nullptr;  // Qt-Parent = this

    // Vollwertiger Terminal-Emulator (Zellengitter) fuer den Alternate-Screen.
    // Der Primaerschirm laeuft weiter ueber m_renderer (Farben, Rollpuffer,
    // Suche, Mitschnitt); erst wenn eine Anwendung auf 1049/1047/47 wechselt,
    // uebernimmt der Emulator und wird ueber den Viewport gezeichnet.
    std::unique_ptr<core::TerminalEmulator> m_emu;
    bool m_altScreen = false;
    QString m_feedCarry;  // unvollstaendige Sequenz ueber Chunk-Grenzen

    QString m_searchPattern;
    std::vector<int> m_matchPositions;  // Zeichen-Offsets der Treffer
    int m_matchIndex = -1;
    QWidget *m_searchBar = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QLabel *m_searchLabel = nullptr;

    QFile *m_logFile = nullptr;         // Qt-Parent = this

    // Selbstgezeichneter Block-Cursor (blinkend).
    QColor m_termFg = QColor(QStringLiteral("#e6e6e6"));
    QColor m_termBg = QColor(QStringLiteral("#101216"));
    QTimer *m_blinkTimer = nullptr;
    bool m_cursorOn = true;
    void restartCursorBlink();          // nach Ausgabe/Fokus wieder sichtbar
};

} // namespace ncssh::gui
