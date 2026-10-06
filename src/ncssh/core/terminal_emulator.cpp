#include "ncssh/core/terminal_emulator.hpp"

#include <QChar>
#include <algorithm>
#include <iterator>

namespace ncssh::core {

// 256-Farben-Palette wie im AnsiRenderer (16 Basis + 6x6x6 Wuerfel + Graustufen).
static const char *kBase16[] = {
    "#2e3436", "#cc0000", "#4e9a06", "#c4a000", "#3465a4", "#75507b", "#06989a", "#d3d7cf",
    "#555753", "#ef2929", "#8ae234", "#fce94f", "#729fcf", "#ad7fa8", "#34e2e2", "#eeeeec",
};

// Obergrenzen gegen boesartige/kaputte Sequenzen: numerische Parameter werden
// gedeckelt (ESC[2147483647B liess m_cy + p0 ueberlaufen -> negativer Cursor
// -> Heap-Schreibzugriff), der Parameterpuffer einer nie endenden CSI waechst
// nicht unbegrenzt.
static constexpr int kMaxParam = 9999;
static constexpr int kMaxParamBuf = 256;

// Liest eine Dezimalzahl mit Deckel kMaxParam (leer -> def). toInt() lieferte
// bei Ueberlauf 0 bzw. ungedeckelte Riesenwerte.
static int parseParam(const QString &s, int def)
{
    if (s.isEmpty())
        return def;
    int v = 0;
    for (const QChar ch : s) {
        const ushort u = ch.unicode();
        if (u < '0' || u > '9')
            break;
        v = std::min(kMaxParam, v * 10 + (u - '0'));
    }
    return v;
}

static QColor ansi256(int n)
{
    n = std::clamp(n, 0, 255);
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

// --- Zeichenbreite / Zeichensaetze --------------------------------------------

namespace {
struct Range {
    char32_t first;
    char32_t last;
};

// Ostasiatische Vollbreite (Unicode EastAsianWidth W/F) und Emoji mit
// Emoji_Presentation — sortiert, fuer die binaere Suche.
constexpr Range kWide[] = {
    {0x1100, 0x115F},   {0x231A, 0x231B},   {0x2329, 0x232A},   {0x23E9, 0x23EC},
    {0x23F0, 0x23F0},   {0x23F3, 0x23F3},   {0x25FD, 0x25FE},   {0x2614, 0x2615},
    {0x2648, 0x2653},   {0x267F, 0x267F},   {0x2693, 0x2693},   {0x26A1, 0x26A1},
    {0x26AA, 0x26AB},   {0x26BD, 0x26BE},   {0x26C4, 0x26C5},   {0x26CE, 0x26CE},
    {0x26D4, 0x26D4},   {0x26EA, 0x26EA},   {0x26F2, 0x26F3},   {0x26F5, 0x26F5},
    {0x26FA, 0x26FA},   {0x26FD, 0x26FD},   {0x2705, 0x2705},   {0x270A, 0x270B},
    {0x2728, 0x2728},   {0x274C, 0x274C},   {0x274E, 0x274E},   {0x2753, 0x2755},
    {0x2757, 0x2757},   {0x2795, 0x2797},   {0x27B0, 0x27B0},   {0x27BF, 0x27BF},
    {0x2B1B, 0x2B1C},   {0x2B50, 0x2B50},   {0x2B55, 0x2B55},   {0x2E80, 0x303E},
    {0x3041, 0x33FF},   {0x3400, 0x4DBF},   {0x4E00, 0x9FFF},   {0xA000, 0xA4CF},
    {0xA960, 0xA97F},   {0xAC00, 0xD7A3},   {0xF900, 0xFAFF},   {0xFE10, 0xFE19},
    {0xFE30, 0xFE6F},   {0xFF00, 0xFF60},   {0xFFE0, 0xFFE6},   {0x16FE0, 0x16FE4},
    {0x17000, 0x18CFF}, {0x1B000, 0x1B2FF}, {0x1F004, 0x1F004}, {0x1F0CF, 0x1F0CF},
    {0x1F18E, 0x1F18E}, {0x1F191, 0x1F19A}, {0x1F200, 0x1F202}, {0x1F210, 0x1F23B},
    {0x1F240, 0x1F248}, {0x1F250, 0x1F251}, {0x1F260, 0x1F265}, {0x1F300, 0x1F320},
    {0x1F32D, 0x1F335}, {0x1F337, 0x1F37C}, {0x1F37E, 0x1F393}, {0x1F3A0, 0x1F3CA},
    {0x1F3CF, 0x1F3D3}, {0x1F3E0, 0x1F3F0}, {0x1F3F4, 0x1F3F4}, {0x1F3F8, 0x1F43E},
    {0x1F440, 0x1F440}, {0x1F442, 0x1F4FC}, {0x1F4FF, 0x1F53D}, {0x1F54B, 0x1F54E},
    {0x1F550, 0x1F567}, {0x1F57A, 0x1F57A}, {0x1F595, 0x1F596}, {0x1F5A4, 0x1F5A4},
    {0x1F5FB, 0x1F64F}, {0x1F680, 0x1F6C5}, {0x1F6CC, 0x1F6CC}, {0x1F6D0, 0x1F6D2},
    {0x1F6D5, 0x1F6D7}, {0x1F6DC, 0x1F6DF}, {0x1F6EB, 0x1F6EC}, {0x1F6F4, 0x1F6FC},
    {0x1F7E0, 0x1F7EB}, {0x1F7F0, 0x1F7F0}, {0x1F90C, 0x1F93A}, {0x1F93C, 0x1F945},
    {0x1F947, 0x1F9FF}, {0x1FA70, 0x1FAFF}, {0x20000, 0x2FFFD}, {0x30000, 0x3FFFD},
};
}  // namespace

int charWidth(char32_t c)
{
    if (c < 0x20 || (c >= 0x7f && c < 0xa0))
        return 0;  // Steuerzeichen belegen keine Zelle
    if (c < 0x300)
        return 1;  // Latin — der haeufigste Fall ohne Nachschlagen
    if (c == 0x200B || (c >= 0x1160 && c <= 0x11FF))
        return 0;  // Nullbreite-Leerzeichen, Hangul-Jamo-Vokale (verbinden sich)
    switch (QChar::category(c)) {
    case QChar::Mark_NonSpacing:
    case QChar::Mark_Enclosing:
    case QChar::Other_Format:
        return 0;
    default:
        break;
    }
    const auto it = std::upper_bound(std::begin(kWide), std::end(kWide), c,
                                     [](char32_t v, const Range &r) { return v < r.first; });
    if (it != std::begin(kWide) && c <= std::prev(it)->last)
        return 2;
    return 1;
}

int textWidth(const QString &text)
{
    int w = 0;
    for (int i = 0; i < text.size(); ++i) {
        char32_t c = text.at(i).unicode();
        if (QChar::isHighSurrogate(c) && i + 1 < text.size()
            && text.at(i + 1).isLowSurrogate()) {
            c = QChar::surrogateToUcs4(text.at(i), text.at(i + 1));
            ++i;
        }
        w += charWidth(c);
    }
    return w;
}

char32_t decSpecialGraphics(char32_t c)
{
    // VT100-Tabelle fuer 0x5F..0x7E.
    static constexpr char32_t kMap[] = {
        0x00A0,                                          // _  geschuetztes Leerzeichen
        0x25C6, 0x2592, 0x2409, 0x240C, 0x240D, 0x240A,  // ` a b c d e
        0x00B0, 0x00B1, 0x2424, 0x240B, 0x2518, 0x2510,  // f g h i j k
        0x250C, 0x2514, 0x253C, 0x23BA, 0x23BB, 0x2500,  // l m n o p q
        0x23BC, 0x23BD, 0x251C, 0x2524, 0x2534, 0x252C,  // r s t u v w
        0x2502, 0x2264, 0x2265, 0x03C0, 0x2260, 0x00A3,  // x y z { | }
        0x00B7,                                          // ~
    };
    if (c >= 0x5F && c <= 0x7E)
        return kMap[c - 0x5F];
    return c;
}

bool TerminalModes::apply(int code, bool set)
{
    switch (code) {
    case 9:
        mouse = set ? MouseTracking::X10 : MouseTracking::Off;
        return true;
    case 1000:
        mouse = set ? MouseTracking::Normal : MouseTracking::Off;
        return true;
    case 1002:
        mouse = set ? MouseTracking::ButtonEvent : MouseTracking::Off;
        return true;
    case 1003:
        mouse = set ? MouseTracking::AnyEvent : MouseTracking::Off;
        return true;
    case 1006:
        sgrMouse = set;
        return true;
    case 2004:
        bracketedPaste = set;
        return true;
    default:
        return false;
    }
}

QByteArray encodeMouse(const TerminalModes &modes, MouseAction action, int button, int col,
                       int row, Qt::KeyboardModifiers mods)
{
    const MouseTracking m = modes.mouse;
    if (m == MouseTracking::Off)
        return {};
    const bool wheel = action == MouseAction::WheelUp || action == MouseAction::WheelDown;
    // Was der Modus ueberhaupt meldet (xterm-Semantik).
    if (m == MouseTracking::X10 && action != MouseAction::Press)
        return {};
    if (action == MouseAction::Move) {
        if (m == MouseTracking::Normal)
            return {};
        if (m == MouseTracking::ButtonEvent && (button < 0 || button > 2))
            return {};  // 1002: Bewegung nur mit gedrueckter Taste
    }
    if (!wheel && (button < 0 || button > 3))
        return {};

    int code = wheel ? (action == MouseAction::WheelUp ? 64 : 65) : button;
    if (action == MouseAction::Move)
        code += 32;
    if (m != MouseTracking::X10) {  // X10 kennt keine Modifikatoren
        if (mods & Qt::ShiftModifier)
            code += 4;
        if (mods & Qt::AltModifier)
            code += 8;
        if (mods & Qt::ControlModifier)
            code += 16;
    }
    col = std::max(0, col);
    row = std::max(0, row);

    if (modes.sgrMouse) {
        // SGR: Loslassen mit 'm' und der echten Taste — eindeutig, kein Limit.
        const char fin = action == MouseAction::Release ? 'm' : 'M';
        return QByteArrayLiteral("\x1b[<") + QByteArray::number(code) + ';'
               + QByteArray::number(col + 1) + ';' + QByteArray::number(row + 1) + fin;
    }
    // Klassisch: jede Zahl als ein Byte (32 + Wert). Loslassen meldet Taste 3.
    if (action == MouseAction::Release)
        code = (code & ~3) | 3;
    QByteArray out = QByteArrayLiteral("\x1b[M");
    out += static_cast<char>(32 + code);
    out += static_cast<char>(std::min(255, 32 + col + 1));  // mehr als 223 Spalten geht nicht
    out += static_cast<char>(std::min(255, 32 + row + 1));
    return out;
}

QString preparePaste(QString text, bool bracketed)
{
    // Getippte Eingabe endet mit CR — LF bzw. CRLF aus der Zwischenablage
    // sonst als doppelte bzw. fremde Zeilenenden.
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\r"));
    text.replace(QLatin1Char('\n'), QLatin1Char('\r'));
    if (!bracketed)
        return text;
    // Wiederholt: "ESC[20" + Marke + "1~" ergaebe nach einmaligem Entfernen
    // wieder eine Marke.
    const QString endMark = QStringLiteral("\x1b[201~");
    while (text.contains(endMark))
        text.remove(endMark);
    return QStringLiteral("\x1b[200~") + text + QStringLiteral("\x1b[201~");
}

// --- Emulator -----------------------------------------------------------------

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
    m_g0Graphics = m_g1Graphics = m_shiftOut = false;
    m_savedG0 = m_savedG1 = m_savedShift = false;
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
    const auto regrid = [&](const std::vector<std::vector<TermCell>> &old) {
        std::vector<std::vector<TermCell>> ng(rows, std::vector<TermCell>(cols, TermCell{}));
        for (int r = 0; r < std::min(rows, static_cast<int>(old.size())); ++r)
            for (int c = 0; c < std::min(cols, static_cast<int>(old[r].size())); ++c)
                ng[r][c] = old[r][c];
        return ng;
    };
    m_grid = regrid(m_grid);
    // Gesicherten Primaerschirm mitziehen: sonst kaeme er beim Verlassen des
    // Alternate-Screens mit alter Spaltenzahl zurueck, und Schreibzugriffe bis
    // m_cols liefen ueber das Zeilenende hinaus.
    if (!m_savedPrimary.empty())
        m_savedPrimary = regrid(m_savedPrimary);
    m_cols = cols;
    m_rows = rows;
    m_top = 0;
    m_bottom = m_rows - 1;
    m_wrapPending = false;
    clampCursor();
    // Am neuen rechten Rand abgeschnittene breite Zeichen beseitigen.
    for (int r = 0; r < m_rows; ++r)
        repairWide(r);
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

void TerminalEmulator::clampRegion()
{
    // Scrollregion immer innerhalb des Gitters halten (Schutz fuer die
    // Zeilenindizes in scrollUp/scrollDown).
    m_top = std::clamp(m_top, 0, m_rows - 1);
    m_bottom = std::clamp(m_bottom, m_top, m_rows - 1);
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
            // Zweites Byte einer ESC-(-/)-Sequenz: Zeichensatz fuer G0/G1
            // ('0' = DEC-Liniengrafik, alles andere = ASCII). ESC # (DECALN)
            // und die G2/G3-Zuweisungen werden verworfen.
            if (m_escInter == QLatin1Char('('))
                m_g0Graphics = (u == '0');
            else if (m_escInter == QLatin1Char(')'))
                m_g1Graphics = (u == '0');
            m_state = State::Ground;
            break;

        case State::Csi:
            if (u >= 0x20 && u <= 0x3f) {
                // Parameter-/Privat-/Zwischenbytes; ueberlange Sequenzen werden
                // abgeschnitten statt den Speicher unbegrenzt zu fuellen.
                if (m_paramBuf.size() < kMaxParamBuf)
                    m_paramBuf += qc;
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
    case 0x0e:  // SO: G1 aktiv
        m_shiftOut = true;
        break;
    case 0x0f:  // SI: G0 aktiv
        m_shiftOut = false;
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
        out.push_back(parseParam(main, def));
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

    // Vertikale Relativbewegung bleibt in der Scrollregion — ausser der Cursor
    // steht schon ausserhalb, dann gilt der Schirmrand (sonst sprang er z.B.
    // bei CUD unterhalb der Region wieder nach oben).
    clampCursor();
    const int upLimit = (m_cy >= m_top) ? m_top : 0;
    const int downLimit = (m_cy <= m_bottom) ? m_bottom : m_rows - 1;

    switch (final.unicode()) {
    case 'A':  // CUU
        m_cy = std::max(upLimit, m_cy - std::max(1, p0));
        m_wrapPending = false;
        break;
    case 'B':  // CUD
        m_cy = std::min(downLimit, m_cy + std::max(1, p0));
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
        m_cy = std::min(downLimit, m_cy + std::max(1, p0));
        m_cx = 0;
        m_wrapPending = false;
        break;
    case 'F':  // CPL
        m_cy = std::max(upLimit, m_cy - std::max(1, p0));
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
            row = std::min(row + m_top, m_bottom);  // DECOM: relativ zur Region
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
    if (m_shiftOut ? m_g1Graphics : m_g0Graphics)
        c = decSpecialGraphics(c);
    int w = charWidth(c);
    if (w == 0) {
        combineWithPrevious(c);
        return;
    }
    if (m_cols < 2)
        w = 1;  // ein breites Zeichen passt nie — dann wenigstens halb zeigen
    if (m_wrapPending) {
        m_cx = 0;
        newLine();
        m_wrapPending = false;
    }
    clampCursor();
    if (w == 2 && m_cx == m_cols - 1) {
        // Passt nicht mehr in die Zeile: wie xterm vorher umbrechen (die
        // letzte Zelle bleibt leer) bzw. ohne Autowrap die letzten zwei
        // Zellen ueberschreiben.
        if (m_wrap) {
            m_grid[m_cy][m_cx] = blankCell();
            m_cx = 0;
            newLine();
        } else {
            m_cx = m_cols - 2;
        }
    }
    auto &line = m_grid[m_cy];
    // Ueberschriebene Haelften frueherer breiter Zeichen beseitigen: links ein
    // breites Zeichen, dessen rechte Haelfte hier lag, rechts eine rechte
    // Haelfte, deren linke wir gleich ueberschreiben.
    if (m_cx > 0 && (line[m_cx - 1].attrs & AttrWide)) {
        line[m_cx - 1].ch = U' ';
        line[m_cx - 1].attrs &= ~AttrWide;
    }
    if (m_cx + w < m_cols && line[m_cx + w].ch == kWideTail)
        line[m_cx + w].ch = U' ';
    TermCell &cell = line[m_cx];
    cell.ch = c;
    cell.attrs = static_cast<quint8>((m_attrs & ~AttrWide) | (w == 2 ? AttrWide : 0));
    cell.fg = m_fg;
    cell.bg = m_bg;
    if (w == 2) {
        TermCell &tail = line[m_cx + 1];
        tail.ch = kWideTail;
        tail.attrs = static_cast<quint8>(m_attrs & ~AttrWide);
        tail.fg = m_fg;
        tail.bg = m_bg;
    }
    if (m_cx + w < m_cols) {
        m_cx += w;
    } else {
        m_cx = m_cols - 1;
        if (m_wrap)
            m_wrapPending = true;  // verzoegerter Umbruch
    }
}

void TerminalEmulator::combineWithPrevious(char32_t mark)
{
    // Das zuletzt geschriebene Zeichen steht links vom Cursor — bzw. unter ihm,
    // wenn am rechten Rand der Umbruch noch aussteht.
    clampCursor();
    int col = m_wrapPending ? m_cx : m_cx - 1;
    if (col > 0 && m_grid[m_cy][col].ch == kWideTail)
        --col;
    if (col < 0)
        return;
    TermCell &cell = m_grid[m_cy][col];
    if (cell.ch == kWideTail || cell.ch == U' ')
        return;
    const char32_t pair[2] = {cell.ch, mark};
    const QList<uint> composed =
        QString::fromUcs4(pair, 2).normalized(QString::NormalizationForm_C).toUcs4();
    if (composed.size() == 1)
        cell.ch = composed.front();
    // Sonst (Emoji-Verbinder, Variantenwahl, Akzent ohne Vorkomposition):
    // verwerfen — das Grundzeichen bleibt lesbar stehen.
}

void TerminalEmulator::repairWide(int row)
{
    if (row < 0 || row >= m_rows)
        return;
    auto &line = m_grid[row];
    for (int c = 0; c < m_cols; ++c) {
        TermCell &cell = line[c];
        if (cell.ch == kWideTail) {
            if (c == 0 || !(line[c - 1].attrs & AttrWide))
                cell.ch = U' ';  // verwaiste rechte Haelfte
        } else if (cell.attrs & AttrWide) {
            if (c + 1 >= m_cols || line[c + 1].ch != kWideTail) {
                cell.ch = U' ';  // linke Haelfte ohne Partner
                cell.attrs &= ~AttrWide;
            }
        }
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
    clampRegion();
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
    clampRegion();
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
    clampCursor();  // Gitter nie mit ungeprueftem Cursor indizieren
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
    repairWide(m_cy);
}

void TerminalEmulator::eraseInLine(int mode)
{
    clampCursor();
    if (mode == 0) {
        for (int c = m_cx; c < m_cols; ++c)
            m_grid[m_cy][c] = blankCell();
    } else if (mode == 1) {
        for (int c = 0; c <= m_cx && c < m_cols; ++c)
            m_grid[m_cy][c] = blankCell();
    } else if (mode == 2) {
        m_grid[m_cy].assign(m_cols, blankCell());
    }
    repairWide(m_cy);
}

void TerminalEmulator::insertLines(int n)
{
    clampCursor();
    clampRegion();
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
    clampCursor();
    clampRegion();
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
    clampCursor();
    n = std::min(n, m_cols - m_cx);
    auto &row = m_grid[m_cy];
    for (int c = m_cols - 1; c >= m_cx + n; --c)
        row[c] = row[c - n];
    for (int c = m_cx; c < m_cx + n; ++c)
        row[c] = blankCell();
    repairWide(m_cy);
}

void TerminalEmulator::deleteChars(int n)
{
    clampCursor();
    n = std::min(n, m_cols - m_cx);
    auto &row = m_grid[m_cy];
    for (int c = m_cx; c <= m_cols - 1 - n; ++c)
        row[c] = row[c + n];
    for (int c = m_cols - n; c < m_cols; ++c)
        row[c] = blankCell();
    repairWide(m_cy);
}

void TerminalEmulator::eraseChars(int n)
{
    clampCursor();
    n = std::min(n, m_cols - m_cx);
    for (int c = m_cx; c < m_cx + n; ++c)
        m_grid[m_cy][c] = blankCell();
    repairWide(m_cy);
}

// --- Cursor sichern / Modi --------------------------------------------------

void TerminalEmulator::saveCursor()
{
    m_savedCx = m_cx;
    m_savedCy = m_cy;
    m_savedAttrs = m_attrs;
    m_savedFg = m_fg;
    m_savedBg = m_bg;
    m_savedG0 = m_g0Graphics;
    m_savedG1 = m_g1Graphics;
    m_savedShift = m_shiftOut;
}

void TerminalEmulator::restoreCursor()
{
    m_cx = m_savedCx;
    m_cy = m_savedCy;
    m_attrs = m_savedAttrs;
    m_fg = m_savedFg;
    m_bg = m_savedBg;
    m_g0Graphics = m_savedG0;
    m_g1Graphics = m_savedG1;
    m_shiftOut = m_savedShift;
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
            // 12 (Blink) wird ignoriert; Maus (9/100x) und Bracketed Paste
            // (2004) verfolgt das Terminal-Widget ueber den ganzen Strom.
            break;
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
        codes.push_back(parseParam(s.section(QLatin1Char(':'), 0, 0), 0));

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
                *target = QColor(std::clamp(codes[i + 2], 0, 255),
                                 std::clamp(codes[i + 3], 0, 255),
                                 std::clamp(codes[i + 4], 0, 255));
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
        for (int c = 0; c < m_cols; ++c) {
            const char32_t ch = m_grid[r][c].ch;
            if (ch != kWideTail)  // rechte Haelfte eines breiten Zeichens
                line += QString::fromUcs4(&ch, 1);
        }
        while (line.endsWith(QLatin1Char(' ')))
            line.chop(1);
        out += line;
        if (r < m_rows - 1)
            out += QLatin1Char('\n');
    }
    return out;
}

} // namespace ncssh::core
