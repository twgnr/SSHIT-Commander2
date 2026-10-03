#include "ncssh/gui/ansi.hpp"

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

// Zeilenmodell: Gearbeitet wird immer in der LETZTEN Zeile des Dokuments, an
// der Spalte m_col. Frueher begann jeder Chunk am Dokumentende und Text wurde
// EINGEFUEGT: Shells, die beim Bearbeiten mitten in der Zeile mit Rueckschritt
// bzw. Cursor-Sequenzen zurueckgehen und den Rest neu schreiben (bash/readline,
// PowerShell/PSReadLine via ConPTY), erzeugten so doppelte Zeichen.

void AnsiRenderer::placeCursor(QTextCursor &cur, bool pad)
{
    const QTextBlock block = cur.document()->lastBlock();
    const int len = block.length() - 1;
    if (pad && m_col > len) {
        cur.setPosition(block.position() + len);
        cur.insertText(QString(m_col - len, QLatin1Char(' ')), format());
    }
    cur.setPosition(block.position() + std::min(m_col, std::max(0, block.length() - 1)));
}

void AnsiRenderer::writeText(QTextCursor &cur, const QString &text)
{
    placeCursor(cur, /*pad=*/true);
    const QTextBlock block = cur.document()->lastBlock();
    const int len = block.length() - 1;
    const int over = std::min<int>(int(text.size()), std::max(0, len - m_col));
    if (over > 0)
        cur.setPosition(block.position() + m_col + over, QTextCursor::KeepAnchor);
    cur.insertText(text, format());   // ersetzt die markierten (alten) Zeichen
    m_col += int(text.size());
}

void AnsiRenderer::newline(QTextCursor &cur)
{
    cur.movePosition(QTextCursor::End);
    cur.insertText(QStringLiteral("\n"));
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
    const QTextBlock block = cur.document()->lastBlock();
    const int len = block.length() - 1;
    const ushort f = final.unicode();
    if (f == 'm') {
        applySgr(params);
    } else if (f == 'K') {  // Zeile loeschen: 0 = ab Cursor, 1 = bis Cursor, 2 = ganz
        const int mode = params.isEmpty() ? 0 : params.toInt();
        if (mode == 2) {
            cur.setPosition(block.position());
            cur.setPosition(block.position() + len, QTextCursor::KeepAnchor);
            cur.removeSelectedText();
        } else if (mode == 1) {
            const int n = std::min(m_col + 1, len);
            if (n > 0) {
                cur.setPosition(block.position());
                cur.setPosition(block.position() + n, QTextCursor::KeepAnchor);
                cur.insertText(QString(n, QLatin1Char(' ')), format());
            }
        } else if (m_col < len) {
            cur.setPosition(block.position() + m_col);
            cur.setPosition(block.position() + len, QTextCursor::KeepAnchor);
            cur.removeSelectedText();
        }
    } else if (f == 'C') {          // Cursor rechts
        m_col += num(1);
    } else if (f == 'D') {          // Cursor links
        m_col = std::max(0, m_col - num(1));
    } else if (f == 'G' || f == '`') {  // Spalte absolut (1-basiert)
        m_col = num(1) - 1;
    } else if (f == 'P') {          // Zeichen ab Cursor loeschen (Rest rueckt nach)
        if (m_col < len) {
            cur.setPosition(block.position() + m_col);
            cur.setPosition(block.position() + std::min(len, m_col + num(1)),
                            QTextCursor::KeepAnchor);
            cur.removeSelectedText();
        }
    } else if (f == '@') {          // Leerzeichen am Cursor einfuegen
        if (m_col <= len) {
            cur.setPosition(block.position() + m_col);
            cur.insertText(QString(num(1), QLatin1Char(' ')), format());
        }
    } else if (f == 'X') {          // Zeichen ab Cursor durch Leerzeichen ersetzen
        const int n = std::min(num(1), std::max(0, len - m_col));
        if (n > 0) {
            cur.setPosition(block.position() + m_col);
            cur.setPosition(block.position() + m_col + n, QTextCursor::KeepAnchor);
            cur.insertText(QString(n, QLatin1Char(' ')), format());
        }
    }
    // andere (Zeilen hoch/runter, Bildschirm loeschen) werden ignoriert — im
    // Rollpuffer-Modus gibt es nur die laufende Zeile.
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
                i = j + 1;
                continue;
            }
            i += 2;  // sonstige 2-Zeichen-Escapes verwerfen
            continue;
        }
        if (ch == QLatin1Char('\r')) {
            // Wagenruecklauf: nur an den Zeilenanfang — folgender Text
            // ueberschreibt (Fortschrittsanzeigen, Prompt-Neuzeichnen).
            m_col = 0;
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
