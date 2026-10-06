// Terminal-Emulation auf PuTTY-Niveau: DEC-Liniengrafik (ncurses-Rahmen),
// breite Zeichen (CJK/Emoji), kombinierende Akzente, Maus-Reporting und
// Bracketed Paste — im Zellengitter, im Zeilen-Renderer und im Widget.
#include "tests/harness.hpp"

#include "ncssh/core/terminal_emulator.hpp"
#include "ncssh/gui/ansi.hpp"
#include "ncssh/gui/bridge.hpp"
#include "ncssh/gui/style.hpp"
#include "ncssh/gui/terminal_widget.hpp"

#include <QApplication>
#include <QPlainTextEdit>
#include <QStyle>

using namespace ncssh;
using core::MouseAction;
using core::MouseTracking;
using core::TerminalEmulator;
using core::TerminalModes;

namespace {

QString rowText(const TerminalEmulator &e, int row)
{
    return e.screenText().split(QLatin1Char('\n')).value(row);
}

QByteArray legacyMouse(int code, int col, int row)
{
    QByteArray out = QByteArrayLiteral("\x1b[M");
    out += static_cast<char>(32 + code);
    out += static_cast<char>(32 + col + 1);
    out += static_cast<char>(32 + row + 1);
    return out;
}

} // namespace

// --- Zeichenbreite ------------------------------------------------------------

TEST(terminal_emulation, char_width_matches_wcwidth)
{
    CHECK_EQ(core::charWidth(U'a'), 1);
    CHECK_EQ(core::charWidth(U'ä'), 1);
    CHECK_EQ(core::charWidth(U'─'), 1);
    CHECK_EQ(core::charWidth(U'中'), 2);
    CHECK_EQ(core::charWidth(U'한'), 2);
    CHECK_EQ(core::charWidth(U'！'), 2);       // Vollbreite-Ausrufezeichen
    CHECK_EQ(core::charWidth(U'\U0001F600'), 2);  // 😀
    CHECK_EQ(core::charWidth(U'́'), 0);   // kombinierender Akut
    CHECK_EQ(core::charWidth(U'‍'), 0);   // Emoji-Verbinder
    CHECK_EQ(core::textWidth(QStringLiteral("a中\U0001F600")), 5);
}

// --- DEC-Liniengrafik -----------------------------------------------------------

TEST(terminal_emulation, dec_graphics_draws_box_in_grid)
{
    TerminalEmulator e(10, 3);
    e.feed(QStringLiteral("\x1b(0lqk\x1b(Bq"));
    CHECK_EQ(rowText(e, 0), QStringLiteral("┌─┐q"));
}

TEST(terminal_emulation, dec_graphics_via_g1_and_shift_out)
{
    // ncurses-Variante mit G1: ESC ) 0, dann SO/SI.
    TerminalEmulator e(10, 3);
    e.feed(QStringLiteral("\x1b)0x\x0ex\x0fx"));
    CHECK_EQ(rowText(e, 0), QStringLiteral("x│x"));
}

TEST(terminal_emulation, dec_graphics_survives_save_restore_and_reset)
{
    TerminalEmulator e(10, 3);
    e.feed(QStringLiteral("\x1b(0\x1b" "7\x1b(B\x1b" "8q"));  // DECSC sichert den Zeichensatz
    CHECK_EQ(rowText(e, 0), QStringLiteral("─"));
    e.feed(QStringLiteral("\x1b" "c" "q"));                // RIS: zurueck auf ASCII
    CHECK_EQ(rowText(e, 0), QStringLiteral("q"));
}

TEST(terminal_emulation, dec_graphics_in_line_renderer)
{
    QPlainTextEdit edit;
    gui::AnsiRenderer r(&edit);
    r.feed(QStringLiteral("\x1b(0mqj\x1b(B ok"));
    CHECK_EQ(edit.toPlainText(), QStringLiteral("└─┘ ok"));
    r.feed(QStringLiteral("\r\n\x1b)0a\x0ex\x0f"));
    CHECK_EQ(edit.toPlainText(), QStringLiteral("└─┘ ok\na│"));
}

// --- Breite Zeichen im Zellengitter -------------------------------------------

TEST(terminal_emulation, wide_char_occupies_two_cells)
{
    TerminalEmulator e(10, 3);
    e.feed(QStringLiteral("中a"));
    CHECK(e.cell(0, 0).ch == U'中');
    CHECK(e.cell(0, 0).attrs & core::AttrWide);
    CHECK(e.cell(0, 1).ch == core::kWideTail);
    CHECK(e.cell(0, 2).ch == U'a');
    CHECK_EQ(e.cursorCol(), 3);
    CHECK_EQ(rowText(e, 0), QStringLiteral("中a"));
}

TEST(terminal_emulation, emoji_outside_bmp_is_kept_intact)
{
    TerminalEmulator e(10, 3);
    e.feed(QStringLiteral("\U0001F600!"));
    CHECK(e.cell(0, 0).ch == U'\U0001F600');
    CHECK_EQ(e.cursorCol(), 3);
    CHECK_EQ(rowText(e, 0), QStringLiteral("\U0001F600!"));
}

TEST(terminal_emulation, wide_char_wraps_instead_of_splitting)
{
    TerminalEmulator e(5, 3);
    e.feed(QStringLiteral("abcd中"));
    CHECK_EQ(rowText(e, 0), QStringLiteral("abcd"));
    CHECK(e.cell(1, 0).ch == U'中');
    CHECK_EQ(e.cursorRow(), 1);
    CHECK_EQ(e.cursorCol(), 2);
}

TEST(terminal_emulation, overwriting_half_clears_the_wide_char)
{
    TerminalEmulator e(10, 3);
    e.feed(QStringLiteral("中文"));
    e.feed(QStringLiteral("\x1b[1;2Hx"));   // rechte Haelfte von 中
    CHECK_EQ(rowText(e, 0), QStringLiteral(" x文"));
    e.feed(QStringLiteral("\x1b[1;3Hy"));   // linke Haelfte von 文
    CHECK_EQ(rowText(e, 0), QStringLiteral(" xy"));
}

TEST(terminal_emulation, delete_char_does_not_leave_half_wide_chars)
{
    TerminalEmulator e(10, 3);
    e.feed(QStringLiteral("a中b\x1b[1;1H\x1b[2P"));  // loescht 'a' und die linke Haelfte
    CHECK(e.cell(0, 0).ch != core::kWideTail);
    CHECK_EQ(rowText(e, 0).trimmed(), QStringLiteral("b"));
}

TEST(terminal_emulation, combining_accent_merges_with_base)
{
    TerminalEmulator e(10, 3);
    e.feed(QStringLiteral("éx"));
    CHECK(e.cell(0, 0).ch == U'é');
    CHECK(e.cell(0, 1).ch == U'x');
    CHECK_EQ(e.cursorCol(), 2);
}

// --- Breite Zeichen im Zeilen-Renderer ----------------------------------------

TEST(terminal_emulation, line_renderer_counts_display_columns)
{
    // readline loescht ein CJK-Zeichen mit ZWEI Rueckschritten. Frueher zaehlte
    // der Renderer Zeichen statt Spalten und loeschte dabei auch das erste.
    QPlainTextEdit edit;
    gui::AnsiRenderer r(&edit);
    r.feed(QStringLiteral("中文"));
    r.feed(QStringLiteral("\b\b\x1b[K"));
    CHECK_EQ(edit.toPlainText(), QStringLiteral("中"));
    r.feed(QStringLiteral("x"));
    CHECK_EQ(edit.toPlainText(), QStringLiteral("中x"));
}

TEST(terminal_emulation, line_renderer_cursor_moves_over_wide_chars)
{
    QPlainTextEdit edit;
    gui::AnsiRenderer r(&edit);
    r.feed(QStringLiteral("日本語 ok"));
    r.feed(QStringLiteral("\x1b[8G!"));   // Spalte 8 = das 'o'
    CHECK_EQ(edit.toPlainText(), QStringLiteral("日本語 !k"));
}

TEST(terminal_emulation, line_renderer_keeps_combining_marks)
{
    QPlainTextEdit edit;
    gui::AnsiRenderer r(&edit);
    r.feed(QStringLiteral("äb"));
    r.feed(QStringLiteral("\b!"));
    CHECK_EQ(edit.toPlainText(), QStringLiteral("ä!"));
}

// --- Maus-Reporting ---------------------------------------------------------------

TEST(terminal_emulation, mouse_modes_follow_decset)
{
    TerminalModes m;
    CHECK(m.apply(1000, true));
    CHECK(m.mouse == MouseTracking::Normal);
    CHECK(m.apply(1006, true));
    CHECK(m.sgrMouse);
    CHECK(m.apply(1002, true));
    CHECK(m.mouse == MouseTracking::ButtonEvent);
    CHECK(m.apply(1000, false));   // wie xterm: jedes Zuruecksetzen schaltet ab
    CHECK(m.mouse == MouseTracking::Off);
    CHECK(!m.apply(25, true));
}

TEST(terminal_emulation, sgr_mouse_encoding)
{
    TerminalModes m;
    m.mouse = MouseTracking::Normal;
    m.sgrMouse = true;
    CHECK_EQ(core::encodeMouse(m, MouseAction::Press, 0, 4, 2, Qt::NoModifier),
             QByteArray("\x1b[<0;5;3M"));
    CHECK_EQ(core::encodeMouse(m, MouseAction::Release, 2, 4, 2, Qt::NoModifier),
             QByteArray("\x1b[<2;5;3m"));
    CHECK_EQ(core::encodeMouse(m, MouseAction::WheelUp, 0, 0, 0, Qt::NoModifier),
             QByteArray("\x1b[<64;1;1M"));
    CHECK_EQ(core::encodeMouse(m, MouseAction::Press, 0, 299, 99, Qt::ControlModifier),
             QByteArray("\x1b[<16;300;100M"));   // kein 223-Spalten-Limit
}

TEST(terminal_emulation, legacy_mouse_encoding)
{
    TerminalModes m;
    m.mouse = MouseTracking::Normal;
    CHECK_EQ(core::encodeMouse(m, MouseAction::Press, 0, 4, 2, Qt::NoModifier),
             legacyMouse(0, 4, 2));
    // Loslassen meldet klassisch Taste 3, Modifikatoren bleiben erhalten.
    CHECK_EQ(core::encodeMouse(m, MouseAction::Release, 0, 4, 2, Qt::ShiftModifier),
             legacyMouse(3 + 4, 4, 2));
    CHECK_EQ(core::encodeMouse(m, MouseAction::WheelDown, 0, 0, 0, Qt::NoModifier),
             legacyMouse(65, 0, 0));
    // Bytes ueber 127 bleiben rohe Bytes (nicht UTF-8).
    const QByteArray far = core::encodeMouse(m, MouseAction::Press, 0, 150, 0, Qt::NoModifier);
    CHECK_EQ(far.size(), qsizetype(6));
    CHECK_EQ(int(static_cast<unsigned char>(far.at(4))), 32 + 151);
}

TEST(terminal_emulation, mouse_motion_depends_on_mode)
{
    TerminalModes m;
    m.sgrMouse = true;
    m.mouse = MouseTracking::Normal;
    CHECK(core::encodeMouse(m, MouseAction::Move, 0, 1, 1, Qt::NoModifier).isEmpty());
    m.mouse = MouseTracking::ButtonEvent;
    CHECK_EQ(core::encodeMouse(m, MouseAction::Move, 0, 1, 1, Qt::NoModifier),
             QByteArray("\x1b[<32;2;2M"));
    CHECK(core::encodeMouse(m, MouseAction::Move, 3, 1, 1, Qt::NoModifier).isEmpty());
    m.mouse = MouseTracking::AnyEvent;
    CHECK_EQ(core::encodeMouse(m, MouseAction::Move, 3, 1, 1, Qt::NoModifier),
             QByteArray("\x1b[<35;2;2M"));
    m.mouse = MouseTracking::X10;
    CHECK(core::encodeMouse(m, MouseAction::Release, 0, 1, 1, Qt::NoModifier).isEmpty());
    m.mouse = MouseTracking::Off;
    CHECK(core::encodeMouse(m, MouseAction::Press, 0, 1, 1, Qt::NoModifier).isEmpty());
}

// --- Bracketed Paste ----------------------------------------------------------------

TEST(terminal_emulation, paste_converts_line_endings)
{
    CHECK_EQ(core::preparePaste(QStringLiteral("a\r\nb\nc"), false),
             QStringLiteral("a\rb\rc"));
}

TEST(terminal_emulation, bracketed_paste_wraps_and_strips_end_marker)
{
    CHECK_EQ(core::preparePaste(QStringLiteral("ls\n"), true),
             QStringLiteral("\x1b[200~ls\r\x1b[201~"));
    // Eingeschmuggeltes Ende (auch verschachtelt) darf nicht durchkommen —
    // sonst liefe "rm -rf ~" als getippter Befehl.
    const QString evil = QStringLiteral("x\x1b[20\x1b[201~1~rm -rf ~\n");
    const QString out = core::preparePaste(evil, true);
    CHECK_EQ(out.count(QStringLiteral("\x1b[201~")), qsizetype(1));
    CHECK(out.endsWith(QStringLiteral("\x1b[201~")));
}

// --- Widget: Schrift ----------------------------------------------------------------

TEST(terminal_emulation, terminal_font_survives_app_stylesheet)
{
    // Das App-Theme setzt "* { font-family: Segoe UI; font-size: 13px }" und
    // ueberstimmte damit setFont(): das Terminal lief in einer Proportional-
    // schrift, das Zellengitter verrutschte, die Groessen-Einstellung wirkte nie.
    const QString oldSheet = qApp->styleSheet();
    const QPalette oldPalette = qApp->palette();
    const QString oldStyle = qApp->style()->name();
    gui::applyTheme(qApp, gui::defaultTheme());
    {
        gui::AsyncBridge bridge;
        gui::TerminalWidget terminal(&bridge);
        terminal.ensurePolished();
        CHECK_EQ(terminal.font().family(), QStringLiteral("Consolas"));
        CHECK_EQ(terminal.font().pointSize(), 10);
    }
    qApp->setStyleSheet(oldSheet);
    qApp->setPalette(oldPalette);
    QApplication::setStyle(oldStyle);
}

// --- Widget: Modi ueber beide Schirme verfolgen ----------------------------------

TEST(terminal_emulation, widget_tracks_modes_across_screens_and_chunks)
{
    gui::AsyncBridge bridge;
    gui::TerminalWidget terminal(&bridge);
    // bash schaltet Bracketed Paste schon am Prompt (Primaerschirm) ein.
    terminal.feedOutput(QStringLiteral("$ \x1b[?2004h"));
    CHECK(terminal.terminalModes().bracketedPaste);
    CHECK(!terminal.toPlainText().contains(QStringLiteral("2004")));

    // vim: Alternate-Screen + Maus in einer Sequenzfolge.
    terminal.feedOutput(QStringLiteral("\x1b[?1049h\x1b[?1000h\x1b[?1006h"));
    CHECK(terminal.terminalModes().mouse == MouseTracking::Normal);
    CHECK(terminal.terminalModes().sgrMouse);

    // Ueber eine Chunk-Grenze geteilte Sequenz.
    terminal.feedOutput(QStringLiteral("\x1b[?10"));
    terminal.feedOutput(QStringLiteral("00l\x1b[?1049l"));
    CHECK(terminal.terminalModes().mouse == MouseTracking::Off);
    terminal.feedOutput(QStringLiteral("\x1b[?2004l"));
    CHECK(!terminal.terminalModes().bracketedPaste);
}
