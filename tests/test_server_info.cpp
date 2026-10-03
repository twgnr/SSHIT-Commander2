// Server-Info: Skriptausgabe zerlegen, Dialog befuellen, Datei oeffnen.
#include "tests/harness.hpp"

#include "ncssh/core/serverinfo.hpp"
#include "ncssh/gui/bridge.hpp"
#include "ncssh/gui/server_info_dialog.hpp"

#include <QCheckBox>
#include <QTableWidget>

using namespace ncssh;

namespace {

const char *kSample =
    "##SECTION hostname\n"
    "web01\n"
    "##SECTION os\n"
    "Debian GNU/Linux 12 (bookworm)\n"
    "##SECTION kernel\n"
    "Linux 6.1.0-18-amd64 x86_64\n"
    "##SECTION uptime\n"
    " 10:00:00 up 3 days,  1 user,  load average: 0.10, 0.05, 0.01\n"
    "##SECTION identity\n"
    "uid=0(root) gid=0(root) groups=0(root)\n"
    "##SECTION users\n"
    "root:x:0:0:root:/root:/bin/bash\n"
    "daemon:x:1:1:daemon:/usr/sbin:/usr/sbin/nologin\n"
    "www-data:x:33:33:www-data:/var/www:/usr/sbin/nologin\n"
    "alice:x:1000:1000:Alice Example,,,:/home/alice:/bin/bash\n"
    "bob:x:1001:1001::/home/bob:/bin/zsh\n"
    "nobody:x:65534:65534:nobody:/nonexistent:/usr/sbin/nologin\n"
    "##SECTION admins\n"
    "sudo:x:27:alice\n"
    "##SECTION files\n"
    "rw\t/etc/ssh/sshd_config\n"
    "rw\t/etc/nginx/sites-enabled/default\n"
    "r-\t/etc/hosts\n"
    "--\t/home/bob/app/.env\n"
    "rw\t/etc/ssh/sshd_config\n"
    "rw\t/opt/stack/docker-compose.yml\n"
    "##SECTION disk\n"
    "Filesystem Size Used Avail Use% Mounted on\n"
    "/dev/sda1 50G 10G 40G 20% /\n"
    "##SECTION memory\n"
    "Mem: 4.0Gi 1.0Gi 3.0Gi\n"
    "##SECTION ports\n"
    "tcp LISTEN 0 128 0.0.0.0:22 0.0.0.0:*\n"
    "##SECTION services\n"
    "nginx.service loaded active running nginx\n"
    "##SECTION end\n";

} // namespace

TEST(server_info, parses_script_output)
{
    const core::ServerInfo info = core::parseServerInfo(QString::fromUtf8(kSample));
    CHECK_EQ(info.hostname, QStringLiteral("web01"));
    CHECK_EQ(info.os, QStringLiteral("Debian GNU/Linux 12 (bookworm)"));
    CHECK(info.identity.startsWith(QStringLiteral("uid=0(root)")));
    CHECK_EQ(int(info.users.size()), 6);
    const core::ServerUser &alice = info.users.at(3);
    CHECK_EQ(alice.name, QStringLiteral("alice"));
    CHECK_EQ(alice.uid, 1000);
    CHECK_EQ(alice.gecos, QStringLiteral("Alice Example"));
    CHECK(alice.login);
    CHECK(alice.admin);                 // Mitglied von sudo
    CHECK(info.users.at(0).admin);      // root
    CHECK(!info.users.at(1).login);     // nologin
    CHECK(!info.users.at(4).admin);

    // Doppelte Pfade nur einmal; Zugriff und Kategorie aus der Zeile/dem Pfad.
    CHECK_EQ(int(info.files.size()), 5);
    CHECK(info.files.at(0).readable && info.files.at(0).writable);
    CHECK_EQ(info.files.at(2).path, QStringLiteral("/etc/hosts"));
    CHECK(info.files.at(2).readable && !info.files.at(2).writable);
    CHECK(!info.files.at(3).readable);
    CHECK_EQ(info.files.at(0).category, core::configCategory(QStringLiteral("/etc/ssh/sshd_config")));
    CHECK(info.services.contains(QStringLiteral("nginx.service")));
    CHECK(info.disk.contains(QStringLiteral("/dev/sda1")));
}

TEST(server_info, categories_group_config_files)
{
    const QString env = core::configCategory(QStringLiteral("/home/bob/app/.env"));
    CHECK_EQ(core::configCategory(QStringLiteral("/srv/x/.env.production")), env);
    CHECK_EQ(core::configCategory(QStringLiteral("/etc/environment")), env);
    CHECK(core::configCategory(QStringLiteral("/etc/nginx/nginx.conf"))
          == core::configCategory(QStringLiteral("/etc/apache2/apache2.conf")));
    CHECK(core::configCategory(QStringLiteral("/opt/a/docker-compose.yml"))
          != core::configCategory(QStringLiteral("/etc/hosts")));
    CHECK(core::configCategory(QStringLiteral("/root/.ssh/authorized_keys"))
          == core::configCategory(QStringLiteral("/etc/ssh/sshd_config")));
}

TEST(server_info, script_is_posix_and_sectioned)
{
    const QString script = core::serverInfoScript();
    for (const char *section : {"hostname", "os", "users", "files", "ports", "services", "end"})
        CHECK(script.contains(QStringLiteral("##SECTION %1").arg(QLatin1String(section))));
    CHECK(!script.contains(QStringLiteral("[[")));   // kein bash-Test
}

TEST(server_info, dialog_lists_files_and_opens_selected)
{
    gui::AsyncBridge bridge;
    QString opened;
    gui::ServerInfoDialog dlg(&bridge, nullptr, [&](const QString &path) { opened = path; });
    dlg.showInfo(core::parseServerInfo(QString::fromUtf8(kSample)));

    auto *files = dlg.findChild<QTableWidget *>(QStringLiteral("ServerInfoFiles"));
    auto *users = dlg.findChild<QTableWidget *>(QStringLiteral("ServerInfoUsers"));
    CHECK(files && users);
    CHECK_EQ(files->rowCount(), 5);
    // Standard: root, alice, bob — keine Dienstkonten.
    CHECK_EQ(users->rowCount(), 3);
    dlg.findChild<QCheckBox *>()->setChecked(true);
    CHECK_EQ(users->rowCount(), 6);

    int row = -1;
    for (int r = 0; r < files->rowCount(); ++r)
        if (files->item(r, 1)->text() == QLatin1String("/etc/hosts"))
            row = r;
    CHECK(row >= 0);
    files->setCurrentCell(row, 1);
    emit files->cellDoubleClicked(row, 1);
    CHECK_EQ(opened, QStringLiteral("/etc/hosts"));
}
