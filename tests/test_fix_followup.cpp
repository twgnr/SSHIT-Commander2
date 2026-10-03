// Nachbesserungen aus der Durchsicht der Runden 2/3:
//  - eine nie beendete OSC verschluckt nicht mehr alle folgende Ausgabe
//  - "!" und "^" in Alarm-Vorlagen bleiben woertlich (cmd /v:on)
//  - Umbenennen eines Profils zieht gespeicherte Tabs und Lesezeichen nach
#include "tests/harness.hpp"

#include "ncssh/core/bookmarks.hpp"
#include "ncssh/core/filealarm.hpp"
#include "ncssh/core/profiles.hpp"
#include "ncssh/core/runner.hpp"
#include "ncssh/core/settings.hpp"
#include "ncssh/gui/ansi.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>

using namespace ncssh;

TEST(fix_followup, unterminated_osc_does_not_swallow_later_output)
{
    QPlainTextEdit edit;
    gui::AnsiRenderer renderer(&edit);
    // "ESC ]" ohne Ende (z. B. aus einer per cat ausgegebenen Binaerdatei) …
    renderer.feed(QStringLiteral("\x1b]0;") + QString(5000, QLatin1Char('x')));
    // … danach normale Ausgabe in einem spaeteren Chunk.
    renderer.feed(QStringLiteral("\r\nSICHTBAR\r\n"));
    CHECK(edit.toPlainText().contains(QStringLiteral("SICHTBAR")));
}

TEST(fix_followup, alarm_template_keeps_literal_exclamation_and_caret)
{
#ifdef Q_OS_WIN
    const QString cmd =
        core::alarmShellCommand(QStringLiteral("echo Achtung! \"^x\" {path}"), 1);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const auto vars = core::alarmEnvironment(QStringLiteral("neu"),
                                             QStringLiteral("C:\\a & b.txt"),
                                             QStringLiteral("Test"), 1);
    for (auto it = vars.begin(); it != vars.end(); ++it)
        env.insert(it.key(), it.value());
    QProcess proc;
    proc.setProcessEnvironment(env);
    core::setShellCommand(proc, cmd, /*delayedExpansion=*/true);
    proc.start();
    CHECK(proc.waitForFinished(10000));
    const QString out = QString::fromLocal8Bit(proc.readAllStandardOutput()).trimmed();
    CHECK_EQ(out, QStringLiteral("Achtung! \"^x\" C:\\a & b.txt"));
#endif
}

TEST(fix_followup, profile_rename_moves_tab_and_bookmark_references)
{
    const QByteArray oldAppData = qgetenv("APPDATA");
    QTemporaryDir cfg;
    CHECK(cfg.isValid());
    qputenv("APPDATA", cfg.path().toLocal8Bit());
    {
        const QString oldName = QStringLiteral("sshit-test-alt-%1").arg(qint64(QDateTime::currentMSecsSinceEpoch()));
        const QString newName = oldName + QStringLiteral("-neu");
        core::ProfileStore store;
        core::ServerProfile p;
        p.name = oldName;
        p.host = QStringLiteral("example.invalid");
        store.upsert(p);
        // Gespeicherter Tab + Lesezeichen, die den alten Namen kennen.
        core::setSetting(QStringLiteral("session_tabs"),
                         QJsonArray{QJsonObject{{QStringLiteral("profile"), oldName}}});
        core::BookmarkStore bm;
        bm.load();
        bm.add(oldName, QStringLiteral("/var/log"));
        bm.save();

        core::ServerProfile renamed = p;
        renamed.name = newName;
        store.rename(oldName, renamed);

        const QVariantList tabs = core::getSetting(QStringLiteral("session_tabs")).toList();
        CHECK_EQ(tabs.size(), 1);
        if (!tabs.isEmpty())
            CHECK_EQ(tabs.first().toMap().value(QStringLiteral("profile")).toString(), newName);
        core::BookmarkStore after;
        after.load();
        CHECK(after.list(newName).contains(QStringLiteral("/var/log")));
        CHECK(after.list(oldName).isEmpty());
    }
    qputenv("APPDATA", oldAppData);
}
