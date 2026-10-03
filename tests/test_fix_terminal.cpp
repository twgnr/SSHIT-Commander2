// Regressionstests fuer die Terminal-Fehlerrunde: Ueberlaeufe im Emulator
// (riesige CSI-Parameter -> Heap-Schreibzugriffe), OSC/Zeichensatz-Sequenzen
// im Zeilen-Renderer und der Verlauf, der von mehreren Instanzen geschrieben
// wird.
#include "tests/harness.hpp"

#include "ncssh/config.hpp"
#include "ncssh/core/history.hpp"
#include "ncssh/core/terminal_emulator.hpp"
#include "ncssh/gui/ansi.hpp"

#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextDocument>

using ncssh::core::HistoryStore;
using ncssh::core::TerminalEmulator;
using ncssh::gui::AnsiRenderer;

namespace {

bool cursorInRange(const TerminalEmulator &e)
{
    return e.cursorRow() >= 0 && e.cursorRow() < e.rows() && e.cursorCol() >= 0
           && e.cursorCol() < e.cols();
}

// Lenkt configDir() waehrend des Tests auf ein frisches Verzeichnis um.
class FixTermConfigDirGuard
{
public:
    FixTermConfigDirGuard() : m_appdata(qgetenv("APPDATA")), m_xdg(qgetenv("XDG_CONFIG_HOME"))
    {
        qputenv("APPDATA", m_dir.path().toLocal8Bit());
        qputenv("XDG_CONFIG_HOME", m_dir.path().toLocal8Bit());
    }
    ~FixTermConfigDirGuard()
    {
        qputenv("APPDATA", m_appdata);
        qputenv("XDG_CONFIG_HOME", m_xdg);
    }
    bool isValid() const { return m_dir.isValid(); }

private:
    QTemporaryDir m_dir;
    QByteArray m_appdata;
    QByteArray m_xdg;
};

} // namespace

// --- Emulator: riesige Parameter -------------------------------------------

TEST(fix_terminal, huge_cud_then_erase_keeps_cursor_in_range)
{
    TerminalEmulator e(20, 5);
    // m_cy + 2147483647 lief frueher ueber -> negativer Cursor -> EL schrieb
    // ausserhalb des Gitters.
    e.feed(QStringLiteral("\x1b[?1049h\x1b[2147483647B\x1b[K"));
    CHECK(cursorInRange(e));
    CHECK_EQ(e.cursorRow(), 4);
    e.feed(QStringLiteral("\x1b[2147483647E\x1b[J\x1b[2147483647A\x1b[1K"));
    CHECK(cursorInRange(e));
    CHECK_EQ(e.cursorRow(), 0);
}

TEST(fix_terminal, huge_cuf_then_char_edits_stay_in_range)
{
    TerminalEmulator e(20, 5);
    e.feed(QStringLiteral("abc\x1b[2147483647C\x1b[2147483647@\x1b[2147483647P"
                          "\x1b[2147483647X\x1b[2147483647L\x1b[2147483647M"
                          "\x1b[2147483647S\x1b[2147483647T"));
    CHECK(cursorInRange(e));
    CHECK_EQ(e.cursorCol(), 19);
    e.feed(QStringLiteral("\x1b[2147483647;2147483647H\x1b[2147483647;2147483647rZ"));
    CHECK(cursorInRange(e));
    e.feed(QStringLiteral("\x1b[99999999999999999999;99999999999999999999HQ"));
    CHECK(cursorInRange(e));
    CHECK_EQ(e.cell(4, 19).ch, U'Q');
}

TEST(fix_terminal, endless_csi_parameters_are_bounded)
{
    TerminalEmulator e(20, 5);
    e.feed(QStringLiteral("\x1b[") + QString(100000, QLatin1Char('1')) + QStringLiteral("mok"));
    CHECK(cursorInRange(e));
    CHECK_EQ(e.cell(0, 0).ch, U'o');
    CHECK_EQ(e.cell(0, 1).ch, U'k');
}

TEST(fix_terminal, resize_in_alt_screen_then_leave_is_safe)
{
    TerminalEmulator e(10, 3);
    e.feed(QStringLiteral("hi\x1b[?1049h"));
    e.resize(40, 3);
    e.feed(QStringLiteral("\x1b[?1049l"));
    // Gesicherter Primaerschirm hatte frueher noch 10 Spalten.
    e.feed(QStringLiteral("\x1b[1;35HZ"));
    CHECK_EQ(e.cell(0, 34).ch, U'Z');
    CHECK_EQ(e.cell(0, 0).ch, U'h');
    CHECK(e.screenText().startsWith(QStringLiteral("hi")));
}

TEST(fix_terminal, truecolor_out_of_range_is_clamped)
{
    TerminalEmulator e(10, 3);
    e.feed(QStringLiteral("\x1b[38;2;999;999;999mA\x1b[38;5;2147483647mB"));
    CHECK(e.cell(0, 0).fg.isValid());
    CHECK(e.cell(0, 1).fg.isValid());
}

TEST(fix_terminal, cud_below_scroll_region_moves_down)
{
    TerminalEmulator e(10, 10);
    e.feed(QStringLiteral("\x1b[2;5r\x1b[8;1H\x1b[B"));
    CHECK_EQ(e.cursorRow(), 8);  // frueher zurueck auf das Regionsende (4)
}

// --- Zeilen-Renderer --------------------------------------------------------

TEST(fix_terminal, osc_with_st_terminator_keeps_following_text)
{
    QPlainTextEdit edit;
    AnsiRenderer r(&edit);
    r.feed(QStringLiteral("\x1b]0;mein titel\x1b\\hallo"));
    CHECK_EQ(edit.toPlainText(), QStringLiteral("hallo"));
}

TEST(fix_terminal, osc_split_across_chunks)
{
    QPlainTextEdit edit;
    AnsiRenderer r(&edit);
    r.feed(QStringLiteral("a\x1b]0;tit"));
    r.feed(QStringLiteral("el\x07welt"));
    CHECK_EQ(edit.toPlainText(), QStringLiteral("awelt"));
    // ST mit ESC und Backslash in verschiedenen Chunks.
    r.feed(QStringLiteral("\x1b]2;t\x1b"));
    r.feed(QStringLiteral("\\!"));
    CHECK_EQ(edit.toPlainText(), QStringLiteral("awelt!"));
}

TEST(fix_terminal, charset_designation_prints_nothing)
{
    QPlainTextEdit edit;
    AnsiRenderer r(&edit);
    // 'tput sgr0' liefert ESC ( B — frueher blieb ein "B" stehen.
    r.feed(QStringLiteral("a\x1b(Bb\x1b)0c\x1b[m"));
    CHECK_EQ(edit.toPlainText(), QStringLiteral("abc"));
}

TEST(fix_terminal, escape_split_at_chunk_end)
{
    QPlainTextEdit edit;
    AnsiRenderer r(&edit);
    // Einzelnes ESC am Ende liess die Schleife frueher nie fortschreiten.
    r.feed(QStringLiteral("x\x1b"));
    r.feed(QStringLiteral("[31mrot"));
    r.feed(QStringLiteral("\x1b[3"));
    r.feed(QStringLiteral("9m!\x1b("));
    r.feed(QStringLiteral("B?"));
    CHECK_EQ(edit.toPlainText(), QStringLiteral("xrot!?"));
}

TEST(fix_terminal, endless_line_is_broken_into_blocks)
{
    QPlainTextEdit edit;
    AnsiRenderer r(&edit);
    r.feed(QString(20000, QLatin1Char('x')));
    CHECK(edit.document()->blockCount() >= 3);
    bool allShort = true;
    for (QTextBlock b = edit.document()->begin(); b.isValid(); b = b.next())
        allShort = allShort && b.length() <= 8193;
    CHECK(allShort);
}

// --- Verlauf mit mehreren Instanzen -----------------------------------------

TEST(fix_terminal, history_instances_do_not_overwrite_each_other)
{
    FixTermConfigDirGuard guard;
    CHECK(guard.isValid());

    HistoryStore console;  // Konsole: frueh geladen, danach veraltet
    HistoryStore dialog;   // Verlaufsdialog
    dialog.addFavorite(QStringLiteral("ls -la"));
    console.add(QStringLiteral("uptime"));  // darf den Favoriten nicht verlieren

    HistoryStore check;
    CHECK(check.favorites().contains(QStringLiteral("ls -la")));
    CHECK(check.history().contains(QStringLiteral("uptime")));

    // Leeren im Dialog, danach Befehl aus einer anderen Konsole.
    HistoryStore other;
    dialog.clearHistory();
    other.addFavorite(QStringLiteral("df -h"));
    HistoryStore check2;
    CHECK(check2.history().isEmpty());
    CHECK(check2.favorites().contains(QStringLiteral("ls -la")));
    CHECK(check2.favorites().contains(QStringLiteral("df -h")));
}
