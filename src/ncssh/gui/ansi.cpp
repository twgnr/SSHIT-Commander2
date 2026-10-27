#include "ncssh/gui/ansi.hpp"

#include "ncssh/core/terminal_emulator.hpp"
#include "ncssh/gui/style.hpp"

#include <QFont>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextCursor>
#include <algorithm>

namespace ncssh::gui {

// Format-Eigenschaft: die von der Ausgabe gewuenschte Textfarbe vor der
// Lesbarkeits-Anpassung (retheme rechnet daraus fuer den neuen Hintergrund).
constexpr int kOriginalFg = QTextFormat::UserProperty + 1;

static const char *kBase16[] = {
    "#2e3436", "#cc0000", "#4e9a06", "#c4a000", "#3465a4", "#75507b", "#06989a", "#d3d7cf",
    "#555753", "#ef2929", "#8ae234", "#fce94f", "#729fcf", "#ad7fa8", "#34e2e2", "#eeeeec",
};

static QColor ansiColor(int n)
{
    n = qBound(0, n, 255);  // Riesenwerte liessen (n - 232) * 10 ueberlaufen
    if (n < 16)
        return QColor(QString::fromLatin1(kBase16[qBound(0, n, 15)]));
    if (n < 232) {
        n -= 16;
        const int r = n / 36, g = (n % 36) / 6, b = n % 6;
        const auto conv = [](int v) { return v ? 55 + v * 40 : 0; };
        return QColor(conv(r), conv(g), conv(b));
    }
    const int v = 8 + (n - 232) * 10;
    return QColor(v, v, v);
}

AnsiRenderer::AnsiRenderer(QPlainTextEdit *editor)
    : m_editor(editor), m_top(editor->document())
{
    // Der Anker steht am Blockanfang; Text, der genau dort eingefuegt wird
    // (Schreiben in Spalte 0), darf ihn nicht hinter sich schieben.
    m_top.setKeepPositionOnInsert(true);
    reset();
}

void AnsiRenderer::setScreenSize(int cols, int rows)
{
    m_cols = std::max(0, cols);
    m_rows = std::max(0, rows);
}

QString AnsiRenderer::stripAnsi(const QString &text)
{
    // CSI-Sequenzen (ESC [ ... Endbuchstabe), OSC-Sequenzen (ESC ] ... BEL/ST)
    // und einzelne ESC-Kommandos. Zusaetzlich \r, damit Zeilen nicht verschmelzen.
    static const QRegularExpression csi(
        QStringLiteral("\x1b\\[[0-9;?]*[A-Za-z]"));
    static const QRegularExpression osc(
        QStringLiteral("\x1b\\][^\x07\x1b]*(?:\x07|\x1b\\\\)"));
    static const QRegularExpression single(QStringLiteral("\x1b[()#][0-9A-Za-z]|\x1b[A-Za-z=>]"));
    QString out = text;
    out.remove(osc);
    out.remove(csi);
    out.remove(single);
    out.remove(QLatin1Char('\r'));
    return out;
}

void AnsiRenderer::retheme()
{
    const QColor oldFg(m_defFg);
    const auto [bg, fg] = terminalColors();  // WICHTIG: liefert (bg, fg)!
    m_defFg = fg;
    m_defBg = bg;
    const QColor newFg(fg), newBg(bg);
    QTextDocument *doc = m_editor->document();
    QTextCursor cur(doc);
    cur.beginEditBlock();
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            if (!frag.isValid())
                continue;
            const QTextCharFormat old = frag.charFormat();
            QColor target;
            if (old.hasProperty(kOriginalFg)) {
                const QColor original = old.property(kOriginalFg).value<QColor>();
                target = old.background().style() == Qt::NoBrush ? readableOn(original, newBg)
                                                                 : original;
            } else if (old.foreground().color() == oldFg) {
                target = newFg;   // Standardtext
            } else {
                continue;         // invertiert o. ae. — unveraendert lassen
            }
            if (target == old.foreground().color())
                continue;
            QTextCharFormat fmt;
            fmt.setForeground(target);
            cur.setPosition(frag.position());
            cur.setPosition(frag.position() + frag.length(), QTextCursor::KeepAnchor);
            cur.mergeCharFormat(fmt);
        }
    }
    cur.endEditBlock();
}

void AnsiRenderer::reset()
{
    const auto [bg, fg] = terminalColors();  // WICHTIG: liefert (bg, fg)!
    m_defFg = fg;
    m_defBg = bg;
    m_fg.reset();
    m_bg.reset();
    m_bold = m_italic = m_underline = m_reverse = false;
    m_pendingCr = false;
    // Parserzustand ebenfalls verwerfen: eine nie beendete OSC wuerde sonst
    // alle folgende Ausgabe (z.B. die Meldung "Shell beendet") verschlucken.
    m_carry.clear();
    m_inString = false;
    m_stringLen = 0;
    m_col = 0;
    m_g0Graphics = m_g1Graphics = m_shiftOut = false;
    // Neuer Bildschirm ab der letzten Zeile (neue Shell bzw. "Leeren").
    m_up = 0;
    m_top.setPosition(m_editor->document()->lastBlock().position());
    m_topRow = 0;
}

QTextCharFormat AnsiRenderer::format() const
{
    QTextCharFormat fmt;
    QColor fg = m_fg.value_or(QColor(m_defFg));
    std::optional<QColor> bg = m_bg;
    if (m_reverse) {
        const QColor newFg = bg.value_or(QColor(m_defBg));
        bg = fg;
        fg = newFg;
    }
    // Explizite Farbe merken (fuer retheme) und auf hellem Grund lesbar machen.
    if (m_fg && !m_reverse) {
        fmt.setProperty(kOriginalFg, *m_fg);
        if (!bg)
            fg = readableOn(fg, QColor(m_defBg));
    }
    fmt.setForeground(fg);
    if (bg)
        fmt.setBackground(*bg);
    if (m_bold)
        fmt.setFontWeight(QFont::Bold);
    fmt.setFontItalic(m_italic);
    fmt.setFontUnderline(m_underline);
    return fmt;
}

void AnsiRenderer::applySgr(const QString &params)
{
    QList<int> codes;
    const QStringList parts = params.isEmpty() ? QStringList{QStringLiteral("0")}
                                               : params.split(QLatin1Char(';'));
    for (const QString &p : parts)
        codes.append(p.isEmpty() ? 0 : p.toInt());

    for (int i = 0; i < codes.size(); ++i) {
        const int c = codes[i];
        if (c == 0) {
            m_fg.reset();
            m_bg.reset();
            m_bold = m_italic = m_underline = false;
        } else if (c == 1) {
            m_bold = true;
        } else if (c == 22) {
            m_bold = false;
        } else if (c == 3) {
            m_italic = true;
        } else if (c == 23) {
            m_italic = false;
        } else if (c == 4) {
            m_underline = true;
        } else if (c == 24) {
            m_underline = false;
        } else if (c == 7) {
            m_reverse = true;
        } else if (c == 27) {
            m_reverse = false;
        } else if (c >= 30 && c <= 37) {
            m_fg = ansiColor(c - 30);
        } else if (c >= 90 && c <= 97) {
            m_fg = ansiColor(c - 90 + 8);
        } else if (c == 39) {
            m_fg.reset();
        } else if (c >= 40 && c <= 47) {
            m_bg = ansiColor(c - 40);
        } else if (c >= 100 && c <= 107) {
            m_bg = ansiColor(c - 100 + 8);
        } else if (c == 49) {
            m_bg.reset();
        } else if (c == 38 || c == 48) {
            std::optional<QColor> *target = (c == 38) ? &m_fg : &m_bg;
            if (i + 2 < codes.size() && codes[i + 1] == 5) {
                *target = ansiColor(codes[i + 2]);
                i += 2;
            } else if (i + 4 < codes.size() && codes[i + 1] == 2) {
                *target = QColor(qBound(0, codes[i + 2], 255), qBound(0, codes[i + 3], 255),
                                 qBound(0, codes[i + 4], 255));
                i += 4;
            }
        }
    }
}

// Zeilenmodell: Gearbeitet wird in der Cursorzeile (m_up Zeilen ueber der
// letzten) an der Spalte m_col; Text UEBERSCHREIBT wie in einem Terminal.
// Frueher begann jeder Chunk am Dokumentende und Text wurde EINGEFUEGT:
// Shells, die beim Bearbeiten mitten in der Zeile zurueckgehen und den Rest
// neu schreiben (bash/readline, PowerShell/PSReadLine via ConPTY), erzeugten
// so doppelte Zeichen. ConPTY springt dafuer mit ABSOLUTEN Positionen
// (ESC[Zeile;SpalteH) — die brauchen das Bildschirmmodell unten, sonst wurde
// aus "cd " beim Neuzeichnen "cdcd ".
//
// Spalten sind Anzeigespalten: ein CJK-Zeichen ist EIN Zeichen im Dokument,
// belegt aber ZWEI Spalten (die Shell schickt dafuer zwei Rueckschritte).
// posForCol rechnet deshalb jede Spalte in eine Dokumentposition um.

namespace {
// Zeichenindex in `line`, an dem Spalte `col` beginnt. Kombinierende Zeichen
// direkt dahinter gehoeren noch zum vorigen Zeichen und werden uebersprungen.
// Hinter dem Zeilenende: line.size().
int posForCol(const QString &line, int col)
{
    const int n = int(line.size());
    int i = 0;
    int w = 0;
    const auto next = [&](int at, int &len) {
        char32_t c = line.at(at).unicode();
        len = 1;
        if (QChar::isHighSurrogate(c) && at + 1 < n && line.at(at + 1).isLowSurrogate()) {
            c = QChar::surrogateToUcs4(line.at(at), line.at(at + 1));
            len = 2;
        }
        return core::charWidth(c);
    };
    int len = 1;
    while (i < n && w < col) {
        w += next(i, len);
        i += len;
    }
    while (i < n && next(i, len) == 0)
        i += len;
    return i;
}
}  // namespace

QTextBlock AnsiRenderer::cursorBlock() const
{
    // Begrenzt: Zeilen kann auch jemand anderes entfernen (z. B. "Leeren").
    const QTextDocument *doc = m_editor->document();
    const int up = std::clamp(m_up, 0, doc->blockCount() - 1);
    return doc->findBlockByNumber(doc->blockCount() - 1 - up);
}

int AnsiRenderer::rowsOf(const QTextBlock &block, bool isCursorBlock) const
{
    if (m_cols <= 0)
        return 1;
    int width = core::textWidth(block.text());
    if (isCursorBlock && m_col > width)
        width = m_col + 1;   // Cursor hinter dem Text belegt seine Zeile mit
    return std::max(1, (width + m_cols - 1) / m_cols);
}

int AnsiRenderer::cursorRowInBlock(const QTextBlock &block) const
{
    if (m_cols <= 0)
        return 0;
    // Steht der Cursor genau am Ende einer vollen Zeile, bleibt er (wie im
    // Terminal, "pending wrap") noch in dieser Zeile.
    return std::min(m_col / m_cols, rowsOf(block, true) - 1);
}

void AnsiRenderer::appendLine(QTextCursor &cur)
{
    cur.movePosition(QTextCursor::End);
    cur.insertText(QStringLiteral("\n"));
}

void AnsiRenderer::normalizeScreen()
{
    if (m_rows <= 0)
        return;
    // Von unten zaehlen, bis m_rows Zeilen beisammen sind oder die bisherige
    // oberste Zeile erreicht ist — Aufwand hoechstens m_rows Bloecke.
    const QTextBlock cursor = cursorBlock();
    const QTextBlock top = m_top.block();
    int acc = 0;
    for (QTextBlock b = m_editor->document()->lastBlock(); b.isValid(); b = b.previous()) {
        const int rows = rowsOf(b, b == cursor);
        if (b == top) {
            if (acc + rows - m_topRow > m_rows)
                m_topRow = rows - (m_rows - acc);   // innerhalb des Blocks weitergerollt
            return;
        }
        if (acc + rows >= m_rows) {
            m_top.setPosition(b.position());
            m_topRow = rows - (m_rows - acc);
            return;
        }
        acc += rows;
    }
}

int AnsiRenderer::cursorScreenRow() const
{
    const QTextBlock cursor = cursorBlock();
    int row = 0;
    int skip = m_topRow;
    for (QTextBlock b = m_top.block(); b.isValid(); b = b.next()) {
        if (b == cursor)
            return row + std::max(0, cursorRowInBlock(b) - skip);
        row += rowsOf(b, false) - skip;
        skip = 0;
    }
    return 0;   // Cursor oberhalb des Bildschirms (sollte nicht vorkommen)
}

void AnsiRenderer::moveToScreenRow(QTextCursor &cur, int row, int termCol)
{
    if (m_rows > 0)
        row = std::min(row, m_rows - 1);
    row = std::max(0, row);
    const QTextBlock cursor = cursorBlock();
    QTextBlock target;
    int rowInBlock = 0;
    int remaining = row;
    int skip = m_topRow;
    for (QTextBlock b = m_top.block(); b.isValid(); b = b.next()) {
        const int avail = rowsOf(b, b == cursor) - skip;
        if (remaining < avail) {
            target = b;
            rowInBlock = skip + remaining;
            break;
        }
        remaining -= avail;
        skip = 0;
    }
    if (!target.isValid()) {
        // Unterhalb der letzten Zeile: die Bildschirmzeilen dort sind leer.
        for (int k = 0; k <= remaining; ++k)
            appendLine(cur);
        target = m_editor->document()->lastBlock();
        rowInBlock = 0;
    }
    m_up = m_editor->document()->blockCount() - 1 - target.blockNumber();
    m_col = rowInBlock * std::max(0, m_cols) + std::max(0, termCol);
}

void AnsiRenderer::eraseDisplay(QTextCursor &cur, int mode)
{
    QTextDocument *doc = m_editor->document();
    if (mode == 2) {
        // Bildschirm loeschen: der alte Inhalt bleibt im Rollpuffer, darunter
        // beginnt ein neuer, leerer Bildschirm (wie Windows Terminal). Die
        // Spalte bleibt; ConPTY setzt den Cursor danach ohnehin (ESC[H).
        if (!doc->lastBlock().text().isEmpty())
            appendLine(cur);
        m_up = 0;
        m_top.setPosition(doc->lastBlock().position());
        m_topRow = 0;
        return;
    }
    if (mode == 3) {
        // Rollpuffer loeschen (clear/cls): alles oberhalb des Bildschirms.
        const int topPos = m_top.block().position();
        if (topPos > 0) {
            cur.setPosition(0);
            cur.setPosition(topPos, QTextCursor::KeepAnchor);
            cur.removeSelectedText();
        }
        return;
    }
    const QTextBlock block = cursorBlock();
    if (mode == 0) {
        // Ab Cursor bis Bildschirmende: Zeilen darunter weg, Rest der Zeile weg.
        if (m_up > 0) {
            cur.setPosition(block.position() + block.length() - 1);
            cur.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
            cur.removeSelectedText();
            m_up = 0;
        }
        const QTextBlock b = cursorBlock();
        const QString line = b.text();
        if (m_col < core::textWidth(line)) {
            cur.setPosition(b.position() + posForCol(line, m_col));
            cur.setPosition(b.position() + int(line.size()), QTextCursor::KeepAnchor);
            cur.removeSelectedText();
        }
    } else if (mode == 1) {
        // Bildschirmanfang bis Cursor: Zeilen darueber leeren, Zeile bis Cursor
        // mit Leerzeichen ueberschreiben.
        for (QTextBlock b = m_top.block(); b.isValid() && b != block; b = b.next()) {
            if (b.length() > 1) {
                cur.setPosition(b.position());
                cur.setPosition(b.position() + b.length() - 1, QTextCursor::KeepAnchor);
                cur.removeSelectedText();
            }
        }
        const QTextBlock b = cursorBlock();
        const QString line = b.text();
        const int n = std::min(m_col + 1, core::textWidth(line));
        if (n > 0) {
            cur.setPosition(b.position());
            cur.setPosition(b.position() + posForCol(line, n), QTextCursor::KeepAnchor);
            cur.insertText(QString(n, QLatin1Char(' ')), format());
        }
    }
}

void AnsiRenderer::placeCursor(QTextCursor &cur, bool pad)
{
    QTextBlock block = cursorBlock();
    const int len = core::textWidth(block.text());
    if (pad && m_col > len) {
        cur.setPosition(block.position() + block.length() - 1);
        cur.insertText(QString(m_col - len, QLatin1Char(' ')), format());
        block = cursorBlock();
    }
    cur.setPosition(block.position() + posForCol(block.text(), m_col));
}

void AnsiRenderer::writeText(QTextCursor &cur, const QString &textIn)
{
    QString text = textIn;
    if (m_shiftOut ? m_g1Graphics : m_g0Graphics) {
        // DEC-Liniengrafik: 'q' -> '─' usw. (alle Ziele liegen in der BMP).
        for (QChar &ch : text)
            ch = QChar(static_cast<char16_t>(core::decSpecialGraphics(ch.unicode())));
    }
    placeCursor(cur, /*pad=*/true);
    const QTextBlock block = cursorBlock();
    const QString line = block.text();
    const int width = core::textWidth(text);
    const int start = posForCol(line, m_col);
    const int end = posForCol(line, m_col + width);
    cur.setPosition(block.position() + start);
    if (end > start)
        cur.setPosition(block.position() + end, QTextCursor::KeepAnchor);
    cur.insertText(text, format());   // ersetzt die markierten (alten) Zeichen
    m_col += width;
}

void AnsiRenderer::newline(QTextCursor &cur)
{
    // Zeile darunter vorhanden (Cursor wurde hochgesetzt): nur hinunter;
    // sonst neue Zeile anhaengen — der Bildschirm rollt dann ggf. mit.
    if (m_up > 0)
        --m_up;
    else
        appendLine(cur);
    m_col = 0;
}

void AnsiRenderer::handleCsi(QTextCursor &cur, const QString &params, QChar final)
{
    // Erster Zahlenparameter (Standard 1), gedeckelt gegen Unsinnswerte.
    const auto num = [&params](int def) {
        bool ok = false;
        const int v = params.section(QLatin1Char(';'), 0, 0).toInt(&ok);
        return (!ok || v <= 0) ? def : std::min(v, 9999);
    };
    const auto num2 = [&params](int def) {   // zweiter Parameter (Spalte bei H)
        bool ok = false;
        const int v = params.section(QLatin1Char(';'), 1, 1).toInt(&ok);
        return (!ok || v <= 0) ? def : std::min(v, 9999);
    };
    const QTextBlock block = cursorBlock();
    const QString line = block.text();
    const int len = core::textWidth(line);   // Zeilenlaenge in Spalten
    // Dokumentposition einer Spalte der Cursorzeile.
    const auto at = [&](int col) { return block.position() + posForCol(line, col); };
    // Erste Spalte der sichtbaren Zeile, in der der Cursor steht (Umbruch).
    const int rowStart = cursorRowInBlock(block) * std::max(0, m_cols);
    const ushort f = final.unicode();
    if (f == 'H' || f == 'f') {     // Cursor absolut (Zeile;Spalte, 1-basiert)
        normalizeScreen();
        moveToScreenRow(cur, num(1) - 1, num2(1) - 1);
        return;
    }
    if (f == 'd') {                 // Zeile absolut, Spalte bleibt
        normalizeScreen();
        moveToScreenRow(cur, num(1) - 1, m_col - rowStart);
        return;
    }
    if (f == 'A' || f == 'F') {     // hoch (F: an den Zeilenanfang)
        normalizeScreen();
        moveToScreenRow(cur, cursorScreenRow() - num(1), f == 'F' ? 0 : m_col - rowStart);
        return;
    }
    if (f == 'B' || f == 'E') {     // runter (E: an den Zeilenanfang)
        normalizeScreen();
        moveToScreenRow(cur, cursorScreenRow() + num(1), f == 'E' ? 0 : m_col - rowStart);
        return;
    }
    if (f == 'J') {                 // Bildschirm loeschen: 0 ab, 1 bis Cursor, 2 ganz, 3 Rollpuffer
        normalizeScreen();
        eraseDisplay(cur, params.isEmpty() ? 0 : params.toInt());
        return;
    }
    if (f == 'm') {
        applySgr(params);
    } else if (f == 'K') {  // Zeile loeschen: 0 = ab Cursor, 1 = bis Cursor, 2 = ganz
        const int mode = params.isEmpty() ? 0 : params.toInt();
        if (mode == 2) {
            cur.setPosition(block.position());
            cur.setPosition(block.position() + int(line.size()), QTextCursor::KeepAnchor);
            cur.removeSelectedText();
        } else if (mode == 1) {
            const int n = std::min(m_col + 1, len);
            if (n > 0) {
                cur.setPosition(block.position());
                cur.setPosition(at(n), QTextCursor::KeepAnchor);
                cur.insertText(QString(n, QLatin1Char(' ')), format());
            }
        } else if (m_col < len) {
            cur.setPosition(at(m_col));
            cur.setPosition(block.position() + int(line.size()), QTextCursor::KeepAnchor);
            cur.removeSelectedText();
        }
    } else if (f == 'C') {          // Cursor rechts
        m_col += num(1);
    } else if (f == 'D') {          // Cursor links
        m_col = std::max(0, m_col - num(1));
    } else if (f == 'G' || f == '`') {  // Spalte absolut (1-basiert) in der sichtbaren Zeile
        m_col = rowStart + num(1) - 1;
    } else if (f == 'P') {          // Zeichen ab Cursor loeschen (Rest rueckt nach)
        if (m_col < len) {
            cur.setPosition(at(m_col));
            cur.setPosition(at(std::min(len, m_col + num(1))), QTextCursor::KeepAnchor);
            cur.removeSelectedText();
        }
    } else if (f == '@') {          // Leerzeichen am Cursor einfuegen
        if (m_col <= len) {
            cur.setPosition(at(m_col));
            cur.insertText(QString(num(1), QLatin1Char(' ')), format());
        }
    } else if (f == 'X') {          // Zeichen ab Cursor durch Leerzeichen ersetzen
        const int n = std::min(num(1), std::max(0, len - m_col));
        if (n > 0) {
            cur.setPosition(at(m_col));
            cur.setPosition(at(m_col + n), QTextCursor::KeepAnchor);
            cur.insertText(QString(n, QLatin1Char(' ')), format());
        }
    }
    // andere (Rollbereiche, Zeilen einfuegen/loeschen …) werden ignoriert —
    // die kommen praktisch nur auf dem Alternate-Screen vor (TerminalEmulator).
}

void AnsiRenderer::feed(const QString &textIn)
{
    // Unvollstaendige Sequenz vom letzten Chunk voranstellen. Frueher wurde der
    // Rest eines Chunks ab einer angeschnittenen Sequenz verworfen, und ein
    // einzelnes ESC am Chunk-Ende liess die Schleife unten nie fortschreiten.
    QString text = m_carry + textIn;
    m_carry.clear();
    // Eigener Cursor (nicht der des Widgets: den verstellt z. B. eine
    // Mausmarkierung); Position kommt aus dem Zeilenmodell.
    QTextCursor cur(m_editor->document());
    placeCursor(cur, /*pad=*/false);

    const int n = text.size();
    int i = 0;

    // Zeichenkette einer OSC/DCS/APC/PM/SOS ueberspringen; Ende ist BEL oder
    // ST (ESC \). Liefert die Position danach; endet der Chunk vorher, bleibt
    // m_inString gesetzt und es geht im naechsten Chunk weiter.
    // Sicherung: eine nie abgeschlossene Zeichenkette (z. B. "ESC ]" mitten in
    // einer per cat ausgegebenen Binaerdatei) verschluckte sonst ALLE folgende
    // Ausgabe. Nach 4096 Zeichen gilt sie als kaputt und endet; CAN/SUB
    // brechen sie wie bei echten Terminals ab.
    constexpr int kMaxStringLen = 4096;
    const auto skipString = [&](int from) -> int {
        for (int k = from; k < n; ++k) {
            const ushort u = text.at(k).unicode();
            if (u == 0x07) {
                m_inString = false;
                return k + 1;
            }
            if (u == 0x18 || u == 0x1a) {
                m_inString = false;
                return k + 1;
            }
            if (++m_stringLen > kMaxStringLen) {
                m_inString = false;
                return k;
            }
            if (u == 0x1b) {
                if (k + 1 >= n) {  // ESC am Chunk-Ende: ST erst im naechsten Chunk
                    m_carry = QStringLiteral("\x1b");
                    return n;
                }
                m_inString = false;
                // ESC \ ist ST; jedes andere ESC bricht die Zeichenkette ab und
                // beginnt eine neue Sequenz (wird von der Hauptschleife gelesen).
                return text.at(k + 1) == QLatin1Char('\\') ? k + 2 : k;
            }
        }
        return n;
    };
    if (m_inString)
        i = skipString(0);

    while (i < n) {
        const QChar ch = text.at(i);
        if (ch == QChar(0x1b)) {
            if (i + 1 >= n) {  // einzelnes ESC am Chunk-Ende -> auf Rest warten
                m_carry = text.mid(i);
                break;
            }
            const QChar nxt = text.at(i + 1);
            if (nxt == QLatin1Char('[')) {
                int j = i + 2;
                while (j < n && !(text.at(j).unicode() >= 0x40 && text.at(j).unicode() <= 0x7e))
                    ++j;
                if (j >= n) {
                    // Angeschnittene CSI zuruecklegen; ueberlange (kaputte)
                    // Sequenzen verwerfen statt unbegrenzt zu puffern.
                    if (n - i <= 256)
                        m_carry = text.mid(i);
                    break;
                }
                handleCsi(cur, text.mid(i + 2, j - (i + 2)), text.at(j));
                i = j + 1;
                continue;
            }
            const ushort nu = nxt.unicode();
            if (nu == ']' || nu == 'P' || nu == '_' || nu == '^' || nu == 'X') {
                // OSC (Titel), DCS, APC, PM, SOS -> bis BEL bzw. ST ueberspringen
                m_inString = true;
                m_stringLen = 0;
                i = skipString(i + 2);
                continue;
            }
            if (nu >= 0x20 && nu <= 0x2f) {
                // ESC + Zwischenbyte + Endbyte, z.B. ESC ( B (Zeichensatz, kommt
                // von 'tput sgr0') — sonst bliebe ein einzelnes "B" stehen.
                int j = i + 1;
                while (j < n && text.at(j).unicode() >= 0x20 && text.at(j).unicode() <= 0x2f)
                    ++j;
                if (j >= n) {
                    if (n - i <= 16)
                        m_carry = text.mid(i);
                    break;
                }
                // Zeichensatz fuer G0 bzw. G1: "0" = DEC-Liniengrafik, sonst ASCII.
                if (j == i + 2 && nu == '(')
                    m_g0Graphics = text.at(j) == QLatin1Char('0');
                else if (j == i + 2 && nu == ')')
                    m_g1Graphics = text.at(j) == QLatin1Char('0');
                i = j + 1;
                continue;
            }
            i += 2;  // sonstige 2-Zeichen-Escapes verwerfen
            continue;
        }
        if (ch == QLatin1Char('\r')) {
            // Wagenruecklauf: an den Anfang der sichtbaren Zeile — folgender
            // Text ueberschreibt (Fortschrittsanzeigen, Prompt-Neuzeichnen).
            m_col = cursorRowInBlock(cursorBlock()) * std::max(0, m_cols);
            ++i;
            continue;
        }
        if (ch == QLatin1Char('\n')) {
            newline(cur);
            ++i;
            continue;
        }
        if (ch == QChar(0x08) || ch == QChar(0x07)) {
            if (ch == QChar(0x08))
                m_col = std::max(0, m_col - 1);   // Rueckschritt: nur Cursor
            ++i;
            continue;
        }
        if (ch == QChar(0x0e) || ch == QChar(0x0f)) {  // SO/SI: G1 bzw. G0 waehlen
            m_shiftOut = ch == QChar(0x0e);
            ++i;
            continue;
        }
        int j = i;
        while (j < n) {
            const QChar c = text.at(j);
            if (c == QChar(0x1b) || c == QLatin1Char('\r') || c == QLatin1Char('\n')
                || c == QChar(0x08) || c == QChar(0x07) || c == QChar(0x0e)
                || c == QChar(0x0f))
                break;
            ++j;
        }
        // Ausgabe ohne Zeilenumbruch (z.B. cat einer Binaerdatei) liess sonst
        // einen einzigen Block unbegrenzt wachsen — maximumBlockCount greift
        // dann nicht, und QPlainTextEdit wird quaelend langsam. Ab
        // kMaxBlockLen Zeichen wird hart umgebrochen.
        constexpr int kMaxBlockLen = 8192;
        int from = i;
        while (from < j) {
            int room = kMaxBlockLen - m_col;
            if (room <= 0) {
                newline(cur);
                continue;
            }
            room = std::min(room, j - from);
            // Surrogatpaar nicht zerschneiden.
            if (from + room < j && room > 1 && text.at(from + room - 1).isHighSurrogate())
                --room;
            writeText(cur, text.mid(from, room));
            from += room;
        }
        i = j;
    }
    placeCursor(cur, /*pad=*/false);
    m_editor->setTextCursor(cur);
    m_editor->ensureCursorVisible();
}

} // namespace ncssh::gui
