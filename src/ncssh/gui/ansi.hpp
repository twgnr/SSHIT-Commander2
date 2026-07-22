// ANSI/VT-Renderer fuer ein QPlainTextEdit (farbige Terminal-Ausgabe).
//
// Interpretiert SGR-Farben (16/256/Truecolor, fett/kursiv/unterstrichen),
// behandelt Zeilenumbruch, Wagenruecklauf und Zeile-Loeschen; andere
// Steuersequenzen werden verworfen, statt als "Muell" angezeigt zu werden.
// (Port von gui/ansi.py)
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

    // Setzt Farben/Attribute auf die Theme-Vorgaben zurueck.
    void reset();

    // Verarbeitet einen Ausgabe-Chunk (darf mitten in einer Sequenz enden).
    void feed(const QString &text);

private:
    QTextCharFormat format() const;
    void applySgr(const QString &params);
    void handleCsi(QTextCursor &cur, const QString &params, QChar final);
    void clearLine(QTextCursor &cur);
    void newline(QTextCursor &cur);

    QPlainTextEdit *m_editor;
    QString m_defFg;
    QString m_defBg;
    std::optional<QColor> m_fg;
    std::optional<QColor> m_bg;
    bool m_bold = false;
    bool m_italic = false;
    bool m_underline = false;
    bool m_reverse = false;
    bool m_pendingCr = false;  // \r am Chunk-Ende (moegliches \r\n ueber die Grenze)
};

} // namespace ncssh::gui
