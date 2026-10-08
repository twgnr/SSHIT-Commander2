// Regressionstests fuer die Funde aus der Handbuch-Pruefung (2026-10):
//  * Makro-Aktion "Sequenz (eine pro Druck)" fuehrte bei jedem Druck alle
//    Schritte aus — wie "Mehrere Aktionen".
//  * SFTP-Batch: der Platzhalter zeigte "$heute", Variablen gab es nicht.
//  * Tunnel-Presets liessen sich nirgends ansehen oder entfernen.
//  * SSH-Kompression war wirkungslos (libssh2 ohne zlib gebaut).
//  * "Zuletzt verbunden" wurde nie gesetzt.
//  * GitHub-Alarm: das Label versprach Haekchen, die es nicht gab.
#include "tests/harness.hpp"

#include "ncssh/core/filesystem.hpp"
#include "ncssh/core/githubalarm.hpp"
#include "ncssh/core/i18n.hpp"
#include "ncssh/core/macroactions.hpp"
#include "ncssh/core/profiles.hpp"
#include "ncssh/core/secrets.hpp"
#include "ncssh/gui/bridge.hpp"
#include "ncssh/gui/githubalarm_dialog.hpp"
#include "ncssh/gui/server_manager.hpp"
#include "ncssh/net/sftp_batch.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QListWidget>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>

#include <libssh2.h>

using namespace ncssh;

namespace {

QJsonObject consoleStep(const QString &cmd)
{
    return QJsonObject{{QStringLiteral("action_type"), QStringLiteral("ssh_command")},
                       {QStringLiteral("payload"), cmd}};
}

QPushButton *buttonWithText(QWidget &parent, const QString &text)
{
    for (QPushButton *b : parent.findChildren<QPushButton *>())
        if (b->text() == text)
            return b;
    return nullptr;
}

} // namespace

TEST(fix_findings, macro_sequence_runs_one_step_per_press)
{
    namespace ma = core::macroactions;
    QStringList sent;
    ma::ExecContext ctx;
    ctx.sshSend = [&sent](const QString &cmd, bool) { sent << cmd; };
    const QJsonArray steps{consoleStep(QStringLiteral("a")), consoleStep(QStringLiteral("b")),
                           consoleStep(QStringLiteral("c"))};

    // Sequenz: je Druck der naechste Schritt, nach dem letzten wieder von vorn.
    for (int press = 0; press < 4; ++press)
        CHECK(!ma::executeAction(QStringLiteral("sequence"), steps, &ctx, QStringLiteral("L:1")));
    CHECK_EQ(sent, (QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"),
                                QStringLiteral("a")}));
    // Eine andere Taste hat ihre eigene Position.
    sent.clear();
    CHECK(!ma::executeAction(QStringLiteral("sequence"), steps, &ctx, QStringLiteral("L:2")));
    CHECK_EQ(sent, QStringList{QStringLiteral("a")});

    // Mehrere Aktionen: alle Schritte bei jedem Druck.
    sent.clear();
    CHECK(!ma::executeAction(QStringLiteral("multi_action"), steps, &ctx, QStringLiteral("L:3")));
    CHECK_EQ(sent, (QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}));
}

TEST(fix_findings, sftp_batch_variables)
{
    const QDateTime when(QDate(2026, 3, 7), QTime(9, 5, 1));
    const auto vars = net::builtinBatchVariables(when);
    CHECK_EQ(net::expandBatchVariables(QStringLiteral("get a.log a-$heute.log"), vars),
             QStringLiteral("get a.log a-2026-03-07.log"));
    // ${…} grenzt ab: "$tag_" waere eine Variable namens "tag_".
    CHECK_EQ(net::expandBatchVariables(QStringLiteral("put x ${JAHR}/$monat/${tag}_$zeit"), vars),
             QStringLiteral("put x 2026/03/07_09-05-01"));
    CHECK_THROWS(net::expandBatchVariables(QStringLiteral("put x $tag_$zeit"), vars));
    CHECK_EQ(net::expandBatchVariables(QStringLiteral("echo $now kostet $$5"), vars),
             QStringLiteral("echo 2026-03-07_09-05-01 kostet $5"));
    CHECK_THROWS(net::expandBatchVariables(QStringLiteral("cd $gibtsnicht"), vars));

    // Eigene Variablen per set; eine unbekannte Variable ist ein Fehler der Zeile.
    QTemporaryDir dir;
    CHECK(dir.isValid());
    core::LocalFileSystem local;
    core::LocalFileSystem remote;
    const auto res = net::runSftpBatch(QStringLiteral("set ziel ordner $jahr\n"
                                                      "echo nach $ziel\n"
                                                      "mkdir \"$ziel\"\n"
                                                      "echo $tippfehler\n"),
                                       &local, &remote, dir.path(), dir.path());
    CHECK_EQ(res.failed, 1);
    CHECK(res.log.join(QLatin1Char('\n')).contains(QStringLiteral("unbekannte Variable $tippfehler")));
    const QString year = QString::number(QDate::currentDate().year());
    CHECK(res.log.contains(QStringLiteral("nach ordner ") + year));
    CHECK(QFileInfo(dir.path() + QStringLiteral("/ordner ") + year).isDir());
}

TEST(fix_findings, server_manager_tunnel_presets_can_be_removed)
{
    const QString name = QStringLiteral("ncssh-selftest-tunnel-remove-D4");
    {
        core::ProfileStore store;
        store.load();
        core::ServerProfile p;
        p.name = name;
        p.host = QStringLiteral("example.com");
        core::TunnelSpec a;
        a.listenPort = 8080;
        a.destHost = QStringLiteral("localhost");
        a.destPort = 80;
        core::TunnelSpec b = a;
        b.listenPort = 9090;
        p.tunnels = {a, b};
        store.upsert(p);
    }
    gui::AsyncBridge bridge;
    gui::ServerManagerDialog dlg(&bridge);
    QListWidget *profiles = nullptr;
    QListWidget *tunnels = nullptr;
    for (QListWidget *l : dlg.findChildren<QListWidget *>()) {
        if (!l->findItems(name, Qt::MatchExactly).isEmpty())
            profiles = l;
        else
            tunnels = l;
    }
    QPushButton *remove = buttonWithText(dlg, core::_t("Entfernen"));
    QPushButton *save = buttonWithText(dlg, core::_t("Speichern"));
    CHECK(profiles && tunnels && remove && save);
    if (!profiles || !tunnels || !remove || !save)
        return;
    profiles->setCurrentItem(profiles->findItems(name, Qt::MatchExactly).first());
    CHECK_EQ(tunnels->count(), 2);   // Presets sind sichtbar
    tunnels->setCurrentRow(0);
    remove->click();
    CHECK_EQ(tunnels->count(), 1);
    save->click();

    core::ProfileStore reloaded;
    reloaded.load();
    const auto saved = reloaded.get(name);
    CHECK(saved.has_value());
    if (saved) {
        CHECK_EQ(int(saved->tunnels.size()), 1);
        if (!saved->tunnels.empty())
            CHECK_EQ(saved->tunnels.front().listenPort, 9090);
    }
    reloaded.remove(name);
    reloaded.save();
}

TEST(fix_findings, libssh2_offers_zlib_compression)
{
    // Ohne zlib bot libssh2 nur "none" an — die Profiloption war wirkungslos.
    // Wie connectSession bei aktivierter Option: erst das Flag, dann bietet
    // libssh2 die Kompressionsverfahren an.
    libssh2_init(0);
    LIBSSH2_SESSION *session = libssh2_session_init();
    CHECK(session != nullptr);
    if (!session)
        return;
    libssh2_session_flag(session, LIBSSH2_FLAG_COMPRESS, 1);
    const char **algs = nullptr;
    const int n = libssh2_session_supported_algs(session, LIBSSH2_METHOD_COMP_CS, &algs);
    QStringList names;
    for (int i = 0; i < n; ++i)
        names << QString::fromLatin1(algs[i]);
    if (algs)
        libssh2_free(session, algs);
    libssh2_session_free(session);
    CHECK(names.contains(QStringLiteral("zlib@openssh.com")) || names.contains(QStringLiteral("zlib")));
}

TEST(fix_findings, last_connected_is_set_without_touching_secrets)
{
    const QString name = QStringLiteral("ncssh-selftest-lastconn-E5");
    core::ProfileStore store;
    store.load();
    core::ServerProfile p;
    p.name = name;
    p.host = QStringLiteral("example.com");
    p.authMethod = QStringLiteral("password");
    p.savePassword = true;
    p.password = QStringLiteral("geheim-E5");
    store.upsert(p);

    core::ProfileStore again;
    again.load();
    CHECK(again.touchLastConnected(name, QStringLiteral("2026-10-08T12:34:56")));
    CHECK(!again.touchLastConnected(QStringLiteral("gibt-es-nicht-E5"), QStringLiteral("x")));

    core::ProfileStore reloaded;
    reloaded.load();
    const auto saved = reloaded.get(name);
    CHECK(saved.has_value());
    if (saved)
        CHECK_EQ(saved->lastConnected, QStringLiteral("2026-10-08T12:34:56"));
#ifdef Q_OS_WIN
    CHECK_EQ(core::getSecret(name, QStringLiteral("password")).value_or(QString()),
             QStringLiteral("geheim-E5"));
#endif
    reloaded.remove(name);
    reloaded.save();
    core::deleteSecret(name, QStringLiteral("password"));
    core::deleteSecret(name, QStringLiteral("passphrase"));
}

TEST(fix_findings, github_alarm_active_checkbox_toggles_watch)
{
    std::vector<core::RepoSpec> repos;
    core::RepoSpec r;
    r.id = 1;
    r.owner = QStringLiteral("selftest");
    r.repo = QStringLiteral("repo-f6");
    r.enabled = true;
    repos.push_back(r);
    core::saveRepos(repos);

    gui::AsyncBridge bridge;
    gui::GithubAlarmManager manager(&bridge);
    gui::GithubAlarmDialog dlg(&manager);
    QTableWidget *table = dlg.findChild<QTableWidget *>();
    CHECK(table != nullptr);
    if (!table || table->rowCount() < 1)
        return;
    QTableWidgetItem *active = table->item(0, 3);
    CHECK(active != nullptr);
    if (!active)
        return;
    CHECK(active->flags() & Qt::ItemIsUserCheckable);
    CHECK_EQ(active->checkState(), Qt::Checked);
    active->setCheckState(Qt::Unchecked);
    const auto saved = core::loadRepos();
    CHECK_EQ(int(saved.size()), 1);
    if (!saved.empty())
        CHECK(!saved.front().enabled);
    core::saveRepos({});
}
