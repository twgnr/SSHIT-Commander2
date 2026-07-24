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

#include <QColor>
#include <QString>
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
};

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

    TermCell blankCell() const;  // Leerzelle mit aktueller Hintergrundfarbe
    void clampCursor();
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

    // Gesicherter Cursor + Stift (DECSC/DECRC).
    int m_savedCx = 0, m_savedCy = 0;
    quint8 m_savedAttrs = 0;
    QColor m_savedFg, m_savedBg;

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
