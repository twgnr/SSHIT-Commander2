#include "ncssh/gui/terminal_widget.hpp"

#include "ncssh/core/i18n.hpp"
#include "ncssh/gui/ansi.hpp"
#include "ncssh/gui/shell_backends.hpp"
#include "ncssh/gui/style.hpp"

#include "ncssh/gui/file_dialogs.hpp"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QFile>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTextEdit>
#include <QUrl>

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
        if (m_logFile) {
            // Mitschnitt ohne Steuerzeichen — sonst ist die Datei unlesbar.
            m_logFile->write(AnsiRenderer::stripAnsi(data).toUtf8());
            m_logFile->flush();
        }
        if (!m_searchPattern.isEmpty())
            recomputeMatches();
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
        if (event->key() == Qt::Key_F) {   // Suche im Rollpuffer
            showSearchBar();
            return;
        }
    }
    // Suchleiste offen: Esc schliesst, F3 blaettert durch die Treffer.
    if (event->key() == Qt::Key_Escape && m_searchBar && m_searchBar->isVisible()) {
        hideSearchBar();
        return;
    }
    if (event->key() == Qt::Key_F3 && !m_searchPattern.isEmpty()) {
        searchStep(!(mods & Qt::ShiftModifier));
        return;
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
    layoutSearchBar();
}

void TerminalWidget::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    QAction *copyAct = menu.addAction(_t("Kopieren"));
    copyAct->setEnabled(textCursor().hasSelection());
    QAction *pasteAct = menu.addAction(_t("Einfügen"));
    QAction *copyAllAct = menu.addAction(_t("Alles kopieren"));
    menu.addSeparator();
    QAction *searchAct = menu.addAction(_t("Im Puffer suchen (Strg+F)"));
    const QString url = urlAt(event->pos());
    QAction *urlAct = url.isEmpty() ? nullptr
                                    : menu.addAction(_t("Link öffnen: %1").arg(url));
    menu.addSeparator();
    QAction *logAct = menu.addAction(isLogging() ? _t("Mitschnitt beenden")
                                                 : _t("Mitschnitt starten …"));
    menu.addSeparator();
    QAction *clearAct = menu.addAction(_t("Leeren"));

    QAction *chosen = menu.exec(event->globalPos());
    if (chosen == copyAct)
        copy();
    else if (chosen == pasteAct)
        sendText(QApplication::clipboard()->text());
    else if (chosen == copyAllAct)
        QApplication::clipboard()->setText(toPlainText());
    else if (chosen == searchAct)
        showSearchBar();
    else if (urlAct && chosen == urlAct)
        QDesktopServices::openUrl(QUrl(url));
    else if (chosen == logAct)
        toggleLogging();
    else if (chosen == clearAct) {
        clear();
        m_renderer->reset();
        clearSearch();
    }
}

// --- Suche im Rollpuffer ----------------------------------------------------

void TerminalWidget::showSearchBar()
{
    if (!m_searchBar) {
        m_searchBar = new QWidget(this);
        m_searchBar->setAutoFillBackground(true);
        auto *row = new QHBoxLayout(m_searchBar);
        row->setContentsMargins(6, 3, 6, 3);
        m_searchEdit = new QLineEdit(m_searchBar);
        m_searchEdit->setPlaceholderText(_t("Suchen …"));
        m_searchEdit->setFixedWidth(220);
        m_searchLabel = new QLabel(m_searchBar);
        m_searchLabel->setObjectName(QStringLiteral("Muted"));
        auto *prev = new QPushButton(QStringLiteral("▲"), m_searchBar);
        auto *next = new QPushButton(QStringLiteral("▼"), m_searchBar);
        auto *close = new QPushButton(QStringLiteral("✕"), m_searchBar);
        for (QPushButton *b : {prev, next, close})
            b->setFixedWidth(26);
        row->addWidget(m_searchEdit);
        row->addWidget(m_searchLabel);
        row->addWidget(prev);
        row->addWidget(next);
        row->addWidget(close);
        connect(m_searchEdit, &QLineEdit::textChanged, this, &TerminalWidget::setSearch);
        connect(m_searchEdit, &QLineEdit::returnPressed, this, [this] { searchStep(true); });
        connect(prev, &QPushButton::clicked, this, [this] { searchStep(false); });
        connect(next, &QPushButton::clicked, this, [this] { searchStep(true); });
        connect(close, &QPushButton::clicked, this, [this] { hideSearchBar(); });
    }
    layoutSearchBar();
    m_searchBar->show();
    m_searchBar->raise();
    m_searchEdit->setFocus();
    m_searchEdit->selectAll();
}

void TerminalWidget::hideSearchBar()
{
    if (m_searchBar)
        m_searchBar->hide();
    clearSearch();
    setFocus();
}

void TerminalWidget::layoutSearchBar()
{
    if (!m_searchBar)
        return;
    m_searchBar->adjustSize();
    const int margin = 8;
    m_searchBar->move(width() - m_searchBar->width() - margin - verticalScrollBar()->width(),
                      margin);
}

void TerminalWidget::setSearch(const QString &pattern)
{
    m_searchPattern = pattern;
    m_matchIndex = -1;
    recomputeMatches();
    if (!m_matchPositions.empty())
        searchStep(true);
}

void TerminalWidget::clearSearch()
{
    m_searchPattern.clear();
    m_matchPositions.clear();
    m_matchIndex = -1;
    setExtraSelections({});
    if (m_searchLabel)
        m_searchLabel->clear();
}

void TerminalWidget::recomputeMatches()
{
    m_matchPositions.clear();
    if (m_searchPattern.isEmpty()) {
        setExtraSelections({});
        if (m_searchLabel)
            m_searchLabel->clear();
        return;
    }
    const QString haystack = toPlainText();
    int from = 0;
    while (true) {
        const int at = haystack.indexOf(m_searchPattern, from, Qt::CaseInsensitive);
        if (at < 0)
            break;
        m_matchPositions.push_back(at);
        from = at + 1;   // ueberlappende Treffer nicht verschlucken
    }
    if (m_matchIndex >= int(m_matchPositions.size()))
        m_matchIndex = m_matchPositions.empty() ? -1 : 0;
    highlightMatches();
}

void TerminalWidget::highlightMatches()
{
    QList<QTextEdit::ExtraSelection> selections;
    const QColor plain(QStringLiteral("#654a00"));
    const QColor active(QStringLiteral("#b8860b"));
    for (int i = 0; i < int(m_matchPositions.size()); ++i) {
        QTextEdit::ExtraSelection sel;
        sel.cursor = textCursor();
        sel.cursor.setPosition(m_matchPositions[size_t(i)]);
        sel.cursor.setPosition(m_matchPositions[size_t(i)] + m_searchPattern.size(),
                               QTextCursor::KeepAnchor);
        sel.format.setBackground(i == m_matchIndex ? active : plain);
        selections.append(sel);
    }
    setExtraSelections(selections);
    if (m_searchLabel)
        m_searchLabel->setText(searchStatus());
}

QString TerminalWidget::searchStatus() const
{
    if (m_searchPattern.isEmpty())
        return {};
    if (m_matchPositions.empty())
        return _t("keine Treffer");
    return QStringLiteral("%1/%2").arg(m_matchIndex + 1).arg(m_matchPositions.size());
}

void TerminalWidget::searchStep(bool forward)
{
    if (m_matchPositions.empty())
        return;
    const int count = int(m_matchPositions.size());
    m_matchIndex = m_matchIndex < 0 ? (forward ? 0 : count - 1)
                                    : (m_matchIndex + (forward ? 1 : count - 1)) % count;
    QTextCursor cursor = textCursor();
    cursor.setPosition(m_matchPositions[size_t(m_matchIndex)]);
    cursor.setPosition(m_matchPositions[size_t(m_matchIndex)] + m_searchPattern.size(),
                       QTextCursor::KeepAnchor);
    setTextCursor(cursor);
    ensureCursorVisible();
    highlightMatches();
}

// --- Mitschnitt -------------------------------------------------------------

bool TerminalWidget::startLogging(const QString &path)
{
    stopLogging();
    auto *file = new QFile(path, this);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Append)) {
        delete file;
        return false;
    }
    m_logFile = file;
    return true;
}

void TerminalWidget::stopLogging()
{
    if (!m_logFile)
        return;
    m_logFile->close();
    m_logFile->deleteLater();
    m_logFile = nullptr;
}

QString TerminalWidget::logPath() const
{
    return m_logFile ? m_logFile->fileName() : QString();
}

void TerminalWidget::toggleLogging()
{
    if (isLogging()) {
        const QString path = logPath();
        stopLogging();
        QMessageBox::information(this, _t("Mitschnitt"),
                                 _t("Mitschnitt beendet:\n%1").arg(path));
        return;
    }
    const QString path = getSaveFileName(this, _t("Mitschnitt starten"),
                                         QStringLiteral("terminal.log"),
                                         _t("Textdateien (*.log *.txt)"));
    if (path.isEmpty())
        return;
    if (!startLogging(path))
        QMessageBox::warning(this, _t("Mitschnitt"),
                             _t("Datei konnte nicht geschrieben werden: %1").arg(path));
}

// --- Links ------------------------------------------------------------------

QString TerminalWidget::urlAt(const QPoint &pos) const
{
    QTextCursor cursor = cursorForPosition(pos);
    cursor.select(QTextCursor::WordUnderCursor);
    if (!cursor.hasSelection())
        return {};
    // Wortgrenzen von Qt reichen fuer URLs nicht — die ganze Zeile absuchen.
    QTextCursor line = cursor;
    line.select(QTextCursor::LineUnderCursor);
    const QString text = line.selectedText();
    static const QRegularExpression re(QStringLiteral("(https?://|ftp://|www\\.)[^\\s\"'<>]+"));
    const int column = cursor.positionInBlock();
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        if (column >= match.capturedStart() && column <= match.capturedEnd()) {
            QString url = match.captured();
            if (url.startsWith(QLatin1String("www.")))
                url.prepend(QStringLiteral("https://"));
            return url;
        }
    }
    return {};
}

void TerminalWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    // Doppelklick auf einen Link oeffnet ihn; sonst normale Wortauswahl.
    const QString url = urlAt(event->pos());
    if (!url.isEmpty()) {
        QDesktopServices::openUrl(QUrl(url));
        return;
    }
    QPlainTextEdit::mouseDoubleClickEvent(event);
}

} // namespace ncssh::gui
