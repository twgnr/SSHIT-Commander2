#include "ncssh/gui/terminal_widget.hpp"

#include "ncssh/core/i18n.hpp"
#include "ncssh/core/settings.hpp"
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
#include <QPainter>
#include <QTextCursor>
#include <QTimer>
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
    setCursorWidth(0);              // Standard-Cursor aus — wir zeichnen selbst
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(core::getSettingInt(QStringLiteral("terminal_font_size"), 10));
    setFont(mono);
    applyThemeColors();
    m_renderer = std::make_unique<AnsiRenderer>(this);

    // Cursor-Blinken (~530 ms, wie ein Terminal).
    m_blinkTimer = new QTimer(this);
    m_blinkTimer->setInterval(530);
    connect(m_blinkTimer, &QTimer::timeout, this, [this] {
        m_cursorOn = !m_cursorOn;
        viewport()->update();
    });
    m_blinkTimer->start();
}

TerminalWidget::~TerminalWidget()
{
    stop();
}

void TerminalWidget::applyThemeColors()
{
    const auto [bg, fg] = terminalColors();
    m_termBg = QColor(bg);
    m_termFg = QColor(fg);
    setStyleSheet(QStringLiteral("QPlainTextEdit { background: %1; color: %2; "
                                 "border: 1px solid #2e3340; border-radius: 8px; padding: 4px; }")
                      .arg(bg, fg));
}

void TerminalWidget::restartCursorBlink()
{
    // Nach Ausgabe oder Fokus soll der Cursor sofort sichtbar sein und der
    // Blink-Rhythmus neu beginnen.
    m_cursorOn = true;
    if (m_blinkTimer)
        m_blinkTimer->start();
    viewport()->update();
}

void TerminalWidget::focusInEvent(QFocusEvent *event)
{
    QPlainTextEdit::focusInEvent(event);
    restartCursorBlink();
}

void TerminalWidget::focusOutEvent(QFocusEvent *event)
{
    QPlainTextEdit::focusOutEvent(event);
    if (m_blinkTimer)
        m_blinkTimer->stop();   // unfokussiert: nicht blinken (Rahmen statt Block)
    viewport()->update();
}

void TerminalWidget::paintEvent(QPaintEvent *event)
{
    if (m_altScreen && m_emu) {
        paintEmulator();  // Vollbild-Anwendung: Zellengitter statt Zeilenpuffer
        return;
    }
    QPlainTextEdit::paintEvent(event);

    // Schmalen Linien-Cursor ("|") an der aktuellen Position zeichnen.
    // cursorRect() liefert die Stelle in Viewport-Koordinaten, an die der
    // ANSI-Renderer den Textcursor gesetzt hat (Shell-Prompt bzw. nach \r/\b).
    if (hasFocus() && !m_cursorOn)
        return;   // Blink-Aus-Phase (nur bei Fokus geblinkt)

    const QRect cr = cursorRect();
    QPainter p(viewport());
    p.fillRect(QRect(cr.left(), cr.top(), 2, cr.height()), m_termFg);
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
        feedOutput(data);
        if (!m_altScreen)
            verticalScrollBar()->setValue(verticalScrollBar()->maximum());
        restartCursorBlink();   // nach Ausgabe Cursor sofort an neuer Stelle zeigen
        if (m_logFile) {
            // Mitschnitt ohne Steuerzeichen — sonst ist die Datei unlesbar.
            m_logFile->write(AnsiRenderer::stripAnsi(data).toUtf8());
            m_logFile->flush();
        }
        if (!m_altScreen && !m_searchPattern.isEmpty())
            recomputeMatches();
    });
    connect(backend, &ShellBackend::closed, this, [this] {
        m_altScreen = false;    // etwaigen Alt-Screen verlassen, damit die Meldung sichtbar ist
        m_feedCarry.clear();
        m_renderer->feed(QStringLiteral("\r\n\x1b[90m[Shell beendet]\x1b[0m\r\n"));
        viewport()->update();
        emit shellClosed();
    });
}

namespace {
// Sucht ab `start` die naechste Alternate-Screen-Umschaltung
// (CSI ? … h|l mit Parameter 47/1047/1049). Rueckgabe:
//   idx  >= 0 : Position der Umschaltsequenz (seqLen = Laenge)
//   idx  == -1: keine gefunden — der Rest ist normaler Text
//   idx  == -2: am Ende steht eine unvollstaendige Sequenz; `seqLen` ist der
//               Offset des dazugehoerigen ESC (ab dort zuruecklegen)
struct AltScan {
    int idx = -1;
    int seqLen = 0;
    bool enter = false;
    bool exit = false;
};
AltScan scanAltTransition(const QString &s, int start)
{
    const int n = s.size();
    int i = start;
    while (i < n) {
        if (s.at(i).unicode() != 0x1b) {
            ++i;
            continue;
        }
        const int esc = i;
        if (esc + 1 >= n)
            return {-2, esc, false, false};  // einzelnes ESC am Ende -> warten
        if (s.at(esc + 1) != QLatin1Char('[')) {
            i = esc + 1;  // anderer Escape (Charset o.ae.) -> nicht unsere Umschaltung
            continue;
        }
        // CSI-Parameter/Zwischenbytes bis zum Endbuchstaben sammeln.
        int j = esc + 2;
        while (j < n) {
            const ushort u = s.at(j).unicode();
            if (u >= 0x40 && u <= 0x7e)
                break;  // Endbuchstabe
            ++j;
        }
        if (j >= n) {
            // Unvollstaendige CSI am Ende. Kurz genug -> zuruecklegen und warten.
            if (n - esc <= 32)
                return {-2, esc, false, false};
            i = esc + 1;  // ueberlang/kaputt: nicht ewig puffern
            continue;
        }
        const QChar fin = s.at(j);
        const QString paramStr = s.mid(esc + 2, j - (esc + 2));
        if (paramStr.startsWith(QLatin1Char('?'))
            && (fin == QLatin1Char('h') || fin == QLatin1Char('l'))) {
            bool isAlt = false;
            for (const QString &p : paramStr.mid(1).split(QLatin1Char(';'))) {
                if (p == QLatin1String("47") || p == QLatin1String("1047")
                    || p == QLatin1String("1049")) {
                    isAlt = true;
                    break;
                }
            }
            if (isAlt)
                return {esc, j - esc + 1, fin == QLatin1Char('h'), fin == QLatin1Char('l')};
        }
        i = j + 1;  // andere CSI -> ueberspringen, weitersuchen
    }
    return {-1, 0, false, false};
}
}  // namespace

void TerminalWidget::feedOutput(const QString &dataIn)
{
    QString data = m_feedCarry + dataIn;
    m_feedCarry.clear();

    const auto emitChunk = [this](const QString &chunk) {
        if (chunk.isEmpty())
            return;
        if (m_altScreen && m_emu)
            m_emu->feed(chunk);
        else
            m_renderer->feed(chunk);
    };

    int i = 0;
    const int n = data.size();
    while (i < n) {
        const AltScan scan = scanAltTransition(data, i);
        if (scan.idx == -2) {  // unvollstaendige Sequenz am Ende -> zuruecklegen
            emitChunk(data.mid(i, scan.seqLen - i));
            m_feedCarry = data.mid(scan.seqLen);
            break;
        }
        if (scan.idx < 0) {
            emitChunk(data.mid(i));
            break;
        }
        emitChunk(data.mid(i, scan.idx - i));
        const QString seq = data.mid(scan.idx, scan.seqLen);
        if (scan.enter) {
            if (!m_emu)
                m_emu = std::make_unique<core::TerminalEmulator>(columns(), rows());
            else
                m_emu->resize(columns(), rows());
            m_altScreen = true;
            setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            m_emu->feed(seq);  // verarbeitet 1049h (leert den Schirm)
        } else if (scan.exit) {
            if (m_emu)
                m_emu->feed(seq);
            m_altScreen = false;
            setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        }
        i = scan.idx + scan.seqLen;
    }
    if (m_altScreen)
        viewport()->update();
}

void TerminalWidget::paintEmulator()
{
    QPainter p(viewport());
    const QFontMetrics fm(font());
    const int cw = qMax(1, fm.horizontalAdvance(QLatin1Char('M')));
    const int chh = qMax(1, fm.height());
    const int ascent = fm.ascent();
    const int ox = 4, oy = 2;

    p.fillRect(viewport()->rect(), m_termBg);
    const int rows = m_emu->rows();
    const int cols = m_emu->cols();
    const auto effColors = [this](const core::TermCell &cell, QColor &fg, QColor &bg) {
        fg = cell.fg.isValid() ? cell.fg : m_termFg;
        bg = cell.bg.isValid() ? cell.bg : m_termBg;
        if (cell.attrs & core::AttrInverse)
            std::swap(fg, bg);
    };

    for (int r = 0; r < rows; ++r) {
        int c = 0;
        while (c < cols) {
            const core::TermCell &first = m_emu->cell(r, c);
            QColor fg, bg;
            effColors(first, fg, bg);
            // Lauf gleicher Farbe/Attribute zusammenfassen (weniger drawText-Aufrufe).
            int c2 = c + 1;
            while (c2 < cols) {
                const core::TermCell &nx = m_emu->cell(r, c2);
                QColor f2, b2;
                effColors(nx, f2, b2);
                if (f2 != fg || b2 != bg || nx.attrs != first.attrs)
                    break;
                ++c2;
            }
            const int x = ox + c * cw;
            const int y = oy + r * chh;
            if (bg != m_termBg)
                p.fillRect(x, y, (c2 - c) * cw, chh, bg);
            QString run;
            for (int k = c; k < c2; ++k)
                run += QChar(static_cast<char16_t>(m_emu->cell(r, k).ch));
            QFont f = font();
            f.setBold(first.attrs & core::AttrBold);
            f.setItalic(first.attrs & core::AttrItalic);
            f.setUnderline(first.attrs & core::AttrUnderline);
            p.setFont(f);
            p.setPen(fg);
            p.drawText(x, y + ascent, run);
            c = c2;
        }
    }

    // Cursor: bei Fokus schmale Linie (nur in der Blink-An-Phase), sonst Rahmen.
    if (m_emu->cursorVisible()) {
        const int cxp = ox + m_emu->cursorCol() * cw;
        const int cyp = oy + m_emu->cursorRow() * chh;
        if (hasFocus()) {
            if (m_cursorOn)
                p.fillRect(cxp, cyp, 2, chh, m_termFg);
        } else {
            p.setPen(m_termFg);
            p.drawRect(cxp, cyp, cw - 1, chh - 1);
        }
    }
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
    // close() beendet und joint die Lesethreads SYNCHRON — deleteLater raeumt
    // danach nur noch das QObject auf. Beim App-Ende wird das Event ggf. nie
    // zugestellt (keine Ereignisschleife mehr); das ist unkritisch, weil dann
    // keine Threads mehr laufen.
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
    // Alternative Kopieren/Einfuegen wie in vielen Terminals: Strg+Einfg /
    // Shift+Einfg. Muss vor der Insert-Escape-Sequenz unten stehen.
    if ((mods & Qt::ControlModifier) && event->key() == Qt::Key_Insert) {
        copy();
        return;
    }
    if ((mods & Qt::ShiftModifier) && event->key() == Qt::Key_Insert) {
        sendText(QApplication::clipboard()->text());
        return;
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

    // Cursortasten: Vollbild-Anwendungen (vim, less) schalten oft in den
    // Anwendungs-Cursor-Modus (DECCKM), dann erwarten sie SS3 (ESC O x) statt
    // CSI (ESC [ x). Der Emulator kennt den aktuellen Modus.
    const bool appKeys = m_altScreen && m_emu && m_emu->applicationCursorKeys();
    const QChar introFinal = appKeys ? QLatin1Char('O') : QLatin1Char('[');
    const auto cursorSeq = [&](QChar letter) {
        return QStringLiteral("\x1b") + introFinal + letter;
    };

    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:   sendText(QStringLiteral("\r")); return;
    case Qt::Key_Backspace: sendText(QStringLiteral("\x7f")); return;
    case Qt::Key_Tab:     sendText(QStringLiteral("\t")); return;
    case Qt::Key_Backtab: sendText(QStringLiteral("\x1b[Z")); return;  // Shift+Tab
    case Qt::Key_Escape:  sendText(QStringLiteral("\x1b")); return;
    case Qt::Key_Up:      sendText(cursorSeq(QLatin1Char('A'))); return;
    case Qt::Key_Down:    sendText(cursorSeq(QLatin1Char('B'))); return;
    case Qt::Key_Right:   sendText(cursorSeq(QLatin1Char('C'))); return;
    case Qt::Key_Left:    sendText(cursorSeq(QLatin1Char('D'))); return;
    case Qt::Key_Home:    sendText(cursorSeq(QLatin1Char('H'))); return;
    case Qt::Key_End:     sendText(cursorSeq(QLatin1Char('F'))); return;
    case Qt::Key_Delete:  sendText(QStringLiteral("\x1b[3~")); return;
    case Qt::Key_Insert:  sendText(QStringLiteral("\x1b[2~")); return;
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
    if (m_altScreen && m_emu) {
        m_emu->resize(columns(), rows());
        viewport()->update();
    }
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
        QApplication::clipboard()->setText((m_altScreen && m_emu) ? m_emu->screenText()
                                                                  : toPlainText());
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
