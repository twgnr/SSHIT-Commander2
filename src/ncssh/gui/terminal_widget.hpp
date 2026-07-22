// Interaktives Terminal: echter PTY-Shell-Channel (lokal via ConPTY, remote via
// SSH) mit ANSI-Farben, Scrollback, Kopieren/Einfuegen.
// (Port von gui/terminal_widget.py, zusammengefasst)
#pragma once

#include "ncssh/gui/bridge.hpp"
#include "ncssh/net/ssh.hpp"

#include <QPlainTextEdit>
#include <memory>
#include <vector>

class QLineEdit;
class QLabel;
class QFile;

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

private:
    void applyThemeColors();
    void attachBackend(ShellBackend *backend);
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

    QString m_searchPattern;
    std::vector<int> m_matchPositions;  // Zeichen-Offsets der Treffer
    int m_matchIndex = -1;
    QWidget *m_searchBar = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QLabel *m_searchLabel = nullptr;

    QFile *m_logFile = nullptr;         // Qt-Parent = this
};

} // namespace ncssh::gui
