#include "ncssh/core/terminal_emulator.hpp"

#include <QChar>
#include <algorithm>

namespace ncssh::core {

// 256-Farben-Palette wie im AnsiRenderer (16 Basis + 6x6x6 Wuerfel + Graustufen).
static const char *kBase16[] = {
    "#2e3436", "#cc0000", "#4e9a06", "#c4a000", "#3465a4", "#75507b", "#06989a", "#d3d7cf",
    "#555753", "#ef2929", "#8ae234", "#fce94f", "#729fcf", "#ad7fa8", "#34e2e2", "#eeeeec",
};

static QColor ansi256(int n)
{
    if (n < 16)
        return QColor(QString::fromLatin1(kBase16[std::clamp(n, 0, 15)]));
    if (n < 232) {
        n -= 16;
        const int r = n / 36, g = (n % 36) / 6, b = n % 6;
        const auto conv = [](int v) { return v ? 55 + v * 40 : 0; };
        return QColor(conv(r), conv(g), conv(b));
    }
    const int v = 8 + (n - 232) * 10;
    return QColor(v, v, v);
}

TerminalEmulator::TerminalEmulator(int cols, int rows)
    : m_cols(std::max(1, cols)), m_rows(std::max(1, rows))
{
    reset();
}

void TerminalEmulator::reset()
{
    m_grid.assign(m_rows, std::vector<TermCell>(m_cols, TermCell{}));
    m_cx = m_cy = 0;
    m_top = 0;
    m_bottom = m_rows - 1;
    m_wrapPending = false;
    m_attrs = 0;
    m_fg = QColor();
    m_bg = QColor();
    m_savedCx = m_savedCy = 0;
    m_savedAttrs = 0;
    m_savedFg = m_savedBg = QColor();
    m_wrap = true;
    m_originMode = false;
    m_cursorVisible = true;
    m_appCursor = false;
    m_altActive = false;
    m_savedPrimary.clear();
    m_state = State::Ground;
    m_paramBuf.clear();
    m_oscBuf.clear();
    m_highSurrogate = 0;
}

void TerminalEmulator::resize(int cols, int rows)
{
    cols = std::max(1, cols);
    rows = std::max(1, rows);
    if (cols == m_cols && rows == m_rows)
        return;
    std::vector<std::vector<TermCell>> ng(rows, std::vector<TermCell>(cols, TermCell{}));
    for (int r = 0; r < std::min(rows, m_rows); ++r)
        for (int c = 0; c < std::min(cols, m_cols); ++c)
            ng[r][c] = m_grid[r][c];
    m_grid = std::move(ng);
    m_cols = cols;
    m_rows = rows;
    m_top = 0;
    m_bottom = m_rows - 1;
    m_wrapPending = false;
    clampCursor();
}

const TermCell &TerminalEmulator::cell(int row, int col) const
{
    static const TermCell blank{};
    if (row < 0 || row >= m_rows || col < 0 || col >= m_cols)
        return blank;
    return m_grid[row][col];
}

TermCell TerminalEmulator::blankCell() const
{
    TermCell c;
    c.ch = U' ';
    c.attrs = 0;
    c.fg = QColor();
    c.bg = m_bg;  // Background-Color-Erase: aktueller Hintergrund
    return c;
}

void TerminalEmulator::clampCursor()
{
    m_cx = std::clamp(m_cx, 0, m_cols - 1);
    m_cy = std::clamp(m_cy, 0, m_rows - 1);
}

// --- Parser -----------------------------------------------------------------

void TerminalEmulator::feed(const QString &data)
{
    for (int i = 0; i < data.size(); ++i) {
        const QChar qc = data.at(i);
        const ushort u = qc.unicode();

        switch (m_state) {
        case State::Ground:
            if (u == 0x1b) {
                m_state = State::Esc;
            } else if (u < 0x20 || u == 0x7f) {
                execC0(static_cast<char>(u));
            } else if (qc.isHighSurrogate()) {
                m_highSurrogate = u;
            } else if (qc.isLowSurrogate() && m_highSurrogate) {
                putCodepoint(QChar::surrogateToUcs4(static_cast<ushort>(m_highSurrogate), u));
                m_highSurrogate = 0;
            } else {
                putCodepoint(u);
            }
            break;

        case State::Esc:
            escDispatch(qc);
            break;

        case State::EscInter:
            // Zweites Byte einer ESC-(-/#-Sequenz (Charset/DECALN) — verworfen.
            m_state = State::Ground;
            break;

        case State::Csi:
            if (u >= 0x30 && u <= 0x3f) {
                m_paramBuf += qc;  // Parameter-/Privat-Bytes
            } else if (u >= 0x20 && u <= 0x2f) {
                m_paramBuf += qc;  // Zwischenbytes (meist ignoriert)
            } else if (u >= 0x40 && u <= 0x7e) {
                csiDispatch(qc);
                m_state = State::Ground;
            } else if (u < 0x20) {
                execC0(static_cast<char>(u));  // C0 wirkt auch mitten in CSI
            } else {
                m_state = State::Ground;
            }
            break;

        case State::Osc:
            if (u == 0x07) {  // BEL beendet OSC
                if (m_oscBuf.startsWith(QStringLiteral("0;"))
                    || m_oscBuf.startsWith(QStringLiteral("2;")))
                    m_title = m_oscBuf.mid(2);
                m_state = State::Ground;
            } else if (u == 0x1b) {  // ST (ESC \) beendet OSC
                if (m_oscBuf.startsWith(QStringLiteral("0;"))
                    || m_oscBuf.startsWith(QStringLiteral("2;")))
                    m_title = m_oscBuf.mid(2);
                m_state = State::Esc;  // folgendes '\' wird als ST verworfen
            } else if (m_oscBuf.size() < 2048) {
                m_oscBuf += qc;
            }
            break;
        }
    }
}

void TerminalEmulator::execC0(char c)
{
    switch (c) {
    case '\a':  // BEL
        break;
    case '\b':  // BS
        if (m_cx > 0)
            --m_cx;
        m_wrapPending = false;
        break;
    case '\t': {  // HT -> naechster 8er-Tabstopp
        m_cx = std::min(m_cols - 1, ((m_cx / 8) + 1) * 8);
        m_wrapPending = false;
        break;
    }
    case '\n':  // LF
    case '\v':  // VT
    case '\f':  // FF
        newLine();
        m_wrapPending = false;
        break;
    case '\r':  // CR
        m_cx = 0;
        m_wrapPending = false;
        break;
    default:
        break;
    }
}

void TerminalEmulator::escDispatch(QChar b)
{
    switch (b.unicode()) {
    case '[':
        m_state = State::Csi;
        m_paramBuf.clear();
        return;
    case ']':
        m_state = State::Osc;
        m_oscBuf.clear();
        return;
    case '(':
    case ')':
    case '*':
    case '+':
    case '#':
        m_escInter = b;
        m_state = State::EscInter;
        return;
    case '7':
        saveCursor();
        break;
    case '8':
        restoreCursor();
        break;
    case 'M':
        reverseIndex();
        break;
    case 'D':
        newLine();
        break;
    case 'E':
        m_cx = 0;
        newLine();
        break;
    case 'c':
        reset();
        break;
    default:
        break;  // =, >, \, etc. — ignoriert
    }
    m_state = State::Ground;
}

std::vector<int> TerminalEmulator::params(int def, int count) const
{
    QString p = m_paramBuf;
    if (p.startsWith(QLatin1Char('?')))
        p.remove(0, 1);
    // Zwischenbytes am Ende (0x20-0x2f) abschneiden.
    while (!p.isEmpty() && p.back().unicode() >= 0x20 && p.back().unicode() <= 0x2f)
        p.chop(1);
    std::vector<int> out;
    const QStringList parts = p.split(QLatin1Char(';'));
    for (const QString &s : parts) {
        // ':'-Subparameter (z.B. Truecolor 38:2:...) auf ';' vereinfachen wird
        // hier nicht gebraucht — nur der Hauptwert zaehlt.
        const QString main = s.section(QLatin1Char(':'), 0, 0);
        out.push_back(main.isEmpty() ? def : main.toInt());
        if (static_cast<int>(out.size()) >= count)
            break;
    }
    if (out.empty())
        out.push_back(def);
    return out;
}

void TerminalEmulator::csiDispatch(QChar final)
{
    const bool priv = m_paramBuf.startsWith(QLatin1Char('?'));
    const std::vector<int> p = params(0);
    const int p0 = p.empty() ? 0 : p[0];
    const auto arg = [&](int idx, int def) {
        return (idx < static_cast<int>(p.size()) && p[idx] != 0) ? p[idx] : def;
    };

    switch (final.unicode()) {
    case 'A':  // CUU
        m_cy = std::max(m_top, m_cy - std::max(1, p0));
        m_wrapPending = false;
        break;
    case 'B':  // CUD
        m_cy = std::min(m_bottom, m_cy + std::max(1, p0));
        m_wrapPending = false;
        break;
    case 'C':  // CUF
        m_cx = std::min(m_cols - 1, m_cx + std::max(1, p0));
        m_wrapPending = false;
        break;
    case 'D':  // CUB
        m_cx = std::max(0, m_cx - std::max(1, p0));
        m_wrapPending = false;
        break;
    case 'E':  // CNL
        m_cy = std::min(m_bottom, m_cy + std::max(1, p0));
        m_cx = 0;
        m_wrapPending = false;
        break;
    case 'F':  // CPL
        m_cy = std::max(m_top, m_cy - std::max(1, p0));
        m_cx = 0;
        m_wrapPending = false;
        break;
    case 'G':  // CHA
    case '`':  // HPA
        m_cx = std::clamp(std::max(1, p0) - 1, 0, m_cols - 1);
        m_wrapPending = false;
        break;
    case 'd':  // VPA
        m_cy = std::clamp(std::max(1, p0) - 1, 0, m_rows - 1);
        m_wrapPending = false;
        break;
    case 'H':  // CUP
    case 'f': {  // HVP
        int row = std::max(1, arg(0, 1)) - 1;
        int col = std::max(1, arg(1, 1)) - 1;
        if (m_originMode)
            row += m_top;
        m_cy = std::clamp(row, 0, m_rows - 1);
        m_cx = std::clamp(col, 0, m_cols - 1);
        m_wrapPending = false;
        break;
    }
    case 'J':  // ED
        eraseInDisplay(p0);
        break;
    case 'K':  // EL
        eraseInLine(p0);
        break;
    case 'L':  // IL
        insertLines(std::max(1, p0));
        break;
    case 'M':  // DL
        deleteLines(std::max(1, p0));
        break;
    case '@':  // ICH
        insertChars(std::max(1, p0));
        break;
    case 'P':  // DCH
        deleteChars(std::max(1, p0));
        break;
    case 'X':  // ECH
        eraseChars(std::max(1, p0));
        break;
    case 'S':  // SU
        scrollUp(std::max(1, p0));
        break;
    case 'T':  // SD
        scrollDown(std::max(1, p0));
        break;
    case 'r': {  // DECSTBM Scrollregion
        const int top = std::max(1, arg(0, 1)) - 1;
        const int bot = std::clamp(arg(1, m_rows) - 1, 0, m_rows - 1);
        if (top < bot) {
            m_top = std::clamp(top, 0, m_rows - 1);
            m_bottom = bot;
            m_cx = 0;
            m_cy = m_originMode ? m_top : 0;
        }
        break;
    }
    case 'm':  // SGR
        applySgr(m_paramBuf);
        break;
    case 'h':  // SM / DECSET
        if (priv)
            privateMode(m_paramBuf, true);
        else
            setMode(m_paramBuf, true);
        break;
    case 'l':  // RM / DECRST
        if (priv)
            privateMode(m_paramBuf, false);
        else
            setMode(m_paramBuf, false);
        break;
    case 's':  // Cursor sichern (ANSI.SYS)
        saveCursor();
        break;
    case 'u':  // Cursor wiederherstellen
        restoreCursor();
        break;
    default:
        break;  // DSR/DA/andere: keine Ruecksendung noetig -> ignoriert
    }
}

void TerminalEmulator::putCodepoint(char32_t c)
{
    if (m_wrapPending) {
        m_cx = 0;
        newLine();
        m_wrapPending = false;
    }
    clampCursor();
    TermCell &cell = m_grid[m_cy][m_cx];
    cell.ch = c;
    cell.attrs = m_attrs;
    cell.fg = m_fg;
    cell.bg = m_bg;
    if (m_cx + 1 < m_cols) {
        ++m_cx;
    } else if (m_wrap) {
        m_wrapPending = true;  // verzoegerter Umbruch
    }
}

// --- Bewegung / Scrollen ----------------------------------------------------

void TerminalEmulator::newLine()
{
    if (m_cy == m_bottom)
        scrollUp(1);
    else if (m_cy < m_rows - 1)
        ++m_cy;
}

void TerminalEmulator::reverseIndex()
{
    if (m_cy == m_top)
        scrollDown(1);
    else if (m_cy > 0)
        --m_cy;
}

void TerminalEmulator::scrollUp(int n)
{
    const int height = m_bottom - m_top + 1;
    n = std::min(n, height);
    if (n <= 0)
        return;
    for (int r = m_top; r <= m_bottom - n; ++r)
        m_grid[r] = m_grid[r + n];
    for (int r = m_bottom - n + 1; r <= m_bottom; ++r)
        m_grid[r].assign(m_cols, blankCell());
}

void TerminalEmulator::scrollDown(int n)
{
    const int height = m_bottom - m_top + 1;
    n = std::min(n, height);
    if (n <= 0)
        return;
    for (int r = m_bottom; r >= m_top + n; --r)
        m_grid[r] = m_grid[r - n];
    for (int r = m_top; r < m_top + n; ++r)
        m_grid[r].assign(m_cols, blankCell());
}

// --- Loeschen ---------------------------------------------------------------

void TerminalEmulator::eraseInDisplay(int mode)
{
    if (mode == 2 || mode == 3) {
        for (auto &row : m_grid)
            row.assign(m_cols, blankCell());
        return;
    }
    if (mode == 0) {  // Cursor bis Ende
        for (int c = m_cx; c < m_cols; ++c)
            m_grid[m_cy][c] = blankCell();
        for (int r = m_cy + 1; r < m_rows; ++r)
            m_grid[r].assign(m_cols, blankCell());
    } else if (mode == 1) {  // Anfang bis Cursor
        for (int r = 0; r < m_cy; ++r)
            m_grid[r].assign(m_cols, blankCell());
        for (int c = 0; c <= m_cx && c < m_cols; ++c)
            m_grid[m_cy][c] = blankCell();
    }
}

void TerminalEmulator::eraseInLine(int mode)
{
    if (mode == 0) {
        for (int c = m_cx; c < m_cols; ++c)
            m_grid[m_cy][c] = blankCell();
    } else if (mode == 1) {
        for (int c = 0; c <= m_cx && c < m_cols; ++c)
            m_grid[m_cy][c] = blankCell();
    } else if (mode == 2) {
        m_grid[m_cy].assign(m_cols, blankCell());
    }
}

void TerminalEmulator::insertLines(int n)
{
    if (m_cy < m_top || m_cy > m_bottom)
        return;
    n = std::min(n, m_bottom - m_cy + 1);
    for (int r = m_bottom; r >= m_cy + n; --r)
        m_grid[r] = m_grid[r - n];
    for (int r = m_cy; r < m_cy + n; ++r)
        m_grid[r].assign(m_cols, blankCell());
}

void TerminalEmulator::deleteLines(int n)
{
    if (m_cy < m_top || m_cy > m_bottom)
        return;
    n = std::min(n, m_bottom - m_cy + 1);
    for (int r = m_cy; r <= m_bottom - n; ++r)
        m_grid[r] = m_grid[r + n];
    for (int r = m_bottom - n + 1; r <= m_bottom; ++r)
        m_grid[r].assign(m_cols, blankCell());
}

void TerminalEmulator::insertChars(int n)
{
    n = std::min(n, m_cols - m_cx);
    auto &row = m_grid[m_cy];
    for (int c = m_cols - 1; c >= m_cx + n; --c)
        row[c] = row[c - n];
    for (int c = m_cx; c < m_cx + n; ++c)
        row[c] = blankCell();
}

void TerminalEmulator::deleteChars(int n)
{
    n = std::min(n, m_cols - m_cx);
    auto &row = m_grid[m_cy];
    for (int c = m_cx; c <= m_cols - 1 - n; ++c)
        row[c] = row[c + n];
    for (int c = m_cols - n; c < m_cols; ++c)
        row[c] = blankCell();
}

void TerminalEmulator::eraseChars(int n)
{
    n = std::min(n, m_cols - m_cx);
    for (int c = m_cx; c < m_cx + n; ++c)
        m_grid[m_cy][c] = blankCell();
}

// --- Cursor sichern / Modi --------------------------------------------------

void TerminalEmulator::saveCursor()
{
    m_savedCx = m_cx;
    m_savedCy = m_cy;
    m_savedAttrs = m_attrs;
    m_savedFg = m_fg;
    m_savedBg = m_bg;
}

void TerminalEmulator::restoreCursor()
{
    m_cx = m_savedCx;
    m_cy = m_savedCy;
    m_attrs = m_savedAttrs;
    m_fg = m_savedFg;
    m_bg = m_savedBg;
    m_wrapPending = false;
    clampCursor();
}

void TerminalEmulator::setMode(const QString &, bool)
{
    // Nicht-private Modi (z.B. IRM Einfuegemodus) werden derzeit nicht benoetigt.
}

void TerminalEmulator::privateMode(const QString &paramBuf, bool set)
{
    QString p = paramBuf;
    if (p.startsWith(QLatin1Char('?')))
        p.remove(0, 1);
    for (const QString &s : p.split(QLatin1Char(';'))) {
        const int code = s.toInt();
        switch (code) {
        case 1:  // DECCKM Cursortasten-Modus
            m_appCursor = set;
            break;
        case 6:  // DECOM Ursprungsmodus
            m_originMode = set;
            m_cx = 0;
            m_cy = set ? m_top : 0;
            break;
        case 7:  // DECAWM Autowrap
            m_wrap = set;
            break;
        case 25:  // DECTCEM Cursor sichtbar
            m_cursorVisible = set;
            break;
        case 47:
        case 1047:
        case 1049:
            switchAltScreen(set);
            break;
        default:
            break;  // 12 (Blink), 2004 (Bracketed Paste), Maus etc.: ignoriert
        }
    }
}

void TerminalEmulator::switchAltScreen(bool alt)
{
    if (alt == m_altActive)
        return;
    if (alt) {
        m_savedPrimary = m_grid;
        m_altSavedCx = m_cx;
        m_altSavedCy = m_cy;
        for (auto &row : m_grid)
            row.assign(m_cols, blankCell());
        m_cx = m_cy = 0;
        m_top = 0;
        m_bottom = m_rows - 1;
    } else {
        if (static_cast<int>(m_savedPrimary.size()) == m_rows)
            m_grid = m_savedPrimary;
        m_savedPrimary.clear();
        m_cx = m_altSavedCx;
        m_cy = m_altSavedCy;
        m_top = 0;
        m_bottom = m_rows - 1;
        clampCursor();
    }
    m_altActive = alt;
    m_wrapPending = false;
}

void TerminalEmulator::applySgr(const QString &paramBuf)
{
    std::vector<int> codes;
    QString p = paramBuf;
    // SGR kennt kein '?'; falls doch vorhanden, entfernen.
    if (p.startsWith(QLatin1Char('?')))
        p.remove(0, 1);
    const QStringList parts =
        p.isEmpty() ? QStringList{QStringLiteral("0")} : p.split(QLatin1Char(';'));
    for (const QString &s : parts)
        codes.push_back(s.isEmpty() ? 0 : s.section(QLatin1Char(':'), 0, 0).toInt());

    for (int i = 0; i < static_cast<int>(codes.size()); ++i) {
        const int c = codes[i];
        if (c == 0) {
            m_attrs = 0;
            m_fg = QColor();
            m_bg = QColor();
        } else if (c == 1) {
            m_attrs |= AttrBold;
        } else if (c == 2) {
            m_attrs |= AttrDim;
        } else if (c == 22) {
            m_attrs &= ~(AttrBold | AttrDim);
        } else if (c == 3) {
            m_attrs |= AttrItalic;
        } else if (c == 23) {
            m_attrs &= ~AttrItalic;
        } else if (c == 4) {
            m_attrs |= AttrUnderline;
        } else if (c == 24) {
            m_attrs &= ~AttrUnderline;
        } else if (c == 7) {
            m_attrs |= AttrInverse;
        } else if (c == 27) {
            m_attrs &= ~AttrInverse;
        } else if (c >= 30 && c <= 37) {
            m_fg = ansi256(c - 30);
        } else if (c >= 90 && c <= 97) {
            m_fg = ansi256(c - 90 + 8);
        } else if (c == 39) {
            m_fg = QColor();
        } else if (c >= 40 && c <= 47) {
            m_bg = ansi256(c - 40);
        } else if (c >= 100 && c <= 107) {
            m_bg = ansi256(c - 100 + 8);
        } else if (c == 49) {
            m_bg = QColor();
        } else if (c == 38 || c == 48) {
            QColor *target = (c == 38) ? &m_fg : &m_bg;
            if (i + 2 < static_cast<int>(codes.size()) && codes[i + 1] == 5) {
                *target = ansi256(codes[i + 2]);
                i += 2;
            } else if (i + 4 < static_cast<int>(codes.size()) && codes[i + 1] == 2) {
                *target = QColor(codes[i + 2], codes[i + 3], codes[i + 4]);
                i += 4;
            }
        }
    }
}

QString TerminalEmulator::screenText() const
{
    QString out;
    for (int r = 0; r < m_rows; ++r) {
        QString line;
        for (int c = 0; c < m_cols; ++c)
            line += QChar(static_cast<char16_t>(m_grid[r][c].ch));
        while (line.endsWith(QLatin1Char(' ')))
            line.chop(1);
        out += line;
        if (r < m_rows - 1)
            out += QLatin1Char('\n');
    }
    return out;
}

} // namespace ncssh::core
