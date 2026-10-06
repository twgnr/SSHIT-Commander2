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
    // Shell des laufenden Terminals: "posix" (Server), "powershell" oder "cmd".
    QString shellKind() const { return m_shellKind; }

    // Sendet Text an die Shell (z.B. ein 'cd' beim Verzeichniswechsel).
    void sendText(const QString &text);
    // Zwischenablage einfuegen; Zeilenenden werden zu CR wie bei getippter
    // Eingabe, bei Bracketed Paste (DECSET 2004) eingeklammert.
    void pasteClipboard();
    // Ausgabe der Shell anzeigen: leitet sie an den Zeilen-Renderer
    // (Primaerschirm) bzw. an den Zellengitter-Emulator (Alternate-Screen:
    // vim/htop/tmux) weiter und verfolgt Maus-/Paste-Modi.
    void feedOutput(const QString &data);
    // Von der Anwendung angeforderte Modi (Maus-Reporting, Bracketed Paste).
    const core::TerminalModes &terminalModes() const { return m_modes; }
    // Eigene Meldung der Anwendung anzeigen (nicht an die Shell gesendet).
    void printLocal(const QString &text, bool error = false);
    // Die gerade getippte, noch nicht ausgefuehrte Befehlszeile (ohne Prompt)
    // — geschaetzt aus der Cursorzeile. Leer in Vollbild-Programmen (vim, top).
    QString currentInputLine() const;

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
    // Enter in einem Terminal, dessen Shell beendet ist: neue Shell gewuenscht.
    void restartRequested();

protected:
    bool event(QEvent *event) override;   // Theme-Wechsel (themeChangedEventType)
    void keyPressEvent(QKeyEvent *event) override;
    // Tab/Shift+Tab gehoeren der Shell (Vervollstaendigung), nicht der
    // Fokus-Kette: ein schreibgeschuetztes QPlainTextEdit gaebe sie sonst ab,
    // und keyPressEvent saehe die Taste nie.
    bool focusNextPrevChild(bool) override { return false; }
    void resizeEvent(QResizeEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    // Maus: hat die Vollbild-Anwendung Maus-Reporting angefordert (vim, htop,
    // tmux, mc), gehen Klicks/Bewegung/Rad an sie; Shift umgeht das.
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    // Read-only blendet den Standard-Cursor aus -> Block-Cursor selbst zeichnen.
    void paintEvent(QPaintEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    void applyThemeColors();
    // Schrift aus den Einstellungen (terminal_font_family/-size) uebernehmen;
    // true, wenn sie sich geaendert hat.
    bool applyTerminalFont();
    // Spalten/Zeilen an Shell und Emulator melden (nach Groessen-/Schriftwechsel).
    void syncTerminalSize();
    void attachBackend(ShellBackend *backend);
    void sendBytes(const QByteArray &data);
    // Maus-Ereignisse gehen an die Anwendung (statt Markieren/Kontextmenue).
    bool mouseReporting(Qt::KeyboardModifiers mods) const;
    // Zelle (Spalte, Zeile) des Emulator-Gitters unter einer Viewport-Position.
    QPoint cellAt(const QPoint &pos) const;
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
    bool m_shellEnded = false;          // Shell hat sich selbst beendet (Enter = Neustart)
    QString m_shellKind;

    // Vollwertiger Terminal-Emulator (Zellengitter) fuer den Alternate-Screen.
    // Der Primaerschirm laeuft weiter ueber m_renderer (Farben, Rollpuffer,
    // Suche, Mitschnitt); erst wenn eine Anwendung auf 1049/1047/47 wechselt,
    // uebernimmt der Emulator und wird ueber den Viewport gezeichnet.
    std::unique_ptr<core::TerminalEmulator> m_emu;
    bool m_altScreen = false;
    QString m_feedCarry;  // unvollstaendige Sequenz ueber Chunk-Grenzen
    core::TerminalModes m_modes;
    QPoint m_lastMouseCell{-1, -1};     // gegen doppelte Bewegungsmeldungen
    int m_wheelAccum = 0;               // Teil-Schritte hochaufloesender Raeder

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
    QString m_fontFamily;               // aktuelle Terminal-Schrift (Einstellungen)
    int m_fontSize = 0;
    QTimer *m_blinkTimer = nullptr;
    bool m_cursorOn = true;
    void restartCursorBlink();          // nach Ausgabe/Fokus wieder sichtbar
};

} // namespace ncssh::gui
