// ANSI/VT-Renderer fuer ein QPlainTextEdit (farbige Terminal-Ausgabe).
//
// Interpretiert SGR-Farben (16/256/Truecolor, fett/kursiv/unterstrichen),
// behandelt Zeilenumbruch, Wagenruecklauf und Zeile-Loeschen; andere
// Steuersequenzen werden verworfen, statt als "Muell" angezeigt zu werden.
#pragma once

#include <QColor>
#include <QString>
#include <QTextCharFormat>
#include <optional>

class QPlainTextEdit;
class QTextCursor;

namespace ncssh::gui {

class AnsiRenderer {
public:
    explicit AnsiRenderer(QPlainTextEdit *editor);

    // Setzt Farben/Attribute auf die Theme-Vorgaben zurueck und verwirft eine
    // angeschnittene Steuersequenz.
    void reset();

    // Verarbeitet einen Ausgabe-Chunk (darf mitten in einer Sequenz enden).
    void feed(const QString &text);

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
    // Schreibt text ab der Cursorspalte der letzten Zeile und UEBERSCHREIBT
    // dabei vorhandene Zeichen (wie ein echtes Terminal).
    void writeText(QTextCursor &cur, const QString &text);
    // Cursor auf (letzte Zeile, m_col) setzen; fehlende Spalten mit Leerzeichen.
    void placeCursor(QTextCursor &cur, bool pad);

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
    int m_col = 0;             // Cursorspalte in der letzten Zeile (bleibt ueber Chunks)
    QString m_carry;           // angeschnittene Escape-Sequenz vom letzten Chunk
    bool m_inString = false;   // mitten in OSC/DCS (bis BEL bzw. ESC \ ueberspringen)
    int m_stringLen = 0;       // bisher uebersprungene Zeichen dieser Zeichenkette
};

} // namespace ncssh::gui
