// ANSI/VT-Renderer fuer ein QPlainTextEdit (farbige Terminal-Ausgabe).
//
// Interpretiert SGR-Farben (16/256/Truecolor, fett/kursiv/unterstrichen),
// behandelt Zeilenumbruch, Wagenruecklauf, Zeile-/Bildschirm-Loeschen,
// Cursorpositionierung und die DEC-Liniengrafik; andere Steuersequenzen werden
// verworfen, statt als "Muell" angezeigt zu werden.
//
// Bildschirmmodell: Die untersten Zeilen des Dokuments bilden den Bildschirm
// des PTY (setScreenSize). Absolute Positionen (ESC[Zeile;SpalteH) zaehlen ab
// dessen oberster Zeile — in SICHTBAREN Zeilen, also mit Umbruch bei der
// Terminalbreite, wie ConPTY sie zaehlt. Darueber liegt der Rollpuffer.
#pragma once

#include <QColor>
#include <QString>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <optional>

class QPlainTextEdit;

namespace ncssh::gui {

class AnsiRenderer {
public:
    explicit AnsiRenderer(QPlainTextEdit *editor);

    // Setzt Farben/Attribute auf die Theme-Vorgaben zurueck und verwirft eine
    // angeschnittene Steuersequenz.
    void reset();

    // Verarbeitet einen Ausgabe-Chunk (darf mitten in einer Sequenz enden).
    void feed(const QString &text);

    // Groesse des PTY-Bildschirms (wie dem Backend gemeldet). 0 = unbekannt:
    // dann kein Umbruch-Zaehlen bzw. kein Mitrollen des Bildschirms.
    void setScreenSize(int cols, int rows);

    // Theme gewechselt: Standardfarben neu lesen und den vorhandenen Text
    // umfaerben (Standardtext -> neue Textfarbe, Farben lesbar fuer den neuen
    // Hintergrund).
    void retheme();

    // Entfernt ANSI-/VT-Steuersequenzen — fuer Mitschnitt und Textkopien.
    static QString stripAnsi(const QString &text);

private:
    QTextCharFormat format() const;
    void applySgr(const QString &params);
    void handleCsi(QTextCursor &cur, const QString &params, QChar final);
    void newline(QTextCursor &cur);
    // Schreibt text ab der Cursorspalte der Cursorzeile und UEBERSCHREIBT
    // dabei vorhandene Zeichen (wie ein echtes Terminal).
    void writeText(QTextCursor &cur, const QString &text);
    // Cursor auf (Cursorzeile, m_col) setzen; fehlende Spalten mit Leerzeichen.
    void placeCursor(QTextCursor &cur, bool pad);

    // --- Bildschirmmodell --------------------------------------------------
    QTextBlock cursorBlock() const;
    // Sichtbare Zeilen eines Blocks (Umbruch bei m_cols); der Cursorblock
    // zaehlt eine Cursorposition hinter dem Text mit.
    int rowsOf(const QTextBlock &block, bool isCursorBlock) const;
    // Sichtbare Zeile des Cursors innerhalb seines Blocks.
    int cursorRowInBlock(const QTextBlock &block) const;
    // Neue leere Zeile am Dokumentende.
    void appendLine(QTextCursor &cur);
    // Oberste Bildschirmzeile nachziehen, wenn unten mehr als m_rows Zeilen
    // stehen (der Bildschirm ist mitgerollt).
    void normalizeScreen();
    // Sichtbare Zeile des Cursors ab der obersten Bildschirmzeile (0-basiert).
    int cursorScreenRow() const;
    // Cursor auf Bildschirmzeile row (0-basiert) und Spalte termCol dieser
    // Zeile; fehlende Zeilen unterhalb werden angelegt.
    void moveToScreenRow(QTextCursor &cur, int row, int termCol);
    void eraseDisplay(QTextCursor &cur, int mode);

    QPlainTextEdit *m_editor;
    QString m_defFg;
    QString m_defBg;
    std::optional<QColor> m_fg;
    std::optional<QColor> m_bg;
    bool m_bold = false;
    bool m_italic = false;
    bool m_underline = false;
    bool m_reverse = false;
    bool m_pendingCr = false;  // (unbenutzt; Zeilenmodell braucht es nicht mehr)
    // Cursorspalte in der letzten Zeile (bleibt ueber Chunks). Gezaehlt in
    // Terminalspalten, nicht in Zeichen: CJK/Emoji belegen zwei Spalten,
    // kombinierende Akzente keine — so passen Rueckschritte der Shell.
    int m_col = 0;
    // Zeichensaetze G0/G1 (ESC ( 0 = DEC-Liniengrafik), SO/SI waehlen G1/G0.
    bool m_g0Graphics = false;
    bool m_g1Graphics = false;
    bool m_shiftOut = false;
    QString m_carry;           // angeschnittene Escape-Sequenz vom letzten Chunk
    bool m_inString = false;   // mitten in OSC/DCS (bis BEL bzw. ESC \ ueberspringen)
    int m_stringLen = 0;       // bisher uebersprungene Zeichen dieser Zeichenkette

    int m_cols = 0;            // PTY-Breite/-Hoehe (0 = unbekannt)
    int m_rows = 0;
    // Cursorzeile als Abstand zum LETZTEN Block (0 = letzte Zeile). Abstaende
    // vom Ende bleiben gueltig, wenn setMaximumBlockCount oben Zeilen abschneidet.
    int m_up = 0;
    // Oberste Bildschirmzeile: Anfang ihres Blocks (Qt fuehrt den Cursor bei
    // Aenderungen mit) und die sichtbare Zeile darin (bei umbrochenen Zeilen).
    QTextCursor m_top;
    int m_topRow = 0;
};

} // namespace ncssh::gui
