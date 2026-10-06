// Umgebungsvariablen pro Session (PuTTY "Environment variables"): Eingabe-
// format des Profil-Dialogs, Speicherung im Profil und Import aus PuTTY
// (.reg/Registry) sowie ~/.ssh/config (SetEnv).
#include "tests/harness.hpp"

#include "ncssh/core/importers.hpp"
#include "ncssh/core/models.hpp"
#include "ncssh/net/ssh.hpp"

#include <QFile>
#include <QTemporaryDir>

using namespace ncssh;
using core::EnvVar;

namespace {

QStringList names(const std::vector<EnvVar> &vars)
{
    QStringList out;
    for (const auto &v : vars)
        out << v.name;
    return out;
}

QString valueOf(const std::vector<EnvVar> &vars, const QString &name)
{
    for (const auto &v : vars)
        if (v.name == name)
            return v.value;
    return QStringLiteral("<fehlt>");
}

} // namespace

TEST(session_env, valid_names)
{
    CHECK(core::isValidEnvName(QStringLiteral("LANG")));
    CHECK(core::isValidEnvName(QStringLiteral("_x1")));
    CHECK(core::isValidEnvName(QStringLiteral("LC_ALL")));
    CHECK(!core::isValidEnvName(QString()));
    CHECK(!core::isValidEnvName(QStringLiteral("1ABC")));
    CHECK(!core::isValidEnvName(QStringLiteral("MY-VAR")));
    CHECK(!core::isValidEnvName(QStringLiteral("A B")));
    CHECK(!core::isValidEnvName(QStringLiteral("ÄRGER")));
}

TEST(session_env, parse_keeps_values_literally)
{
    const auto r = core::parseEnvironment(
        QStringLiteral("# Kommentar\r\nLANG=de_DE.UTF-8\n\n  EDITOR = vim\nGREETING=a b = c \nEMPTY=\n"));
    CHECK(r.errors.isEmpty());
    CHECK_EQ(names(r.vars),
             (QStringList{QStringLiteral("LANG"), QStringLiteral("EDITOR"),
                          QStringLiteral("GREETING"), QStringLiteral("EMPTY")}));
    CHECK_EQ(valueOf(r.vars, QStringLiteral("LANG")), QStringLiteral("de_DE.UTF-8"));
    // Name wird getrimmt, der Wert bleibt woertlich (auch Leerzeichen und '=').
    CHECK_EQ(valueOf(r.vars, QStringLiteral("EDITOR")), QStringLiteral(" vim"));
    CHECK_EQ(valueOf(r.vars, QStringLiteral("GREETING")), QStringLiteral("a b = c "));
    CHECK_EQ(valueOf(r.vars, QStringLiteral("EMPTY")), QString());
}

TEST(session_env, parse_reports_invalid_lines_and_dedups)
{
    const auto r = core::parseEnvironment(
        QStringLiteral("A=1\nkein gleichheitszeichen\n9X=2\nA=3"));
    CHECK_EQ(r.errors.size(), qsizetype(2));
    CHECK(r.errors.value(0).startsWith(QStringLiteral("2:")));
    CHECK(r.errors.value(1).startsWith(QStringLiteral("3:")));
    // Doppelter Name: der spaetere gewinnt, Position bleibt die erste.
    CHECK_EQ(r.vars.size(), size_t(1));
    CHECK_EQ(valueOf(r.vars, QStringLiteral("A")), QStringLiteral("3"));
}

TEST(session_env, format_roundtrips)
{
    const std::vector<EnvVar> vars{{QStringLiteral("LANG"), QStringLiteral("C.UTF-8")},
                                   {QStringLiteral("X"), QStringLiteral("a=b c")}};
    const QString text = core::formatEnvironment(vars);
    CHECK_EQ(text, QStringLiteral("LANG=C.UTF-8\nX=a=b c"));
    CHECK(core::parseEnvironment(text).vars == vars);
}

TEST(session_env, profile_json_keeps_order_and_drops_invalid_names)
{
    core::ServerProfile p;
    p.name = QStringLiteral("web");
    p.host = QStringLiteral("web.example");
    p.environment = {{QStringLiteral("ZZZ"), QStringLiteral("1")},
                     {QStringLiteral("AAA"), QStringLiteral("2")}};
    const core::ServerProfile back = core::ServerProfile::fromJson(p.toJson());
    CHECK(back.environment == p.environment);

    // Von Hand editierte Datei: ungueltige Namen nicht an den Server schicken.
    QJsonObject json = p.toJson();
    QJsonArray env = json.value(QStringLiteral("environment")).toArray();
    env.append(QJsonObject{{QStringLiteral("name"), QStringLiteral("bad name")},
                           {QStringLiteral("value"), QStringLiteral("x")}});
    json.insert(QStringLiteral("environment"), env);
    CHECK_EQ(core::ServerProfile::fromJson(json).environment.size(), size_t(2));

    // Alte Profile ohne Feld: leer.
    json.remove(QStringLiteral("environment"));
    CHECK(core::ServerProfile::fromJson(json).environment.empty());
}

TEST(session_env, putty_environment_format)
{
    const auto vars = core::parsePuttyEnvironment(
        QStringLiteral("LANG=de_DE.UTF-8,LIST=a\\,b\\=c\\\\d,1BAD=x,OLD\tstyle"));
    CHECK_EQ(names(vars), (QStringList{QStringLiteral("LANG"), QStringLiteral("LIST"),
                                       QStringLiteral("OLD")}));
    CHECK_EQ(valueOf(vars, QStringLiteral("LIST")), QStringLiteral("a,b=c\\d"));
    CHECK_EQ(valueOf(vars, QStringLiteral("OLD")), QStringLiteral("style"));
    CHECK(core::parsePuttyEnvironment(QString()).empty());
}

TEST(session_env, ssh_config_setenv)
{
    const auto vars = core::parseSshSetEnv(
        QStringLiteral("FOO=bar  BAZ=\"a b\" \"QUX=c d\" 1BAD=x NOEQ"));
    CHECK_EQ(names(vars), (QStringList{QStringLiteral("FOO"), QStringLiteral("BAZ"),
                                       QStringLiteral("QUX")}));
    CHECK_EQ(valueOf(vars, QStringLiteral("BAZ")), QStringLiteral("a b"));
    CHECK_EQ(valueOf(vars, QStringLiteral("QUX")), QStringLiteral("c d"));
}

// Live (nur mit NCSSH_LIVE_SSH=host:port, Benutzer sshitbob/BobPass1): echter
// OpenSSH mit Standard-AcceptEnv "LANG LC_*" — LANG kommt an, die eigene
// Variable wird abgelehnt, gemeldet und danach nicht erneut angefragt.
TEST(session_env_live, server_accepts_lang_and_reports_rejected)
{
    const QString target = qEnvironmentVariable("NCSSH_LIVE_SSH");
    if (target.isEmpty())
        return;
    core::ServerProfile profile;
    profile.host = target.section(QLatin1Char(':'), 0, 0);
    profile.port = target.section(QLatin1Char(':'), 1, 1).toInt();
    profile.username = QStringLiteral("sshitbob");
    profile.authMethod = QStringLiteral("password");
    profile.password = QStringLiteral("BobPass1");
    profile.knownHostsPolicy = QStringLiteral("ignore");
    profile.environment = {{QStringLiteral("LC_SSHIT_PROBE"), QStringLiteral("hallo welt")},
                           {QStringLiteral("SSHIT_PROBE"), QStringLiteral("nein")}};
    const net::SSHSessionPtr session = net::connectSession(profile, nullptr);
    CHECK(session != nullptr);
    if (!session)
        return;

    // Konsolenbefehl (exec-Kanal): angenommene Variable da, abgelehnte nicht.
    QString out;
    session->runner()->stream(QStringLiteral("echo \"[$LC_SSHIT_PROBE|$SSHIT_PROBE]\""),
                              QStringLiteral("."),
                              [&out](const QString &line) { out += line; },
                              std::make_shared<gui::CancelToken>());
    CHECK(out.contains(QStringLiteral("[hallo welt|]")));
    {
        std::lock_guard<std::recursive_mutex> lock(session->mutex());
        CHECK_EQ(session->rejectedEnv, QStringList{QStringLiteral("SSHIT_PROBE")});
    }

    // Terminal: dieselbe Session fragt die abgelehnte Variable nicht erneut an.
    auto shell = net::RemoteShell::open(session, 80, 24);
    CHECK(shell->rejectedEnvironment().isEmpty());
    shell->write(QByteArrayLiteral("echo \"<$LC_SSHIT_PROBE>\"; exit\n"));
    QByteArray seen;
    for (int i = 0; i < 100 && !shell->atEof(); ++i)
        seen += shell->read(8192, 100);
    CHECK(seen.contains("<hallo welt>"));
    shell->close();

    // Frische Session: das Terminal meldet die Ablehnung selbst.
    const net::SSHSessionPtr fresh = net::connectSession(profile, nullptr);
    CHECK(fresh != nullptr);
    if (!fresh)
        return;
    auto shell2 = net::RemoteShell::open(fresh, 80, 24);
    CHECK_EQ(shell2->rejectedEnvironment(), QStringList{QStringLiteral("SSHIT_PROBE")});
    shell2->close();
    fresh->close();
    session->close();
}

TEST(session_env, putty_reg_export_imports_environment)
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("putty.reg"));
    QFile f(path);
    CHECK(f.open(QIODevice::WriteOnly));
    // In .reg sind Backslashes verdoppelt: PuTTYs "a\,b" steht als "a\\,b".
    f.write("Windows Registry Editor Version 5.00\r\n\r\n"
            "[HKEY_CURRENT_USER\\Software\\SimonTatham\\PuTTY\\Sessions\\web01]\r\n"
            "\"HostName\"=\"web01.example\"\r\n"
            "\"PortNumber\"=dword:00000016\r\n"
            "\"Environment\"=\"LANG=de_DE.UTF-8,LIST=a\\\\,b\"\r\n");
    f.close();
    const auto profiles = core::importFromFile(path);
    CHECK_EQ(profiles.size(), size_t(1));
    if (profiles.empty())
        return;
    CHECK_EQ(profiles.front().host, QStringLiteral("web01.example"));
    CHECK_EQ(names(profiles.front().environment),
             (QStringList{QStringLiteral("LANG"), QStringLiteral("LIST")}));
    CHECK_EQ(valueOf(profiles.front().environment, QStringLiteral("LANG")),
             QStringLiteral("de_DE.UTF-8"));   // frueher am zweiten '=' abgeschnitten
    CHECK_EQ(valueOf(profiles.front().environment, QStringLiteral("LIST")),
             QStringLiteral("a,b"));
}
