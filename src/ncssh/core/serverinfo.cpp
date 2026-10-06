#include "ncssh/core/serverinfo.hpp"

#include "ncssh/core/i18n.hpp"

#include <QHash>
#include <QSet>

namespace ncssh::core {

QString serverInfoScript()
{
    // Bewusst POSIX-sh (dash/busybox): keine bash-Eigenheiten. Jeder Block
    // verschluckt seine Fehler, damit ein fehlendes Werkzeug (ss, systemctl,
    // free) nur seinen Abschnitt leer laesst. find bekommt ein Zeitlimit,
    // falls "timeout" vorhanden ist.
    return QStringLiteral(R"SH(
LC_ALL=C; export LC_ALL
T=""; command -v timeout >/dev/null 2>&1 && T="timeout 10"
echo '##SECTION hostname'; hostname 2>/dev/null || uname -n
echo '##SECTION os'
if [ -r /etc/os-release ]; then . /etc/os-release; echo "${PRETTY_NAME:-$NAME $VERSION}"; else uname -s; fi
echo '##SECTION kernel'; uname -srm 2>/dev/null
echo '##SECTION uptime'; uptime 2>/dev/null
echo '##SECTION identity'; id 2>/dev/null
echo '##SECTION users'; getent passwd 2>/dev/null || cat /etc/passwd 2>/dev/null
echo '##SECTION admins'
getent group sudo wheel admin 2>/dev/null || grep -E '^(sudo|wheel|admin):' /etc/group 2>/dev/null
echo '##SECTION files'
chk() { for f in "$@"; do [ -f "$f" ] || continue; r=-; w=-; [ -r "$f" ] && r=r; [ -w "$f" ] && w=w; printf '%s%s\t%s\n' "$r" "$w" "$f"; done; }
chk /etc/ssh/sshd_config /etc/ssh/sshd_config.d/*.conf /etc/ssh/ssh_config \
    /etc/hosts /etc/hostname /etc/resolv.conf /etc/fstab /etc/environment \
    /etc/default/locale /etc/security/pam_env.conf \
    /etc/profile /etc/profile.d/*.sh /etc/bash.bashrc /etc/sudoers /etc/sudoers.d/* \
    /etc/crontab /etc/cron.d/* /etc/logrotate.conf \
    /etc/netplan/*.yaml /etc/network/interfaces /etc/sysctl.conf \
    /etc/nginx/nginx.conf /etc/nginx/sites-enabled/* /etc/nginx/conf.d/*.conf \
    /etc/apache2/apache2.conf /etc/apache2/sites-enabled/* /etc/httpd/conf/httpd.conf \
    /etc/caddy/Caddyfile /etc/haproxy/haproxy.cfg \
    /etc/mysql/my.cnf /etc/mysql/mariadb.conf.d/*.cnf /etc/mysql/mysql.conf.d/*.cnf \
    /etc/postgresql/*/main/postgresql.conf /etc/postgresql/*/main/pg_hba.conf \
    /etc/redis/redis.conf /etc/mongod.conf \
    /etc/php/*/fpm/php.ini /etc/php/*/cli/php.ini /etc/php/*/fpm/pool.d/*.conf \
    /etc/docker/daemon.json /etc/systemd/system/*.service \
    /etc/fail2ban/jail.local /etc/fail2ban/jail.conf /etc/ufw/ufw.conf \
    "$HOME/.bashrc" "$HOME/.profile" "$HOME/.bash_profile" "$HOME/.zshrc" "$HOME/.pam_environment" \
    "$HOME/.ssh/authorized_keys" "$HOME/.ssh/config"
for d in /home /root /var/www /srv /opt; do
  [ -d "$d" ] || continue
  $T find "$d" -maxdepth 4 \( -name node_modules -o -name .git -o -name vendor \) -prune -o -type f \
    \( -name '.env' -o -name '.env.*' -o -name '*.env' -o -name 'docker-compose*.yml' \
       -o -name 'docker-compose*.yaml' -o -name 'compose.yml' -o -name 'compose.yaml' \) \
    -print 2>/dev/null | head -100 | while IFS= read -r f; do chk "$f"; done
done
echo '##SECTION disk'; df -hP -x tmpfs -x devtmpfs -x squashfs 2>/dev/null || df -h 2>/dev/null
echo '##SECTION memory'; free -h 2>/dev/null
echo '##SECTION ports'; ss -tulpn 2>/dev/null || netstat -tulpn 2>/dev/null || netstat -an 2>/dev/null | grep -i listen
echo '##SECTION services'
systemctl list-units --type=service --state=running --no-legend --no-pager 2>/dev/null
echo '##SECTION end'
)SH");
}

QString configCategory(const QString &path)
{
    const auto has = [&](const char *part) { return path.contains(QLatin1String(part)); };
    const QString name = path.section(QLatin1Char('/'), -1);
    if (name == QLatin1String(".env") || name.startsWith(QLatin1String(".env."))
        || name.endsWith(QLatin1String(".env")) || path == QLatin1String("/etc/environment")
        || path == QLatin1String("/etc/default/locale") || name == QLatin1String("pam_env.conf")
        || name == QLatin1String(".pam_environment"))
        return _t("Umgebung (.env)");
    if (name.startsWith(QLatin1String("docker-compose")) || name.startsWith(QLatin1String("compose."))
        || has("/etc/docker/"))
        return _t("Docker");
    if (has("/.ssh/") || has("/etc/ssh/"))
        return _t("SSH");
    if (has("/nginx/") || has("/apache2/") || has("/httpd/") || has("/caddy/") || has("/haproxy/"))
        return _t("Webserver");
    if (has("/php/"))
        return _t("PHP");
    if (has("/mysql/") || has("/postgresql/") || has("/redis/") || has("mongod"))
        return _t("Datenbank");
    if (has("cron") || has("/systemd/") || has("logrotate"))
        return _t("Dienste & Zeitplan");
    if (has("/netplan/") || has("/network/") || path == QLatin1String("/etc/hosts")
        || path == QLatin1String("/etc/hostname") || path == QLatin1String("/etc/resolv.conf"))
        return _t("Netzwerk");
    if (has("sudoers") || has("fail2ban") || has("/ufw/"))
        return _t("Sicherheit");
    if (name.startsWith(QLatin1Char('.')) || has("/etc/profile") || has("bash.bashrc"))
        return _t("Shell");
    return _t("System");
}

ServerInfo parseServerInfo(const QString &output)
{
    // Abschnitte einsammeln.
    QHash<QString, QStringList> sections;
    QString current;
    const QStringList lines = output.split(QLatin1Char('\n'));
    for (QString line : lines) {
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        if (line.startsWith(QLatin1String("##SECTION "))) {
            current = line.mid(10).trimmed();
            continue;
        }
        if (!current.isEmpty())
            sections[current] << line;
    }
    const auto text = [&](const char *key) {
        QStringList l = sections.value(QString::fromLatin1(key));
        while (!l.isEmpty() && l.last().trimmed().isEmpty())
            l.removeLast();
        return l.join(QLatin1Char('\n')).trimmed();
    };

    ServerInfo info;
    info.hostname = text("hostname");
    info.os = text("os");
    info.kernel = text("kernel");
    info.uptime = text("uptime");
    info.identity = text("identity");
    info.disk = text("disk");
    info.memory = text("memory");
    info.ports = text("ports");
    info.services = text("services");

    // Mitglieder der Admin-Gruppen ("sudo:x:27:alice,bob").
    QSet<QString> admins;
    for (const QString &line : sections.value(QStringLiteral("admins"))) {
        const QStringList parts = line.trimmed().split(QLatin1Char(':'));
        if (parts.size() >= 4)
            for (const QString &m : parts.at(3).split(QLatin1Char(','), Qt::SkipEmptyParts))
                admins.insert(m.trimmed());
    }

    // passwd: name:x:uid:gid:gecos:home:shell
    for (const QString &line : sections.value(QStringLiteral("users"))) {
        const QStringList p = line.trimmed().split(QLatin1Char(':'));
        if (p.size() < 7)
            continue;
        ServerUser u;
        u.name = p.at(0);
        u.uid = p.at(2).toInt();
        u.gid = p.at(3).toInt();
        u.gecos = p.at(4).section(QLatin1Char(','), 0, 0);
        u.home = p.at(5);
        u.shell = p.at(6);
        u.login = !u.shell.endsWith(QLatin1String("nologin")) && !u.shell.endsWith(QLatin1String("/false"))
                  && !u.shell.endsWith(QLatin1String("/sync")) && !u.shell.isEmpty();
        u.admin = u.uid == 0 || admins.contains(u.name);
        info.users.push_back(u);
    }

    // Dateien: "rw<TAB>/pfad" — doppelte (Glob + find) nur einmal.
    QSet<QString> seen;
    for (const QString &line : sections.value(QStringLiteral("files"))) {
        const int tab = line.indexOf(QLatin1Char('\t'));
        if (tab != 2)
            continue;
        ServerConfigFile f;
        f.path = line.mid(tab + 1).trimmed();
        if (f.path.isEmpty() || seen.contains(f.path))
            continue;
        seen.insert(f.path);
        f.readable = line.at(0) == QLatin1Char('r');
        f.writable = line.at(1) == QLatin1Char('w');
        f.category = configCategory(f.path);
        info.files.push_back(f);
    }
    return info;
}

} // namespace ncssh::core
