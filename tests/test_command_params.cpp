// Parameter-Hilfe der Konsole (Befehl erkennen, --help auswerten, Argumente
// in Befehls-Reihenfolge) und das helle Terminal-Theme.
#include "tests/harness.hpp"

#include "ncssh/core/command_params.hpp"
#include "ncssh/gui/ansi.hpp"
#include "ncssh/gui/bridge.hpp"
#include "ncssh/gui/command_builder.hpp"
#include "ncssh/gui/console_panel.hpp"
#include "ncssh/gui/param_dialog.hpp"
#include "ncssh/gui/style.hpp"
#include "ncssh/gui/terminal_widget.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDebug>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStyle>
#include <QTextBlock>
#include <QThread>
#include <QTreeWidget>

using namespace ncssh;

namespace {

template <typename Predicate>
bool pump(Predicate ready, int timeoutMs = 3000)
{
    QDeadlineTimer deadline(timeoutMs);
    while (!ready()) {
        if (deadline.hasExpired())
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return true;
}

const core::CommandSpec *specStartingWith(const std::vector<core::CommandSpec> &specs,
                                          const QString &prefix)
{
    for (const core::CommandSpec &s : specs)
        if (s.templateText.startsWith(prefix))
            return &s;
    return nullptr;
}

const char *kLsHelp =
    "Usage: ls [OPTION]... [FILE]...\n"
    "List information about the FILEs (the current directory by default).\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options too.\n"
    "  -a, --all                  do not ignore entries starting with .\n"
    "      --block-size=SIZE      with -l, scale sizes by SIZE when printing them;\n"
    "                               e.g., '--block-size=M'; see SIZE format below\n"
    "  -l                         use a long listing format\n"
    "  -w, --width=COLS           set output width to COLS.  0 means no limit\n"
    "      --color[=WHEN]         color the output WHEN; more info below\n"
    "      --help     display this help and exit\n";

} // namespace

TEST(command_params, recognises_command_behind_prefixes)
{
    const QString posix = QStringLiteral("posix");
    CHECK_EQ(core::commandName(QStringLiteral("sudo -u bob LANG=C /usr/bin/cp -r a b"), posix),
             QStringLiteral("cp"));
    CHECK_EQ(core::commandName(QStringLiteral("  "), posix), QString());
    CHECK(core::isKnownCommand(QStringLiteral("cp"), posix));
    CHECK(core::isKnownCommand(QStringLiteral("rsync"), posix));
    CHECK(!core::isKnownCommand(QStringLiteral("gibtesnicht"), posix));
    // Windows: Katalog-Befehle, Gross/Klein egal.
    CHECK(core::isKnownCommand(core::commandName(QStringLiteral("XCOPY a b"),
                                                 QStringLiteral("windows")),
                               QStringLiteral("windows")));
}

TEST(command_params, picks_variants_and_subcommands)
{
    const QString posix = QStringLiteral("posix");
    const auto tar = core::specsForLine(QStringLiteral("tar"), posix);
    CHECK(tar.size() >= 2);   // packen + entpacken
    const auto commit = core::specsForLine(QStringLiteral("git commit"), posix);
    CHECK_EQ(int(commit.size()), 1);
    CHECK(commit.front().templateText.startsWith(QStringLiteral("git commit")));
    CHECK(core::specsForLine(QStringLiteral("git"), posix).size() > 3);
}

TEST(command_params, positional_arguments_follow_command_order)
{
    const QString posix = QStringLiteral("posix");
    const auto cp = core::specsForLine(QStringLiteral("cp"), posix);
    const core::CommandSpec *spec = specStartingWith(cp, QStringLiteral("cp "));
    CHECK(spec != nullptr);
    const auto params = core::positionalParams(*spec);
    CHECK_EQ(int(params.size()), 2);
    CHECK_EQ(params.at(0).name, QStringLiteral("src"));
    CHECK_EQ(params.at(1).name, QStringLiteral("dst"));
    CHECK_EQ(core::positionalAppend(*spec, {{QStringLiteral("src"), QStringLiteral("a.txt")},
                                            {QStringLiteral("dst"), QStringLiteral("Neuer Ordner")}},
                                    QStringLiteral("cp -r")),
             QStringLiteral("a.txt \"Neuer Ordner\""));

    // Fehlende Befehlswoerter ("-czf") kommen vor die Werte.
    const auto tar = core::specsForLine(QStringLiteral("tar"), posix);
    const core::CommandSpec *pack = specStartingWith(tar, QStringLiteral("tar -czf"));
    CHECK(pack != nullptr);
    CHECK_EQ(core::positionalAppend(*pack, {{QStringLiteral("archive"), QStringLiteral("x.tgz")},
                                            {QStringLiteral("target"), QStringLiteral("dir")}},
                                    QStringLiteral("tar")),
             QStringLiteral("-czf x.tgz dir"));
    CHECK_EQ(core::positionalAppend(*pack, {{QStringLiteral("archive"), QStringLiteral("x.tgz")},
                                            {QStringLiteral("target"), QStringLiteral("dir")}},
                                    QStringLiteral("tar -czf")),
             QStringLiteral("x.tgz dir"));
}

TEST(command_params, parses_gnu_help)
{
    const auto opts = core::parseHelpOptions(QString::fromUtf8(kLsHelp));
    CHECK_EQ(int(opts.size()), 6);
    CHECK_EQ(opts.at(0).shortFlag, QStringLiteral("-a"));
    CHECK_EQ(opts.at(0).longFlag, QStringLiteral("--all"));
    CHECK(opts.at(0).description.startsWith(QStringLiteral("do not ignore")));
    // Fortsetzungszeile haengt an der Beschreibung.
    CHECK_EQ(opts.at(1).longFlag, QStringLiteral("--block-size"));
    CHECK_EQ(opts.at(1).arg, QStringLiteral("SIZE"));
    CHECK(opts.at(1).description.contains(QStringLiteral("SIZE format below")));
    CHECK_EQ(opts.at(1).insertText(QStringLiteral("M")), QStringLiteral("--block-size=M"));
    CHECK_EQ(opts.at(3).insertText(QStringLiteral("80")), QStringLiteral("-w 80"));
    CHECK_EQ(opts.at(4).longFlag, QStringLiteral("--color"));
    CHECK_EQ(opts.at(4).arg, QStringLiteral("WHEN"));
}

TEST(command_params, parses_git_and_busybox_help)
{
    const auto git = core::parseHelpOptions(QStringLiteral(
        "usage: git commit [<options>] [--] <pathspec>...\n"
        "\n"
        "    -q, --quiet           suppress summary after successful commit\n"
        "    -m, --message <message>\n"
        "                          commit message\n"
        "    --amend               amend previous commit\n"));
    CHECK_EQ(int(git.size()), 3);
    CHECK_EQ(git.at(1).shortFlag, QStringLiteral("-m"));
    CHECK_EQ(git.at(1).arg, QStringLiteral("<message>"));
    CHECK_EQ(git.at(1).description, QStringLiteral("commit message"));
    CHECK_EQ(git.at(1).insertText(QStringLiteral("Erster Stand")),
             QStringLiteral("-m \"Erster Stand\""));

    const auto busybox = core::parseHelpOptions(QStringLiteral(
        "Usage: ls [-1AaCxdLHRFplinshrSXvctu] [FILE]...\n\n"
        "\t-1\tOne column output\n"
        "\t-a\tInclude names starting with .\n"));
    CHECK_EQ(int(busybox.size()), 2);
    CHECK_EQ(busybox.at(1).description, QStringLiteral("Include names starting with ."));
}

TEST(command_params, help_command_is_safe)
{
    CHECK_EQ(core::helpCommandFor(QStringLiteral("ls -l")),
             QStringLiteral("LC_ALL=C ls --help 2>&1 | head -n 400"));
    CHECK_EQ(core::helpCommandFor(QStringLiteral("sudo git commit -m x")),
             QStringLiteral("LC_ALL=C git commit -h 2>&1 | head -n 400"));
    CHECK(core::helpCommandFor(QStringLiteral("ls;reboot")).isEmpty());
    CHECK(core::helpCommandFor(QStringLiteral("reboot")).isEmpty());
    CHECK(core::helpCommandFor(QStringLiteral("$(id)")).isEmpty());
}

TEST(command_params, terminal_input_line_without_prompt)
{
    gui::AsyncBridge bridge;
    gui::TerminalWidget terminal(&bridge);
    terminal.setPlainText(QStringLiteral("Willkommen\nroot@web01:~# cp -r src"));
    CHECK_EQ(terminal.currentInputLine(), QStringLiteral("cp -r src"));
    terminal.setPlainText(QStringLiteral("PS C:\\Users\\x> dir /w"));
    CHECK_EQ(terminal.currentInputLine(), QStringLiteral("dir /w"));
    terminal.setPlainText(QStringLiteral("Ausgabe ohne Prompt"));
    CHECK(terminal.currentInputLine().isEmpty());
}

TEST(command_params, param_button_and_dialog_append_in_order)
{
    gui::AsyncBridge bridge;
    gui::ConsolePanel console(&bridge, QStringLiteral("Test"));
    console.show();
    QPushButton *param = nullptr;
    for (QPushButton *b : console.findChildren<QPushButton *>())
        if (b->accessibleName() == QLatin1String("Parameter"))
            param = b;
    CHECK(param != nullptr);
    auto *input = console.findChild<QLineEdit *>();
    CHECK(!param->isVisible());
    input->setText(QStringLiteral("gibtesnicht"));
    QThread::msleep(300);
    QCoreApplication::processEvents();
    CHECK(pump([&] { return !param->isVisible(); }));

#ifdef Q_OS_WIN
    input->setText(QStringLiteral("xcopy"));
#else
    input->setText(QStringLiteral("cp -r"));
#endif
    CHECK(pump([&] { return param->isVisible(); }));

    // Schliessen gibt den Fokus an die Eingabe zurueck (Enter fuehrt aus).
    console.activateWindow();
    param->click();
    auto *opened = console.findChild<gui::ParamDialog *>();
    CHECK(opened != nullptr);
    opened->activateWindow();
    opened->setFocus();
    CHECK(pump([&] { return !input->hasFocus(); }));
    opened->close();
    CHECK(pump([&] { return input->hasFocus(); }));
    console.close();

    // Fenster direkt: Felder in Befehls-Reihenfolge, Anfuegen an die Zeile.
    QString line = QStringLiteral("cp -r");
    gui::ParamDialog dlg(&bridge, line, QStringLiteral("posix"), nullptr, QString(),
                         [&](const QString &text) { line += QLatin1Char(' ') + text; },
                         [&] { return line; });
    QList<QLineEdit *> fields;
    for (QLineEdit *e : dlg.findChildren<QLineEdit *>())
        if (!e->isClearButtonEnabled())   // Filterfeld ausnehmen
            fields << e;
    CHECK_EQ(fields.size(), 2);
    fields.at(0)->setText(QStringLiteral("a.txt"));
    fields.at(1)->setText(QStringLiteral("/tmp/ziel"));
    emit fields.at(1)->returnPressed();
    CHECK_EQ(line, QStringLiteral("cp -r a.txt /tmp/ziel"));

    // Katalog-Schalter stehen in der Liste; Doppelklick fuegt an.
    auto *tree = dlg.findChild<QTreeWidget *>(QStringLiteral("ParamOptions"));
    CHECK(tree && tree->topLevelItemCount() >= 2);
    QTreeWidgetItem *preserve = nullptr;
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        if (tree->topLevelItem(i)->text(0) == QLatin1String("-p"))
            preserve = tree->topLevelItem(i);
    CHECK(preserve != nullptr);
    emit tree->itemDoubleClicked(preserve, 0);
    CHECK_EQ(line, QStringLiteral("cp -r a.txt /tmp/ziel -p"));

    // Hilfe-Optionen kommen dazu, Dubletten nicht.
    const int before = tree->topLevelItemCount();
    dlg.addHelpOptions(core::parseHelpOptions(QString::fromUtf8(kLsHelp)));
    CHECK_EQ(tree->topLevelItemCount(), before + 6);
}

TEST(command_params, builder_checkboxes_reach_preview)
{
    // Assistent der Befehlspalette: Haekchen muessen in der Vorschau landen
    // (frueher kam der Flag-Text statt "an" bei render() an -> nie gesetzt).
    const core::CommandSpec *wc = specStartingWith(core::catalog(), QStringLiteral("wc "));
    CHECK(wc != nullptr);
    if (!wc)
        return;
    gui::CommandBuilder builder(*wc, QStringLiteral("posix"));
    auto *preview = builder.findChild<QPlainTextEdit *>();
    CHECK(preview != nullptr);
    const auto box = [&](const QString &flagValue) -> QCheckBox * {
        for (const core::CommandParam &p : wc->params)
            if (p.flagValue == flagValue)
                for (QCheckBox *c : builder.findChildren<QCheckBox *>())
                    if (c->text() == p.label)
                        return c;
        return nullptr;
    };
    QCheckBox *lines = box(QStringLiteral("-l"));
    QCheckBox *bytes = box(QStringLiteral("-c"));
    CHECK(lines != nullptr);
    CHECK(bytes != nullptr);   // Bytes (-c) war nicht waehlbar
    if (!preview || !lines || !bytes)
        return;
    CHECK_EQ(preview->toPlainText(), QStringLiteral("wc -l"));   // Vorgabe "an"
    bytes->setChecked(true);
    CHECK_EQ(preview->toPlainText(), QStringLiteral("wc -l -c"));
    lines->setChecked(false);
    CHECK_EQ(preview->toPlainText(), QStringLiteral("wc -c"));

    // Jeder Schalter des Katalogs wirkt sich auf die Vorschau aus.
    for (const core::CommandSpec &spec : core::catalog()) {
        gui::CommandBuilder b(spec, spec.platform == QLatin1String("windows")
                                        ? QStringLiteral("windows") : QStringLiteral("posix"));
        auto *pv = b.findChild<QPlainTextEdit *>();
        for (const core::CommandParam &p : spec.params) {
            if (p.kind != QLatin1String("flag"))
                continue;
            QCheckBox *check = nullptr;
            for (QCheckBox *c : b.findChildren<QCheckBox *>())
                if (c->text() == (p.description.isEmpty() ? p.label : p.description))
                    check = c;
            CHECK(check != nullptr);
            if (!pv || !check)
                continue;
            check->setChecked(false);
            const QString off = pv->toPlainText();
            check->setChecked(true);
            const QString on = pv->toPlainText();
            if (on == off || !on.contains(p.flagValue.trimmed()))
                qWarning() << "Schalter ohne Wirkung:" << spec.templateText << p.name;
            CHECK(on != off);
            CHECK(on.contains(p.flagValue.trimmed()));
        }
    }
}

TEST(command_params, valued_option_only_when_filled)
{
    // lsof: "-p" steht nur mit PID da; -a verknuepft es mit -i per UND.
    const core::CommandSpec *lsof = specStartingWith(core::catalog(), QStringLiteral("sudo lsof "));
    CHECK(lsof != nullptr);
    if (!lsof)
        return;
    using Values = QHash<QString, QString>;
    CHECK_EQ(core::render(*lsof, Values{}), QStringLiteral("sudo lsof"));
    CHECK_EQ(core::render(*lsof, Values{{QStringLiteral("pid"), QStringLiteral("  ")}}),
             QStringLiteral("sudo lsof"));
    CHECK_EQ(core::render(*lsof, Values{{QStringLiteral("net"), QStringLiteral("1")},
                                        {QStringLiteral("pid"), QStringLiteral("1234")}}),
             QStringLiteral("sudo lsof -i -a -p 1234"));
    // Parameter-Knopf der Konsole: gleiche Regel.
    CHECK_EQ(core::positionalAppend(*lsof, Values{{QStringLiteral("pid"), QStringLiteral("1234")}},
                                    QStringLiteral("sudo lsof")),
             QStringLiteral("-a -p 1234"));

    // Windows-ping: -t muss hinter -n stehen (die letzte Angabe gewinnt).
    const core::CommandSpec *ping = nullptr;
    for (const core::CommandSpec &s : core::catalog())
        if (s.platform == QLatin1String("windows") && s.templateText.startsWith(QStringLiteral("ping ")))
            ping = &s;
    CHECK(ping != nullptr);
    if (!ping)
        return;
    CHECK_EQ(core::render(*ping, Values{{QStringLiteral("endless"), QStringLiteral("1")},
                                        {QStringLiteral("host"), QStringLiteral("8.8.8.8")}}),
             QStringLiteral("ping -n 4 -t 8.8.8.8"));
}

TEST(command_params, light_theme_makes_terminal_light_and_readable)
{
    // Weiss auf hellem Grund wird abgedunkelt, auf dunklem bleibt es.
    const QColor white(QStringLiteral("#ffffff"));
    CHECK(gui::readableOn(white, QColor(QStringLiteral("#fbfbfd"))).lightness() < 140);
    CHECK(gui::readableOn(white, QColor(QStringLiteral("#12141a"))) == white);

    const QString oldSheet = qApp->styleSheet();
    const QPalette oldPalette = qApp->palette();
    const QString oldStyle = qApp->style()->name();
    gui::AsyncBridge bridge;
    gui::TerminalWidget terminal(&bridge);
    gui::applyTheme(qApp, QStringLiteral("Hell"));
    // Bestehendes Terminal zieht nach (Hintergrund aus dem Theme).
    CHECK(terminal.styleSheet().contains(QStringLiteral("#fbfbfd")));

    QPlainTextEdit edit;
    gui::AnsiRenderer renderer(&edit);
    renderer.feed(QStringLiteral("\x1b[97mWEISS\x1b[0m normal"));
    const QTextBlock block = edit.document()->firstBlock();
    const QColor first = block.begin().fragment().charFormat().foreground().color();
    CHECK(first.lightness() < 140);   // lesbar auf weissem Grund

    gui::applyTheme(qApp, gui::defaultTheme());
    CHECK(terminal.styleSheet().contains(QStringLiteral("#12141a")));
    qApp->setStyleSheet(oldSheet);
    qApp->setPalette(oldPalette);
    QApplication::setStyle(oldStyle);
}
