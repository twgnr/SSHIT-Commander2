#include "ncssh/gui/ansi.hpp"

#include "ncssh/gui/style.hpp"

#include <QFont>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <algorithm>

namespace ncssh::gui {

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

AnsiRenderer::AnsiRenderer(QPlainTextEdit *editor) : m_editor(editor)
{
    reset();
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

void AnsiRenderer::clearLine(QTextCursor &cur)
{
    cur.movePosition(QTextCursor::StartOfBlock);
    cur.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    cur.removeSelectedText();
}

void AnsiRenderer::newline(QTextCursor &cur)
{
    cur.movePosition(QTextCursor::End);
    cur.insertText(QStringLiteral("\n"));
}

void AnsiRenderer::handleCsi(QTextCursor &cur, const QString &params, QChar final)
{
    if (final == QLatin1Char('m')) {
        applySgr(params);
    } else if (final == QLatin1Char('K')) {  // Zeile loeschen (ab Cursor)
        cur.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        cur.removeSelectedText();
    }
    // andere (Cursorbewegung, Bildschirm loeschen) werden ignoriert
}

void AnsiRenderer::feed(const QString &textIn)
{
    // Unvollstaendige Sequenz vom letzten Chunk voranstellen. Frueher wurde der
    // Rest eines Chunks ab einer angeschnittenen Sequenz verworfen, und ein
    // einzelnes ESC am Chunk-Ende liess die Schleife unten nie fortschreiten.
    QString text = m_carry + textIn;
    m_carry.clear();
    QTextCursor cur = m_editor->textCursor();
    cur.movePosition(QTextCursor::End);

    if (m_pendingCr) {  // \r vom letzten Chunk
        m_pendingCr = false;
        if (text.startsWith(QLatin1Char('\n'))) {
            newline(cur);
            text.remove(0, 1);
        } else {
            clearLine(cur);
        }
    }

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
                i = j + 1;
                continue;
            }
            i += 2;  // sonstige 2-Zeichen-Escapes verwerfen
            continue;
        }
        if (ch == QLatin1Char('\r')) {
            if (i == n - 1) {  // \r am Ende -> auf naechsten Chunk warten (\r\n?)
                m_pendingCr = true;
                ++i;
                continue;
            }
            if (text.at(i + 1) == QLatin1Char('\n')) {  // CRLF -> ein Zeilenumbruch
                newline(cur);
                i += 2;
                continue;
            }
            clearLine(cur);  // einzelnes \r -> Zeile ueberschreiben
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
                cur.movePosition(QTextCursor::Left);
            ++i;
            continue;
        }
        int j = i;
        while (j < n) {
            const QChar c = text.at(j);
            if (c == QChar(0x1b) || c == QLatin1Char('\r') || c == QLatin1Char('\n')
                || c == QChar(0x08) || c == QChar(0x07))
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
            int room = kMaxBlockLen - (cur.block().length() - 1);
            if (room <= 0) {
                newline(cur);
                continue;
            }
            room = std::min(room, j - from);
            // Surrogatpaar nicht zerschneiden.
            if (from + room < j && room > 1 && text.at(from + room - 1).isHighSurrogate())
                --room;
            cur.insertText(text.mid(from, room), format());
            from += room;
        }
        i = j;
    }
    m_editor->setTextCursor(cur);
    m_editor->ensureCursorVisible();
}

} // namespace ncssh::gui
