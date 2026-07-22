#include "ncssh/gui/terminal_widget.hpp"

#include "ncssh/core/i18n.hpp"
#include "ncssh/gui/ansi.hpp"
#include "ncssh/gui/shell_backends.hpp"
#include "ncssh/gui/style.hpp"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMenu>
#include <QResizeEvent>
#include <QScrollBar>

namespace ncssh::gui {

using core::_t;

TerminalWidget::TerminalWidget(AsyncBridge *bridge, QWidget *parent)
    : QPlainTextEdit(parent), m_bridge(bridge)
{
    setReadOnly(true);              // Eingabe geht ueber keyPressEvent an die Shell
    setUndoRedoEnabled(false);
    setMaximumBlockCount(10000);    // Scrollback
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setCursorWidth(8);
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(10);
    setFont(mono);
    applyThemeColors();
    m_renderer = std::make_unique<AnsiRenderer>(this);
}

TerminalWidget::~TerminalWidget()
{
    stop();
}

void TerminalWidget::applyThemeColors()
{
    const auto [bg, fg] = terminalColors();
    setStyleSheet(QStringLiteral("QPlainTextEdit { background: %1; color: %2; "
                                 "border: 1px solid #2e3340; border-radius: 8px; padding: 4px; }")
                      .arg(bg, fg));
}

int TerminalWidget::columns() const
{
    const QFontMetrics fm(font());
    const int charWidth = qMax(1, fm.horizontalAdvance(QLatin1Char('M')));
    return qMax(20, (viewport()->width() - 8) / charWidth);
}

int TerminalWidget::rows() const
{
    const QFontMetrics fm(font());
    const int lineHeight = qMax(1, fm.height());
    return qMax(5, (viewport()->height() - 8) / lineHeight);
}

void TerminalWidget::attachBackend(ShellBackend *backend)
{
    stop();
    m_backend = backend;
    connect(backend, &ShellBackend::dataReceived, this, [this](const QString &data) {
        m_renderer->feed(data);
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    });
    connect(backend, &ShellBackend::closed, this, [this] {
        m_renderer->feed(QStringLiteral("\r\n\x1b[90m[Shell beendet]\x1b[0m\r\n"));
        emit shellClosed();
    });
}

void TerminalWidget::startLocal()
{
    auto *backend = new LocalShellBackend(this);
    attachBackend(backend);
    backend->start(columns(), rows());
}

void TerminalWidget::startRemote(const net::SSHSessionPtr &session)
{
    if (!session)
        return;
    auto *backend = new RemoteShellBackend(m_bridge, this);
    attachBackend(backend);
    backend->start(session, columns(), rows());
}

void TerminalWidget::stop()
{
    if (!m_backend)
        return;
    m_backend->close();
    m_backend->deleteLater();
    m_backend = nullptr;
}

void TerminalWidget::sendText(const QString &text)
{
    if (m_backend)
        m_backend->write(text);
}

void TerminalWidget::keyPressEvent(QKeyEvent *event)
{
    if (!m_backend) {
        QPlainTextEdit::keyPressEvent(event);
        return;
    }
    const Qt::KeyboardModifiers mods = event->modifiers();

    // Kopieren/Einfuegen im Terminal: Strg+Shift+C / Strg+Shift+V
    if ((mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier)) {
        if (event->key() == Qt::Key_C) {
            copy();
            return;
        }
        if (event->key() == Qt::Key_V) {
            sendText(QApplication::clipboard()->text());
            return;
        }
    }
    // Scrollback: Shift+Bild auf/ab
    if ((mods & Qt::ShiftModifier)
        && (event->key() == Qt::Key_PageUp || event->key() == Qt::Key_PageDown)) {
        QPlainTextEdit::keyPressEvent(event);
        return;
    }

    // Strg+<Buchstabe> -> Steuerzeichen (Strg+C = 0x03 usw.)
    if ((mods & Qt::ControlModifier) && !(mods & Qt::ShiftModifier)
        && event->key() >= Qt::Key_A && event->key() <= Qt::Key_Z) {
        const char code = static_cast<char>(event->key() - Qt::Key_A + 1);
        sendText(QString(QChar::fromLatin1(code)));
        return;
    }

    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:   sendText(QStringLiteral("\r")); return;
    case Qt::Key_Backspace: sendText(QStringLiteral("\x7f")); return;
    case Qt::Key_Tab:     sendText(QStringLiteral("\t")); return;
    case Qt::Key_Escape:  sendText(QStringLiteral("\x1b")); return;
    case Qt::Key_Up:      sendText(QStringLiteral("\x1b[A")); return;
    case Qt::Key_Down:    sendText(QStringLiteral("\x1b[B")); return;
    case Qt::Key_Right:   sendText(QStringLiteral("\x1b[C")); return;
    case Qt::Key_Left:    sendText(QStringLiteral("\x1b[D")); return;
    case Qt::Key_Home:    sendText(QStringLiteral("\x1b[H")); return;
    case Qt::Key_End:     sendText(QStringLiteral("\x1b[F")); return;
    case Qt::Key_Delete:  sendText(QStringLiteral("\x1b[3~")); return;
    case Qt::Key_PageUp:  sendText(QStringLiteral("\x1b[5~")); return;
    case Qt::Key_PageDown: sendText(QStringLiteral("\x1b[6~")); return;
    default: break;
    }
    if (!event->text().isEmpty())
        sendText(event->text());
}

void TerminalWidget::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    if (m_backend)
        m_backend->resize(columns(), rows());
}

void TerminalWidget::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    QAction *copyAct = menu.addAction(_t("Kopieren"));
    copyAct->setEnabled(textCursor().hasSelection());
    QAction *pasteAct = menu.addAction(_t("Einfügen"));
    menu.addSeparator();
    QAction *clearAct = menu.addAction(_t("Leeren"));
    QAction *chosen = menu.exec(event->globalPos());
    if (chosen == copyAct)
        copy();
    else if (chosen == pasteAct)
        sendText(QApplication::clipboard()->text());
    else if (chosen == clearAct) {
        clear();
        m_renderer->reset();
    }
}

} // namespace ncssh::gui
