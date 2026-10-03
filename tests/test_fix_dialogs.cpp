// Regressionstests (netzfrei) fuer die Fehlerrunde Dialoge/Kern:
//  * Alarm-Aktion: Dateinamen mit cmd-Metazeichen (`x & calc.exe`) duerfen
//    keinen zweiten Befehl ausfuehren.
//  * Lokale Befehle erreichen cmd.exe woertlich (keine \"-Quotes von QProcess).
//  * Tastenkuerzel-Dubletten werden schreibweisen-unabhaengig erkannt.
//  * Profile: Umbenennen ersetzt statt zu duplizieren, freie Namen beim Import.
#include "tests/harness.hpp"

#include "ncssh/config.hpp"
#include "ncssh/core/filealarm.hpp"
#include "ncssh/core/profiles.hpp"
#include "ncssh/core/runner.hpp"
#include "ncssh/core/secrets.hpp"
#include "ncssh/core/shortcuts.hpp"

#include <QProcess>
#include <QProcessEnvironment>
#include <QSet>
#include <QTemporaryDir>

using namespace ncssh::core;

namespace {

// Lenkt configDir() waehrend des Tests auf ein frisches Verzeichnis um.
class ConfigDirGuard
{
public:
    ConfigDirGuard() : m_appdata(qgetenv("APPDATA")), m_xdg(qgetenv("XDG_CONFIG_HOME"))
    {
        qputenv("APPDATA", m_dir.path().toLocal8Bit());
        qputenv("XDG_CONFIG_HOME", m_dir.path().toLocal8Bit());
    }
    ~ConfigDirGuard()
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

#ifdef Q_OS_WIN
// Fuehrt eine Alarm-Aktion wie FileAlarmManager::runAction aus, aber
// blockierend mit eingefangener Ausgabe.
QString runAlarmCommand(const QString &templ, const QString &path)
{
    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const auto vars = alarmEnvironment(QStringLiteral("created"), path,
                                       QStringLiteral("Test"), 1);
    for (auto it = vars.begin(); it != vars.end(); ++it)
        env.insert(it.key(), it.value());
    proc.setProcessEnvironment(env);
    proc.setProcessChannelMode(QProcess::MergedChannels);
    setShellCommand(proc, alarmShellCommand(templ, 1), /*delayedExpansion=*/true);
    proc.start();
    if (!proc.waitForFinished(10000))
        return QStringLiteral("<timeout>");
    return QString::fromLocal8Bit(proc.readAll()).trimmed();
}
#endif

} // namespace

TEST(fix_dialogs, alarm_command_never_embeds_values)
{
    // Die Vorlage enthaelt nach dem Aufbau nur Variablenverweise, nie den Wert.
    const QString cmd = alarmShellCommand(QStringLiteral("echo {kind} {path} {name} {count}"), 7);
    CHECK(!cmd.contains(QStringLiteral("{path}")));
    CHECK(!cmd.contains(QStringLiteral("{kind}")));
    CHECK(!cmd.contains(QStringLiteral("{name}")));
    CHECK(cmd.contains(QStringLiteral("ALARM_PATH")));
    CHECK(cmd.contains(QStringLiteral("ALARM_KIND")));
    CHECK(cmd.contains(QStringLiteral("ALARM_NAME")));
    CHECK(cmd.endsWith(QStringLiteral(" 7")));
#ifdef Q_OS_WIN
    CHECK_EQ(alarmShellCommand(QStringLiteral("echo {path}"), 1),
             QStringLiteral("echo !ALARM_PATH!"));
#endif
    const auto env = alarmEnvironment(QStringLiteral("deleted"), QStringLiteral("/a b"),
                                      QStringLiteral("N"), 3);
    CHECK_EQ(env.value(QStringLiteral("ALARM_PATH")), QStringLiteral("/a b"));
    CHECK_EQ(env.value(QStringLiteral("ALARM_KIND")), QStringLiteral("deleted"));
    CHECK_EQ(env.value(QStringLiteral("ALARM_COUNT")), QStringLiteral("3"));
}

#ifdef Q_OS_WIN
TEST(fix_dialogs, alarm_command_neutralizes_cmd_metacharacters)
{
    // Frueher: "echo {path}" mit dem Namen `x & echo INJECTED` lief als zwei
    // Befehle. Jetzt muss der Name als reiner Text ausgegeben werden.
    CHECK_EQ(runAlarmCommand(QStringLiteral("echo {path}"),
                             QStringLiteral("x & echo INJECTED")),
             QStringLiteral("x & echo INJECTED"));
    // Weitere Metazeichen: Pipe, Umleitung, Prozent-Variablen.
    CHECK_EQ(runAlarmCommand(QStringLiteral("echo {path}"),
                             QStringLiteral("a | b > c %PATH%")),
             QStringLiteral("a | b > c %PATH%"));
    // Gequotete Platzhalter funktionieren weiterhin.
    CHECK_EQ(runAlarmCommand(QStringLiteral("echo \"{path}\""),
                             QStringLiteral("x & y")),
             QStringLiteral("\"x & y\""));
}

TEST(fix_dialogs, runner_passes_quotes_verbatim_to_cmd)
{
    CHECK_EQ(cmdNativeArguments(QStringLiteral("dir \"C:\\Program Files\"")),
             QStringLiteral("/s /c \"dir \"C:\\Program Files\"\""));
    CHECK_EQ(cmdNativeArguments(QStringLiteral("echo %1"), true),
             QStringLiteral("/v:on /s /c \"echo %1\""));

    // Echter Lauf: QProcess quotete frueher im MSVC-Stil (\"a b\"), cmd gab
    // dann die Backslashes mit aus.
    LocalCommandRunner runner;
    QStringList lines;
    runner.stream(QStringLiteral("echo \"a b\""), QString(),
                  [&lines](const QString &l) { lines << l; }, nullptr);
    CHECK_EQ(lines.join(QLatin1Char('\n')).trimmed(), QStringLiteral("\"a b\""));
    CHECK_EQ(runner.lastExitStatus.value_or(-1), 0);

    // Pfad mit Leerzeichen in Anfuehrungszeichen (der gemeldete Fall).
    lines.clear();
    runner.stream(QStringLiteral("if exist \"C:\\Windows\\System32\" (echo yes) else (echo no)"),
                  QString(), [&lines](const QString &l) { lines << l; }, nullptr);
    CHECK_EQ(lines.join(QLatin1Char('\n')).trimmed(), QStringLiteral("yes"));
}
#endif

TEST(fix_dialogs, shortcut_normalization)
{
    CHECK_EQ(normalizeShortcut(QStringLiteral("ctrl+p")), QStringLiteral("Ctrl+P"));
    CHECK_EQ(normalizeShortcut(QStringLiteral("Ctrl+P")), QStringLiteral("Ctrl+P"));
    CHECK_EQ(normalizeShortcut(QStringLiteral(" CTRL + shift + k ")),
             QStringLiteral("Ctrl+Shift+K"));
    CHECK_EQ(normalizeShortcut(QStringLiteral("f9")), normalizeShortcut(QStringLiteral("F9")));
    CHECK(normalizeShortcut(QString()).isEmpty());
    CHECK(normalizeShortcut(QStringLiteral("   ")).isEmpty());
}

TEST(fix_dialogs, default_shortcuts_do_not_clash_with_fixed_ones)
{
    // Sonst liesse sich der Einstellungsdialog mit Standardwerten nicht
    // speichern (die Dublettenpruefung schliesst die festen Kuerzel ein).
    QSet<QString> seen;
    for (const auto &[key, label] : fixedShortcuts()) {
        const QString norm = normalizeShortcut(key);
        CHECK(!norm.isEmpty());
        CHECK(!seen.contains(norm));
        seen.insert(norm);
    }
    for (const ShortcutDef &def : shortcutDefs()) {
        if (def.key.isEmpty())
            continue;
        const QString norm = normalizeShortcut(def.key);
        if (seen.contains(norm))
            ::ncssh::tests::reportFailure(__FILE__, __LINE__,
                                          "Doppeltes Standard-Kuerzel: " + def.key.toStdString());
        seen.insert(norm);
    }
}

TEST(fix_dialogs, profile_unique_name_and_rename)
{
    ConfigDirGuard guard;
    CHECK(guard.isValid());

    const QString oldName = QStringLiteral("ncssh-selftest-rename-A9");
    const QString newName = QStringLiteral("ncssh-selftest-rename-B9");

    ProfileStore store;
    ServerProfile p;
    p.name = oldName;
    p.host = QStringLiteral("example.com");
    p.authMethod = QStringLiteral("password");
    p.password = QStringLiteral("pw-rename-test");
    p.savePassword = true;
    store.upsert(p);

    // Freie Namen fuer den Import: vorhandener Name bekommt einen Zaehler.
    CHECK_EQ(store.uniqueName(QStringLiteral("frei")), QStringLiteral("frei"));
    CHECK_EQ(store.uniqueName(oldName), oldName + QStringLiteral(" (2)"));

    // Umbenennen ersetzt das Profil (kein Duplikat) und zieht das Passwort mit.
    ServerProfile renamed = p;
    renamed.name = newName;
    store.rename(oldName, renamed);
    CHECK(!store.get(oldName).has_value());
    CHECK(store.get(newName).has_value());
    CHECK_EQ(int(store.profiles().size()), 1);
#ifdef Q_OS_WIN
    CHECK_EQ(getSecret(newName, QStringLiteral("password")).value_or(QString()),
             QStringLiteral("pw-rename-test"));
    CHECK(!getSecret(oldName, QStringLiteral("password")).has_value());
#endif

    // Auf einen vergebenen Namen umbenennen wird abgelehnt.
    ServerProfile other;
    other.name = oldName;
    other.host = QStringLiteral("other.example.com");
    store.upsert(other);
    ServerProfile clash = renamed;
    clash.name = oldName;
    CHECK_THROWS(store.rename(newName, clash));
    CHECK_EQ(store.get(oldName)->host, QStringLiteral("other.example.com"));

    // Aufraeumen (Keyring).
    store.remove(newName);
    store.remove(oldName);
}
