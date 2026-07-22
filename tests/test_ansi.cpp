// Tests fuer den ANSI-Filter des Terminals (AnsiRenderer::stripAnsi).
//
// Die Funktion entscheidet, was im Mitschnitt landet — sie muss Steuerzeichen
// entfernen, ohne Nutztext zu fressen.
#include "tests/harness.hpp"

#include "ncssh/gui/ansi.hpp"

using ncssh::gui::AnsiRenderer;

TEST(ansi, strips_sgr_colors)
{
    CHECK_EQ(AnsiRenderer::stripAnsi(QStringLiteral("\x1b[31mrot\x1b[0m")),
             QStringLiteral("rot"));
    CHECK_EQ(AnsiRenderer::stripAnsi(QStringLiteral("\x1b[1;38;5;208mtext\x1b[m")),
             QStringLiteral("text"));
}

TEST(ansi, strips_cursor_and_erase_sequences)
{
    CHECK_EQ(AnsiRenderer::stripAnsi(QStringLiteral("a\x1b[2Kb\x1b[10;5Hc")),
             QStringLiteral("abc"));
    // private Modi (?25l = Cursor aus)
    CHECK_EQ(AnsiRenderer::stripAnsi(QStringLiteral("\x1b[?25lx\x1b[?25h")),
             QStringLiteral("x"));
}

TEST(ansi, strips_osc_title)
{
    // Fenstertitel, mit BEL bzw. ST abgeschlossen
    CHECK_EQ(AnsiRenderer::stripAnsi(QStringLiteral("\x1b]0;mein titel\x07hallo")),
             QStringLiteral("hallo"));
    CHECK_EQ(AnsiRenderer::stripAnsi(QStringLiteral("\x1b]2;t\x1b\\ende")),
             QStringLiteral("ende"));
}

TEST(ansi, keeps_newlines_but_drops_carriage_return)
{
    // \r wuerde die Zeilen im Mitschnitt uebereinanderlegen.
    CHECK_EQ(AnsiRenderer::stripAnsi(QStringLiteral("eins\r\nzwei\r\n")),
             QStringLiteral("eins\nzwei\n"));
}

TEST(ansi, leaves_plain_text_untouched)
{
    const QString plain = QStringLiteral("nur text mit [Klammern] und 100% Zeichen");
    CHECK_EQ(AnsiRenderer::stripAnsi(plain), plain);
}

TEST(ansi, keeps_lone_bracket_after_escape_free_text)
{
    // Kein ESC davor -> keine Sequenz, darf nicht verschwinden.
    CHECK_EQ(AnsiRenderer::stripAnsi(QStringLiteral("array[0m] = 5")),
             QStringLiteral("array[0m] = 5"));
}
