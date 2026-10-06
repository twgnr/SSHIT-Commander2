// Vollwertiger VT100/xterm-Zellengitter-Emulator (kein Widget, rein testbar).
//
// Der bisherige AnsiRenderer haengt Ausgabe an ein QPlainTextEdit an und kann
// nur Farben + wenige Steuerzeichen; Cursor-Adressierung, Loeschen, Scroll-
// regionen und der Alternate-Screen fehlen — deshalb rendern vim/htop/tmux/less
// nicht. Diese Klasse fuehrt statt dessen ein echtes 2D-Zellengitter mit Cursor,
// Attributen, Scrollregion und dem ueblichen CSI/ESC/OSC-Sprachumfang. Sie wird
// vom Terminal-Widget genutzt, sobald eine Anwendung auf den Alternate-Screen
// wechselt (DECSET 1049/1047/47).
#pragma once

#include <QByteArray>
#include <QColor>
#include <QString>
#include <qnamespace.h>
#include <cstdint>
#include <vector>

namespace ncssh::core {

// Eine Bildschirmzelle. Ungueltige fg/bg bedeuten "Theme-Standard verwenden"
// (der Emulator kennt die Theme-Farben bewusst nicht).
struct TermCell {
    char32_t ch = U' ';
    quint8 attrs = 0;  // Bit-Flags aus TermAttr
    QColor fg;
    QColor bg;
};

enum TermAttr : quint8 {
    AttrBold = 1,
    AttrItalic = 2,
    AttrUnderline = 4,
    AttrInverse = 8,
    AttrDim = 16,
    AttrWide = 32,     // Zeichen belegt diese UND die rechte Nachbarzelle
};

// Rechte Haelfte eines breiten Zeichens (CJK, Emoji): die Zelle zeigt nichts,
// ihr Inhalt steht in der linken Nachbarzelle (mit AttrWide).
constexpr char32_t kWideTail = 0;

// Anzeigebreite in Terminalspalten (wie wcwidth): 0 fuer kombinierende und
// unsichtbare Zeichen, 2 fuer ostasiatische Vollbreite und Emoji, sonst 1.
int charWidth(char32_t c);
// Summe der Anzeigebreiten eines UTF-16-Textes (Surrogatpaare korrekt).
int textWidth(const QString &text);

// DEC Special Graphics (Zeichensatz "0", ESC ( 0): ncurses zeichnet damit
// Rahmen — 'q' wird zu '─', 'x' zu '│' usw. Andere Zeichen bleiben.
char32_t decSpecialGraphics(char32_t c);

// Maus-Reporting (DECSET 9/1000/1002/1003) und Bracketed Paste (2004).
// Diese Modi setzen Programme auf beiden Schirmen (bash schaltet 2004 schon am
// Prompt ein) — das Terminal-Widget verfolgt sie deshalb ueber den gesamten
// Ausgabestrom, nicht nur im Zellengitter.
enum class MouseTracking { Off, X10, Normal, ButtonEvent, AnyEvent };

struct TerminalModes {
    MouseTracking mouse = MouseTracking::Off;
    bool sgrMouse = false;         // 1006: Koordinaten als Dezimalzahlen
    bool bracketedPaste = false;   // 2004

    // Wendet einen privaten Modus (CSI ? code h/l) an; false = nicht unserer.
    bool apply(int code, bool set);
};

enum class MouseAction { Press, Release, Move, WheelUp, WheelDown };

// Kodiert ein Mausereignis fuer die Anwendung. button: 0 links, 1 Mitte,
// 2 rechts, 3 = keine Taste (Bewegung ohne Taste). col/row sind 0-basiert.
// Leer, wenn der aktuelle Modus das Ereignis nicht meldet.
QByteArray encodeMouse(const TerminalModes &modes, MouseAction action, int button,
                       int col, int row, Qt::KeyboardModifiers mods);

// Bereitet eingefuegten Text fuer die Shell vor: Zeilenenden werden zu CR
// (wie getippt); bei Bracketed Paste kommt er zwischen ESC[200~ und ESC[201~,
// ein eingeschmuggeltes Endkennzeichen im Text wird entfernt (sonst liefe der
// Rest als getippte Befehle).
QString preparePaste(QString text, bool bracketed);

class TerminalEmulator {
public:
    explicit TerminalEmulator(int cols = 80, int rows = 24);

    void resize(int cols, int rows);
    void feed(const QString &data);  // darf mitten in einer Sequenz enden
    void reset();

    int cols() const { return m_cols; }
    int rows() const { return m_rows; }
    const TermCell &cell(int row, int col) const;

    int cursorRow() const { return m_cy; }
    int cursorCol() const { return m_cx; }
    bool cursorVisible() const { return m_cursorVisible; }
    bool applicationCursorKeys() const { return m_appCursor; }
    const QString &title() const { return m_title; }

    // Klartext des sichtbaren Schirms (fuer Kopieren / Mitschnitt).
    QString screenText() const;

private:
    enum class State { Ground, Esc, EscInter, Csi, Osc };

    void putCodepoint(char32_t c);
    void execC0(char c);
    void csiDispatch(QChar final);
    void escDispatch(QChar b);
    void applySgr(const QString &params);
    void setMode(const QString &params, bool set);
    void privateMode(const QString &params, bool set);

    void newLine();              // Cursor eine Zeile tiefer (Scrollregion beachtet)
    void reverseIndex();         // eine Zeile hoeher (Scrollregion beachtet)
    void scrollUp(int n);        // Inhalt der Scrollregion nach oben
    void scrollDown(int n);
    void eraseInDisplay(int mode);
    void eraseInLine(int mode);
    void insertLines(int n);
    void deleteLines(int n);
    void insertChars(int n);
    void deleteChars(int n);
    void eraseChars(int n);
    void saveCursor();
    void restoreCursor();
    void switchAltScreen(bool alt);
    // Kombinierendes Zeichen (Breite 0) mit dem zuletzt geschriebenen Zeichen
    // verschmelzen (a + U+0308 -> ä); ohne Vorkomposition wird es verworfen.
    void combineWithPrevious(char32_t mark);
    // Halbe breite Zeichen in einer Zeile beseitigen (nach Einfuegen/Loeschen/
    // Ueberschreiben einzelner Zellen): verwaiste Haelften werden zu Leerzellen.
    void repairWide(int row);

    TermCell blankCell() const;  // Leerzelle mit aktueller Hintergrundfarbe
    void clampCursor();
    void clampRegion();          // m_top/m_bottom in [0, m_rows) halten
    std::vector<int> params(int def, int count = 16) const;

    std::vector<std::vector<TermCell>> m_grid;
    int m_cols;
    int m_rows;

    int m_cx = 0;
    int m_cy = 0;
    int m_top = 0;                 // Scrollregion oben (0-basiert, inklusive)
    int m_bottom = 0;              // Scrollregion unten (0-basiert, inklusive)
    bool m_wrapPending = false;    // verzoegerter Umbruch am rechten Rand

    // Aktueller Stift (SGR).
    quint8 m_attrs = 0;
    QColor m_fg;
    QColor m_bg;

    // Zeichensaetze: G0/G1 jeweils ASCII oder DEC Special Graphics; SO/SI
    // schalten zwischen G0 und G1 um.
    bool m_g0Graphics = false;
    bool m_g1Graphics = false;
    bool m_shiftOut = false;       // SO aktiv -> G1 gilt

    // Gesicherter Cursor + Stift + Zeichensaetze (DECSC/DECRC).
    int m_savedCx = 0, m_savedCy = 0;
    quint8 m_savedAttrs = 0;
    QColor m_savedFg, m_savedBg;
    bool m_savedG0 = false, m_savedG1 = false, m_savedShift = false;

    // Modi.
    bool m_wrap = true;            // DECAWM
    bool m_originMode = false;     // DECOM
    bool m_cursorVisible = true;   // DECTCEM
    bool m_appCursor = false;      // DECCKM (Cursortasten senden SS3 statt CSI)
    bool m_altActive = false;

    // Gesicherter Primaerschirm-Zustand fuer den Alternate-Screen-Wechsel.
    std::vector<std::vector<TermCell>> m_savedPrimary;
    int m_altSavedCx = 0, m_altSavedCy = 0;

    QString m_title;

    // Parser.
    State m_state = State::Ground;
    QString m_paramBuf;
    QString m_oscBuf;
    QChar m_escInter;
    char32_t m_highSurrogate = 0;  // haengendes hohes Surrogat aus feed()
};

} // namespace ncssh::core
