// Befehlsmodus der Konsole: die Ausgabe eines Befehls muss im Ausgabefenster
// ankommen. Bis 1.0.2 hiess der Zeilen-Callback "emit" — Qts leeres Makro —,
// und jede Ausgabe ging verloren; nur die Befehlszeile selbst war zu sehen.
#include "tests/harness.hpp"

#include "ncssh/core/runner.hpp"
#include "ncssh/gui/bridge.hpp"
#include "ncssh/gui/console_panel.hpp"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QThread>

using namespace ncssh;

namespace {

template <typename Predicate>
bool pump(Predicate ready, int timeoutMs = 10000)
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

QPlainTextEdit *outputOf(gui::ConsolePanel &console)
{
    for (QPlainTextEdit *e : console.findChildren<QPlainTextEdit *>())
        if (e->objectName() == QLatin1String("ConsoleOutput"))
            return e;
    return nullptr;
}

} // namespace

TEST(console_output, command_mode_shows_command_output)
{
    gui::AsyncBridge bridge;
    core::LocalCommandRunner runner;
    gui::ConsolePanel console(&bridge, QStringLiteral("Test"));
    console.setRunner(&runner, QDir::homePath());
    auto *input = console.findChild<QLineEdit *>();
    QPlainTextEdit *output = outputOf(console);
    CHECK(input && output);

    input->setText(QStringLiteral("echo AUSGABE-KOMMT-AN"));
    emit input->returnPressed();
    // Befehlszeile ("… $ echo AUSGABE-KOMMT-AN") PLUS eine eigene Ergebniszeile.
    CHECK(pump([&] {
        const QStringList lines = output->toPlainText().split(QLatin1Char('\n'));
        return lines.contains(QStringLiteral("AUSGABE-KOMMT-AN"));
    }));
}

TEST(console_output, bridge_stream_delivers_lines)
{
    gui::AsyncBridge bridge;
    QStringList got;
    bool done = false;
    bridge.stream(
        [](const gui::AsyncBridge::EmitLine &emitLine, const gui::CancelTokenPtr &) {
            emitLine(QStringLiteral("eins"));
            emitLine(QStringLiteral("zwei"));
        },
        [&](const QString &line) { got << line; }, [&] { done = true; });
    CHECK(pump([&] { return done; }));
    CHECK_EQ(got, (QStringList{QStringLiteral("eins"), QStringLiteral("zwei")}));
}

#ifdef Q_OS_WIN
TEST(console_output, cmd_umlauts_are_decoded)
{
    // cmd schreibt in der OEM-Codepage; die Konsole muss das lesbar zeigen.
    core::LocalCommandRunner runner;
    QStringList lines;
    runner.stream(QString::fromUtf8("echo Größe Übel"), QDir::homePath(),
                  [&](const QString &l) { lines << l; }, {});
    CHECK(lines.contains(QString::fromUtf8("Größe Übel")));
}
#endif
