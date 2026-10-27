// Bildschirmmodell des Terminal-Renderers (Primaerschirm): absolute Cursor-
// positionen, Cursor hoch/runter und Bildschirm loeschen. Anlass: lokale
// PowerShell (ConPTY) zeichnet die Eingabe nach "cd " mit ESC[1;36H neu — der
// Renderer ignorierte das und haengte an: aus "cd " wurde "cdcd ".
// Die Eingaben unten sind echte ConPTY-Mitschnitte (Windows 11, PowerShell
// 5.1/PSReadLine 2.0 bzw. cmd).
#include "tests/harness.hpp"

#include "ncssh/gui/ansi.hpp"

#include <QPlainTextEdit>
#include <QTextBlock>

using ncssh::gui::AnsiRenderer;

namespace {

QStringList blocks(const QPlainTextEdit &edit)
{
    QStringList out;
    for (QTextBlock b = edit.document()->begin(); b.isValid(); b = b.next())
        out << b.text();
    return out;
}

// Kopien statt Referenzen: CHECK_EQ bindet eine Referenz, und first()/last()
// einer temporaeren Liste zeigten danach auf freigegebenen Speicher.
QString firstLine(const QPlainTextEdit &edit)
{
    return edit.document()->firstBlock().text();
}

QString lastLine(const QPlainTextEdit &edit)
{
    return edit.document()->lastBlock().text();
}

} // namespace

TEST(terminal_screen, powershell_redraw_does_not_duplicate_input)
{
    QPlainTextEdit edit;
    AnsiRenderer r(&edit);
    r.setScreenSize(100, 30);
    // Vorher stand schon etwas im Terminal (z. B. eine alte Sitzung).
    r.feed(QStringLiteral("[Shell beendet]\r\n"));
    r.feed(QStringLiteral("\x1b[?9001h\x1b[?1004h\x1b[?25l\x1b[2J\x1b[m\x1b[H"
                          "PS C:\\Repository\\SSHIT-Commander2>\x1b[1C"
                          "\x1b]0;C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe\x07"
                          "\x1b[?25h"));
    r.feed(QStringLiteral("\x1b[?25l\x1b[93mc\x1b[?25h"));
    r.feed(QStringLiteral("\x1b[m\x1b[?25l\x1b[93m\bcd\x1b[?25h"));
    r.feed(QStringLiteral("\x1b[m\x1b[?25l\x1b[93m\x1b[1;36Hcd \x1b[?25h"));   // Leerzeichen
    CHECK_EQ(lastLine(edit), QStringLiteral("PS C:\\Repository\\SSHIT-Commander2> cd "));
    r.feed(QStringLiteral("\x1b[m\x1b[?25l\x1b[93m\x1b[1;36Hcd \b\x1b[?25h"));  // Backspace
    const QStringList lines = blocks(edit);
    CHECK_EQ(lines.value(lines.size() - 1), QStringLiteral("PS C:\\Repository\\SSHIT-Commander2> cd "));
    CHECK_EQ(lines.value(0), QStringLiteral("[Shell beendet]"));   // alter Inhalt bleibt oben
    CHECK(!edit.toPlainText().contains(QStringLiteral("cdcd")));
}

TEST(terminal_screen, cmd_prompt_lands_on_its_own_row)
{
    QPlainTextEdit edit;
    AnsiRenderer r(&edit);
    r.setScreenSize(100, 30);
    r.feed(QStringLiteral("\x1b[?9001h\x1b[?1004h\x1b[?25l\x1b[2J\x1b[m\x1b[H"
                          "Microsoft Windows [Version 10.0.26200.9711]"
                          "\x1b]0;C:\\WINDOWS\\system32\\cmd.exe\x07\x1b[?25h\x1b[?25l\r\n"
                          "(c) Microsoft Corporation. Alle Rechte vorbehalten."
                          "\x1b[4;1HC:\\Repository\\SSHIT-Commander2>\x1b[?25h"));
    r.feed(QStringLiteral("cd "));
    r.feed(QStringLiteral("\b \b"));
    CHECK_EQ(blocks(edit),
             (QStringList{QStringLiteral("Microsoft Windows [Version 10.0.26200.9711]"),
                          QStringLiteral("(c) Microsoft Corporation. Alle Rechte vorbehalten."),
                          QString(), QStringLiteral("C:\\Repository\\SSHIT-Commander2>cd ")}));
}

TEST(terminal_screen, rows_count_wrapped_lines_like_conpty)
{
    // ConPTY bricht lange Zeilen ohne Zeilenumbruch um: 30 Zeichen bei 20
    // Spalten belegen ZWEI Bildschirmzeilen, der Prompt steht in Zeile 3.
    QPlainTextEdit edit;
    AnsiRenderer r(&edit);
    r.setScreenSize(20, 5);
    r.feed(QStringLiteral("\x1b[2J\x1b[H") + QString(30, QLatin1Char('A'))
           + QStringLiteral("\r\nPS>"));
    r.feed(QStringLiteral("\x1b[3;4Hx"));
    CHECK_EQ(blocks(edit), (QStringList{QString(30, QLatin1Char('A')), QStringLiteral("PS>x")}));
    // Zeile 2 ist die Fortsetzung der langen Zeile: Spalte 1 dort = Zeichen 21.
    r.feed(QStringLiteral("\x1b[2;1HB"));
    CHECK_EQ(firstLine(edit), QString(20, QLatin1Char('A')) + QStringLiteral("B")
                                       + QString(9, QLatin1Char('A')));
    // Wagenruecklauf geht an den Anfang der SICHTBAREN Zeile.
    r.feed(QStringLiteral("\rC"));
    CHECK_EQ(firstLine(edit), QString(20, QLatin1Char('A')) + QStringLiteral("C")
                                       + QString(9, QLatin1Char('A')));
}

TEST(terminal_screen, screen_scrolls_with_output)
{
    QPlainTextEdit edit;
    AnsiRenderer r(&edit);
    r.setScreenSize(80, 3);
    r.feed(QStringLiteral("1\r\n2\r\n3\r\n4\r\n5"));
    r.feed(QStringLiteral("\x1b[1;1HX"));   // oberste Bildschirmzeile = "3"
    CHECK_EQ(blocks(edit), (QStringList{QStringLiteral("1"), QStringLiteral("2"),
                                        QStringLiteral("X"), QStringLiteral("4"),
                                        QStringLiteral("5")}));
    // Weit unterhalb: auf die unterste Zeile begrenzt.
    r.feed(QStringLiteral("\x1b[99;1HY"));
    CHECK_EQ(lastLine(edit), QStringLiteral("Y"));
    CHECK_EQ(int(blocks(edit).size()), 5);
}

TEST(terminal_screen, scrollback_limit_keeps_screen_mapping)
{
    // setMaximumBlockCount schneidet oben Zeilen ab — die Zuordnung der
    // Bildschirmzeilen darf dadurch nicht verrutschen.
    QPlainTextEdit edit;
    edit.setMaximumBlockCount(4);
    AnsiRenderer r(&edit);
    r.setScreenSize(80, 3);
    r.feed(QStringLiteral("1\r\n2\r\n3\r\n4\r\n5\r\n6\r\n7\r\n8"));
    r.feed(QStringLiteral("\x1b[1;1HX"));
    CHECK_EQ(blocks(edit), (QStringList{QStringLiteral("5"), QStringLiteral("X"),
                                        QStringLiteral("7"), QStringLiteral("8")}));
}

TEST(terminal_screen, cursor_up_down_and_line_moves)
{
    QPlainTextEdit edit;
    AnsiRenderer r(&edit);
    r.setScreenSize(80, 10);
    r.feed(QStringLiteral("aa\r\nbb\r\ncc"));
    r.feed(QStringLiteral("\x1b[2AX"));      // zwei hoch, Spalte bleibt (2)
    r.feed(QStringLiteral("\x1b[1BY"));      // eins runter, Spalte jetzt 3
    r.feed(QStringLiteral("\x1b[1EZ"));      // naechste Zeile, Spaltenanfang
    r.feed(QStringLiteral("\x1b[3FW"));      // drei hoch (begrenzt auf Zeile 1), Spaltenanfang
    CHECK_EQ(blocks(edit), (QStringList{QStringLiteral("WaX"), QStringLiteral("bb Y"),
                                        QStringLiteral("Zc")}));
    // Neuer Text danach geht wieder ans Ende: Zeilenumbruch nur bis zur letzten Zeile.
    r.feed(QStringLiteral("\x1b[3;3H\r\nneu"));
    CHECK_EQ(lastLine(edit), QStringLiteral("neu"));
}

TEST(terminal_screen, erase_display_modes)
{
    QPlainTextEdit edit;
    AnsiRenderer r(&edit);
    r.setScreenSize(80, 10);
    r.feed(QStringLiteral("alt\r\n"));
    // 2: neuer leerer Bildschirm, der alte Inhalt bleibt im Rollpuffer.
    r.feed(QStringLiteral("\x1b[2J\x1b[Hl1\r\nl2\r\nl3"));
    CHECK_EQ(blocks(edit), (QStringList{QStringLiteral("alt"), QStringLiteral("l1"),
                                        QStringLiteral("l2"), QStringLiteral("l3")}));
    // 0: ab Cursor bis Bildschirmende.
    r.feed(QStringLiteral("\x1b[2;2H\x1b[J"));
    CHECK_EQ(blocks(edit), (QStringList{QStringLiteral("alt"), QStringLiteral("l1"),
                                        QStringLiteral("l")}));
    // 1: Bildschirmanfang bis einschliesslich Cursor.
    r.feed(QStringLiteral("2\r\nl3\x1b[2;1H\x1b[1J"));
    CHECK_EQ(blocks(edit), (QStringList{QStringLiteral("alt"), QString(), QStringLiteral(" 2"),
                                        QStringLiteral("l3")}));
    // 3: Rollpuffer (alles oberhalb des Bildschirms) weg — z. B. clear/cls.
    r.feed(QStringLiteral("\x1b[3J"));
    CHECK_EQ(blocks(edit), (QStringList{QString(), QStringLiteral(" 2"), QStringLiteral("l3")}));
}

TEST(terminal_screen, unknown_size_keeps_line_model)
{
    // Ohne Groessenangabe (z. B. Befehlsausgabe ohne PTY): kein Umbruch-
    // Zaehlen, kein Mitrollen; einfache Ausgabe wie bisher.
    QPlainTextEdit edit;
    AnsiRenderer r(&edit);
    r.feed(QStringLiteral("eins\r\nzwei\rZ"));
    CHECK_EQ(blocks(edit), (QStringList{QStringLiteral("eins"), QStringLiteral("Zwei")}));
}
