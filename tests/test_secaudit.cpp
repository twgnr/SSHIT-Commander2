// Tests fuer die Sicherheits-Audit-Parser (core/secaudit) — rein, ohne I/O.
#include "tests/harness.hpp"

#include "ncssh/core/secaudit.hpp"

#include <QHash>

using namespace ncssh::core;

namespace {
// Befunde auf {direktive -> schwere} reduzieren.
QHash<QString, QString> severityByDirective(const std::vector<SshdFinding> &items)
{
    QHash<QString, QString> out;
    for (const auto &f : items)
        out.insert(f.directive, f.severity);
    return out;
}

QHash<QString, QString> severityByAccount(const std::vector<AccountFinding> &items)
{
    QHash<QString, QString> out;
    for (const auto &f : items)
        out.insert(f.account, f.severity);
    return out;
}

bool hasSocket(const std::vector<ListeningSocket> &list, const QString &proto,
               const QString &host, const QString &port)
{
    for (const auto &s : list) {
        if (s.proto == proto && s.host == host && s.port == port)
            return true;
    }
    return false;
}

bool hasPublic(const std::vector<PublicPort> &list, const QString &proto, const QString &port)
{
    for (const auto &p : list) {
        if (p.proto == proto && p.port == port)
            return true;
    }
    return false;
}
} // namespace

// --- sshd_config-Parser ------------------------------------------------------
TEST(secaudit, parse_sshd_config_basics)
{
    const auto cfg = parseSshdConfig(QStringLiteral(
        "# Kommentar\nPermitRootLogin yes\nPasswordAuthentication no\n"
        "Port 22\n\nMatch User bob\n  PasswordAuthentication yes\n"));
    CHECK_EQ(cfg.value(QStringLiteral("permitrootlogin")), QStringLiteral("yes"));
    // Match-Block wird ignoriert
    CHECK_EQ(cfg.value(QStringLiteral("passwordauthentication")), QStringLiteral("no"));
    CHECK_EQ(cfg.value(QStringLiteral("port")), QStringLiteral("22"));
}

TEST(secaudit, audit_sshd_flags_risky)
{
    const auto cfg = parseSshdConfig(QStringLiteral(
        "PermitRootLogin yes\nPermitEmptyPasswords yes\nPasswordAuthentication yes\n"
        "X11Forwarding yes\nCiphers aes128-cbc,3des-cbc\nMACs hmac-md5\nProtocol 2,1\n"));
    const auto items = severityByDirective(auditSshd(cfg));
    CHECK_EQ(items.value(QStringLiteral("PermitRootLogin")), QStringLiteral("Important"));
    CHECK_EQ(items.value(QStringLiteral("PermitEmptyPasswords")), QStringLiteral("Critical"));
    CHECK_EQ(items.value(QStringLiteral("PasswordAuthentication")), QStringLiteral("Moderate"));
    CHECK_EQ(items.value(QStringLiteral("Protocol")), QStringLiteral("Critical"));   // enthaelt "1"
    CHECK_EQ(items.value(QStringLiteral("Ciphers")), QStringLiteral("Important"));   // CBC
    CHECK(items.contains(QStringLiteral("MACs")));
    CHECK(items.contains(QStringLiteral("X11Forwarding")));
}

TEST(secaudit, audit_sshd_clean_config_has_no_findings)
{
    const auto cfg = parseSshdConfig(QStringLiteral(
        "PermitRootLogin no\nPasswordAuthentication no\nX11Forwarding no\n"
        "PubkeyAuthentication yes\nProtocol 2\n"));
    CHECK(auditSshd(cfg).empty());
}

TEST(secaudit, parse_ufw_status)
{
    CHECK_EQ(parseUfwStatus(QStringLiteral("Status: active\n")).value_or(QString()),
             QStringLiteral("active"));
    CHECK_EQ(parseUfwStatus(QStringLiteral("Status: inactive")).value_or(QString()),
             QStringLiteral("inactive"));
    CHECK(!parseUfwStatus(QStringLiteral("ERROR: You need to be root")).has_value());
}

// --- bestehende Parser (Regressionsschutz) ----------------------------------
TEST(secaudit, parse_apt_upgrade_security_flag)
{
    const auto out = parseAptUpgrade(QStringLiteral(
        "Inst libssl3 [3.0.2] (3.0.8 Ubuntu:22.04/jammy-security [amd64])\n"
        "Inst bash [5.1] (5.1.1 Ubuntu:22.04/jammy-updates [amd64])\n"));
    QHash<QString, bool> sec;
    for (const auto &u : out)
        sec.insert(u.package, u.security);
    CHECK_EQ(sec.value(QStringLiteral("libssl3")), true);
    CHECK_EQ(sec.value(QStringLiteral("bash")), false);
}

TEST(secaudit, parse_dnf_cves)
{
    const auto out = parseDnfCves(
        QStringLiteral("CVE-2024-1234 Important/Sec. openssl-libs\nnoise line\n"));
    CHECK_EQ(out.size(), size_t(1));
    CHECK_EQ(out[0].cve, QStringLiteral("CVE-2024-1234"));
    CHECK_EQ(out[0].severity, QStringLiteral("Important/Sec."));
    CHECK_EQ(out[0].package, QStringLiteral("openssl-libs"));
}

TEST(secaudit, listening_and_public_ports)
{
    const QString text = QStringLiteral(
        "Netid State  Recv-Q Send-Q Local Address:Port Peer Address:Port\n"
        "tcp   LISTEN 0 128 0.0.0.0:22 0.0.0.0:*\n"
        "tcp   LISTEN 0 128 127.0.0.1:5432 0.0.0.0:*\n"
        "tcp   LISTEN 0 128 [::]:80 [::]:*\n"
        "udp   UNCONN 0 0   0.0.0.0:68 0.0.0.0:*\n");
    const auto lst = parseListening(text);
    CHECK(hasSocket(lst, QStringLiteral("tcp"), QStringLiteral("0.0.0.0"), QStringLiteral("22")));
    CHECK(hasSocket(lst, QStringLiteral("tcp"), QStringLiteral("127.0.0.1"),
                    QStringLiteral("5432")));

    const auto pub = publicPorts(lst);
    CHECK(hasPublic(pub, QStringLiteral("tcp"), QStringLiteral("22")));
    CHECK(hasPublic(pub, QStringLiteral("tcp"), QStringLiteral("80")));
    CHECK(hasPublic(pub, QStringLiteral("udp"), QStringLiteral("68")));
    // nur localhost -> nicht oeffentlich
    CHECK(!hasPublic(pub, QStringLiteral("tcp"), QStringLiteral("5432")));
}

TEST(secaudit, audit_accounts)
{
    const QString passwd = QStringLiteral(
        "root:x:0:0:root:/root:/bin/bash\n"
        "backdoor:x:0:0::/root:/bin/bash\n"
        "bob:x:1000:1000::/home/bob:/bin/bash\n");
    const QString shadow = QStringLiteral("root:$6$abc:1:::::\nguest::1::::::\n");
    const auto names = severityByAccount(auditAccounts(passwd, shadow));
    // UID 0 ausser root
    CHECK_EQ(names.value(QStringLiteral("backdoor")), QStringLiteral("Critical"));
    // leeres Passwort
    CHECK_EQ(names.value(QStringLiteral("guest")), QStringLiteral("Critical"));
    CHECK(!names.contains(QStringLiteral("bob")));
    CHECK(!names.contains(QStringLiteral("root")));
}

TEST(secaudit, unattended_enabled)
{
    CHECK_EQ(unattendedEnabled(QStringLiteral("APT::Periodic::Unattended-Upgrade \"1\";")), true);
    CHECK_EQ(unattendedEnabled(QStringLiteral("APT::Periodic::Unattended-Upgrade \"0\";")), false);
    // auskommentiert
    CHECK_EQ(unattendedEnabled(QStringLiteral("// APT::Periodic::Unattended-Upgrade \"1\";")),
             false);
    CHECK_EQ(unattendedEnabled(QString()), false);
}

TEST(secaudit, parse_cert_check)
{
    const auto out = parseCertCheck(QStringLiteral(
        "OK /etc/letsencrypt/live/a/cert.pem\nSOON /etc/letsencrypt/live/b/cert.pem\n"));
    CHECK_EQ(out, (QStringList{QStringLiteral("/etc/letsencrypt/live/b/cert.pem")}));
}

TEST(secaudit, parse_os_release)
{
    const auto os = parseOsRelease(QStringLiteral(
        "NAME=\"Ubuntu\"\nID=ubuntu\nVERSION_ID=\"22.04\"\n# Kommentar\n"));
    CHECK_EQ(os.value(QStringLiteral("ID")), QStringLiteral("ubuntu"));
    CHECK_EQ(os.value(QStringLiteral("VERSION_ID")), QStringLiteral("22.04"));
    CHECK_EQ(os.value(QStringLiteral("NAME")), QStringLiteral("Ubuntu"));
}
