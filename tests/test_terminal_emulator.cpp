// Tests fuer den VT100/xterm-Zellengitter-Emulator (core::TerminalEmulator).
//
// Deckt Cursor-Adressierung, Loeschen, Autowrap, Scrollregionen, SGR-Farben
// und den Alternate-Screen ab — genau die Faehigkeiten, die dem alten
// AnsiRenderer fehlten (vim/htop/tmux/less).
#include "tests/harness.hpp"

#include "ncssh/core/terminal_emulator.hpp"

using ncssh::core::AttrBold;
using ncssh::core::AttrInverse;
using ncssh::core::TerminalEmulator;

static char32_t chAt(const TerminalEmulator &e, int r, int c)
{
    return e.cell(r, c).ch;
}

TEST(terminal_emulator, prints_text_and_advances_cursor)
{
    TerminalEmulator e(20, 5);
    e.feed(QStringLiteral("Hi"));
    CHECK_EQ(chAt(e, 0, 0), U'H');
    CHECK_EQ(chAt(e, 0, 1), U'i');
    CHECK_EQ(e.cursorCol(), 2);
    CHECK_EQ(e.cursorRow(), 0);
}

TEST(terminal_emulator, absolute_cursor_positioning)
{
    TerminalEmulator e(20, 10);
    e.feed(QStringLiteral("\x1b[3;5HX"));  // Zeile 3, Spalte 5 (1-basiert)
    CHECK_EQ(chAt(e, 2, 4), U'X');
    CHECK_EQ(e.cursorRow(), 2);
    CHECK_EQ(e.cursorCol(), 5);
}

TEST(terminal_emulator, carriage_return_overwrites)
{
    TerminalEmulator e(20, 5);
    e.feed(QStringLiteral("abc\rX"));
    CHECK_EQ(chAt(e, 0, 0), U'X');
    CHECK_EQ(chAt(e, 0, 1), U'b');
    CHECK_EQ(chAt(e, 0, 2), U'c');
}

TEST(terminal_emulator, newline_moves_down_keeps_column)
{
    TerminalEmulator e(20, 5);
    e.feed(QStringLiteral("a\r\nb"));
    CHECK_EQ(chAt(e, 0, 0), U'a');
    CHECK_EQ(chAt(e, 1, 0), U'b');
}

TEST(terminal_emulator, backspace_moves_left)
{
    TerminalEmulator e(20, 5);
    e.feed(QStringLiteral("ab\bX"));  // Cursor auf 'b' zurueck, dann ueberschreiben
    CHECK_EQ(chAt(e, 0, 1), U'X');
    CHECK_EQ(e.cursorCol(), 2);
}

TEST(terminal_emulator, erase_in_line_from_cursor)
{
    TerminalEmulator e(20, 5);
    e.feed(QStringLiteral("abcdef"));
    e.feed(QStringLiteral("\x1b[4D"));  // 4 zurueck -> Spalte 2
    e.feed(QStringLiteral("\x1b[K"));   // ab Cursor loeschen
    CHECK_EQ(chAt(e, 0, 0), U'a');
    CHECK_EQ(chAt(e, 0, 1), U'b');
    CHECK_EQ(chAt(e, 0, 2), U' ');
    CHECK_EQ(chAt(e, 0, 5), U' ');
}

TEST(terminal_emulator, erase_display_clears_everything)
{
    TerminalEmulator e(10, 4);
    e.feed(QStringLiteral("xxxx\r\nyyyy"));
    e.feed(QStringLiteral("\x1b[2J"));
    CHECK_EQ(chAt(e, 0, 0), U' ');
    CHECK_EQ(chAt(e, 1, 0), U' ');
}

TEST(terminal_emulator, autowrap_deferred_at_right_margin)
{
    TerminalEmulator e(3, 3);
    e.feed(QStringLiteral("abcd"));  // abc fuellt Zeile 0, d bricht auf Zeile 1 um
    CHECK_EQ(chAt(e, 0, 0), U'a');
    CHECK_EQ(chAt(e, 0, 2), U'c');
    CHECK_EQ(chAt(e, 1, 0), U'd');
    CHECK_EQ(e.cursorRow(), 1);
    CHECK_EQ(e.cursorCol(), 1);
}

TEST(terminal_emulator, newline_at_bottom_scrolls_region)
{
    TerminalEmulator e(4, 3);
    e.feed(QStringLiteral("L1\r\nL2\r\nL3"));  // Cursor in letzter Zeile
    e.feed(QStringLiteral("\r\n"));            // LF am unteren Rand -> hochscrollen
    CHECK_EQ(chAt(e, 0, 0), U'L');
    CHECK_EQ(chAt(e, 0, 1), U'2');
    CHECK_EQ(chAt(e, 1, 1), U'3');
    CHECK_EQ(chAt(e, 2, 0), U' ');  // neue Leerzeile unten
}

TEST(terminal_emulator, scroll_region_limits_scrolling)
{
    TerminalEmulator e(4, 4);
    // Scrollregion auf Zeilen 1..2 (1-basiert 1;2 -> 0-basiert 0..1? nein: 2;3)
    e.feed(QStringLiteral("\x1b[2;3r"));  // Region = Zeilen 2..3 (0-basiert 1..2)
    e.feed(QStringLiteral("\x1b[2;1HA")); // in Zeile 2 schreiben
    e.feed(QStringLiteral("\r\nB"));      // Zeile 3
    e.feed(QStringLiteral("\r\nC"));      // LF am Regionsende -> Region scrollt
    // Zeile 0 unberuehrt (ausserhalb der Region), Region zeigt B dann C.
    CHECK_EQ(chAt(e, 1, 0), U'B');
    CHECK_EQ(chAt(e, 2, 0), U'C');
}

TEST(terminal_emulator, insert_and_delete_chars)
{
    TerminalEmulator e(10, 3);
    e.feed(QStringLiteral("abcde"));
    e.feed(QStringLiteral("\x1b[3G"));   // Spalte 3 (-> 'c')
    e.feed(QStringLiteral("\x1b[2@"));   // 2 Zeichen einfuegen
    CHECK_EQ(chAt(e, 0, 0), U'a');
    CHECK_EQ(chAt(e, 0, 1), U'b');
    CHECK_EQ(chAt(e, 0, 2), U' ');
    CHECK_EQ(chAt(e, 0, 4), U'c');
    e.feed(QStringLiteral("\x1b[2P"));   // 2 Zeichen loeschen -> zurueck
    CHECK_EQ(chAt(e, 0, 2), U'c');
}

TEST(terminal_emulator, sgr_sets_colors_and_attrs)
{
    TerminalEmulator e(10, 3);
    e.feed(QStringLiteral("\x1b[1;31mA\x1b[0mB"));
    const auto &a = e.cell(0, 0);
    CHECK(a.attrs & AttrBold);
    CHECK_EQ(a.fg.name(), QStringLiteral("#cc0000"));
    const auto &b = e.cell(0, 1);
    CHECK_EQ(int(b.attrs), 0);
    CHECK(!b.fg.isValid());  // Standardfarbe nach Reset
}

TEST(terminal_emulator, sgr_truecolor_and_inverse)
{
    TerminalEmulator e(10, 3);
    e.feed(QStringLiteral("\x1b[7;38;2;10;20;30mZ"));
    const auto &z = e.cell(0, 0);
    CHECK(z.attrs & AttrInverse);
    CHECK_EQ(z.fg.red(), 10);
    CHECK_EQ(z.fg.green(), 20);
    CHECK_EQ(z.fg.blue(), 30);
}

TEST(terminal_emulator, alternate_screen_saves_and_restores)
{
    TerminalEmulator e(20, 5);
    e.feed(QStringLiteral("hello"));
    e.feed(QStringLiteral("\x1b[?1049h"));  // Alt-Screen an
    CHECK_EQ(chAt(e, 0, 0), U' ');     // geleert
    e.feed(QStringLiteral("ALT"));
    CHECK_EQ(chAt(e, 0, 0), U'A');
    e.feed(QStringLiteral("\x1b[?1049l"));  // zurueck zum Primaerschirm
    CHECK_EQ(chAt(e, 0, 0), U'h');
    CHECK_EQ(chAt(e, 0, 4), U'o');
    CHECK_EQ(e.cursorCol(), 5);              // Cursor wiederhergestellt
}

TEST(terminal_emulator, osc_sets_window_title)
{
    TerminalEmulator e(20, 3);
    e.feed(QStringLiteral("\x1b]0;mein titel\x07rest"));
    CHECK_EQ(e.title(), QStringLiteral("mein titel"));
    CHECK_EQ(chAt(e, 0, 0), U'r');  // Text nach OSC normal gedruckt
}

TEST(terminal_emulator, dectcem_toggles_cursor_visibility)
{
    TerminalEmulator e(10, 3);
    CHECK(e.cursorVisible());
    e.feed(QStringLiteral("\x1b[?25l"));
    CHECK(!e.cursorVisible());
    e.feed(QStringLiteral("\x1b[?25h"));
    CHECK(e.cursorVisible());
}

TEST(terminal_emulator, application_cursor_keys_mode)
{
    TerminalEmulator e(10, 3);
    CHECK(!e.applicationCursorKeys());
    e.feed(QStringLiteral("\x1b[?1h"));
    CHECK(e.applicationCursorKeys());
    e.feed(QStringLiteral("\x1b[?1l"));
    CHECK(!e.applicationCursorKeys());
}

TEST(terminal_emulator, split_escape_across_feeds)
{
    TerminalEmulator e(20, 5);
    e.feed(QStringLiteral("\x1b[3;"));   // Sequenz endet mitten drin
    e.feed(QStringLiteral("5HX"));       // Fortsetzung
    CHECK_EQ(chAt(e, 2, 4), U'X');
}

TEST(terminal_emulator, insert_and_delete_lines)
{
    TerminalEmulator e(6, 4);
    e.feed(QStringLiteral("AAAA\r\nBBBB\r\nCCCC"));
    e.feed(QStringLiteral("\x1b[1;1H"));  // nach oben links
    e.feed(QStringLiteral("\x1b[1L"));    // Leerzeile einfuegen
    CHECK_EQ(chAt(e, 0, 0), U' ');
    CHECK_EQ(chAt(e, 1, 0), U'A');
    e.feed(QStringLiteral("\x1b[1M"));    // Zeile loeschen -> A wieder oben
    CHECK_EQ(chAt(e, 0, 0), U'A');
}

TEST(terminal_emulator, resize_preserves_top_left)
{
    TerminalEmulator e(10, 5);
    e.feed(QStringLiteral("keep"));
    e.resize(20, 8);
    CHECK_EQ(chAt(e, 0, 0), U'k');
    CHECK_EQ(e.cols(), 20);
    CHECK_EQ(e.rows(), 8);
}
