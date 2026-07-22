#include "ncssh/core/secaudit.hpp"

#include "ncssh/core/i18n.hpp"

#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <stdexcept>

namespace ncssh::core {

namespace {

// Zerlegt an Whitespace-Laeufen (entspricht Python str.split() ohne Argument).
QStringList whitespaceSplit(const QString &s)
{
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    return s.split(ws, Qt::SkipEmptyParts);
}

// Trennt am ersten Whitespace-Lauf in [kopf, rest] (Python str.split(None, 1)).
QStringList splitOnce(const QString &s)
{
    int i = 0;
    while (i < s.size() && !s.at(i).isSpace())
        ++i;
    if (i >= s.size())
        return {s};
    int j = i;
    while (j < s.size() && s.at(j).isSpace())
        ++j;
    return {s.left(i), s.mid(j)};
}

// Entfernt fuehrende/abschliessende Zeichen aus chars (Python str.strip(chars)).
QString stripChars(const QString &s, const QString &chars)
{
    int start = 0;
    int end = s.size();
    while (start < end && chars.contains(s.at(start)))
        ++start;
    while (end > start && chars.contains(s.at(end - 1)))
        --end;
    return s.mid(start, end - start);
}

// Entfernt nur abschliessenden Whitespace (Python str.rstrip()).
QString rstrip(const QString &s)
{
    int end = s.size();
    while (end > 0 && s.at(end - 1).isSpace())
        --end;
    return s.left(end);
}

bool isDigits(const QString &s)
{
    if (s.isEmpty())
        return false;
    for (const QChar c : s)
        if (!c.isDigit())
            return false;
    return true;
}

std::optional<double> parseCvssVector(const QString &vectorIn)
{
    const QString vector = vectorIn.trimmed();
    if (vector.isEmpty())
        return std::nullopt;
    bool ok = false;  // manche Eintraege sind bereits numerisch
    const double num = vector.toDouble(&ok);
    if (ok)
        return num;
    // Heuristik: Netzwerk-erreichbar + geringe Komplexitaet => tendenziell hoch.
    QHash<QString, QString> parts;
    const auto tokens = vector.split(QLatin1Char('/'));
    for (const QString &kv : tokens) {
        if (!kv.contains(QLatin1Char(':')) || kv.startsWith(QLatin1String("CVSS")))
            continue;
        const int c = kv.indexOf(QLatin1Char(':'));
        parts.insert(kv.left(c), kv.mid(c + 1));
    }
    if (parts.isEmpty())
        return std::nullopt;
    const QString av = parts.value(QStringLiteral("AV"));
    const QString ac = parts.value(QStringLiteral("AC"));
    int impact = 0;
    for (const char *m : {"C", "I", "A"}) {
        const QString val = parts.value(QString::fromLatin1(m), QStringLiteral("N"));
        if (val == QLatin1String("H") || val == QLatin1String("L"))
            ++impact;
    }
    double base = 4.0;
    if (av == QLatin1String("N"))
        base += 3.0;
    else if (av == QLatin1String("A") || av == QLatin1String("L"))
        base += 1.0;
    if (ac == QLatin1String("L"))
        base += 1.0;
    base += impact * 0.7;
    return std::min(base, 10.0);
}

std::optional<double> maxCvssScore(const QJsonArray &severityList)
{
    std::optional<double> best;
    for (const QJsonValue &entry : severityList) {
        const QString raw = entry.toObject().value(QStringLiteral("score")).toString();
        const std::optional<double> score = parseCvssVector(raw);
        if (score.has_value() && (!best.has_value() || *score > *best))
            best = score;
    }
    return best;
}

QString cvssBucket(double score)
{
    if (score >= 9.0)
        return QStringLiteral("Critical");
    if (score >= 7.0)
        return QStringLiteral("Important");
    if (score >= 4.0)
        return QStringLiteral("Moderate");
    return QStringLiteral("Low");
}

// Blockierender HTTP-Aufruf ueber einen lokalen Event-Loop (im Worker-Thread).
QByteArray httpRequest(const QNetworkRequest &req, const QByteArray *postData, int timeoutSec)
{
    QNetworkAccessManager mgr;
    QNetworkReply *reply = postData ? mgr.post(req, *postData) : mgr.get(req);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(std::max(1, timeoutSec) * 1000);
    loop.exec();

    if (!reply->isFinished()) {
        reply->abort();
        reply->deleteLater();
        throw std::runtime_error("OSV-Anfrage: Zeitueberschreitung");
    }
    if (reply->error() != QNetworkReply::NoError) {
        const QString err = reply->errorString();
        reply->deleteLater();
        throw std::runtime_error(("OSV-Anfrage fehlgeschlagen: " + err).toStdString());
    }
    const QByteArray body = reply->readAll();
    reply->deleteLater();
    return body;
}

} // namespace

const QStringList &keyPackages()
{
    // Kernkomponenten (bewusst inkl. gaengiger Server-/Anwendungssoftware, sonst
    // tauchen CVEs eines nicht abgefragten Pakets nie auf).
    static const QStringList packages = {
        // System / Krypto
        QStringLiteral("openssl"), QStringLiteral("openssh-server"),
        QStringLiteral("openssh-client"), QStringLiteral("sudo"), QStringLiteral("bash"),
        QStringLiteral("libc6"), QStringLiteral("zlib1g"), QStringLiteral("curl"),
        QStringLiteral("wget"), QStringLiteral("python3"), QStringLiteral("libssl3"),
        QStringLiteral("libssl1.1"), QStringLiteral("glibc"),
        // Webserver / Reverse-Proxy
        QStringLiteral("nginx"), QStringLiteral("nginx-core"), QStringLiteral("nginx-full"),
        QStringLiteral("apache2"), QStringLiteral("apache2-bin"), QStringLiteral("httpd"),
        QStringLiteral("haproxy"), QStringLiteral("lighttpd"), QStringLiteral("caddy"),
        QStringLiteral("tomcat9"),
        // Datenbanken / Cache
        QStringLiteral("mysql-server"), QStringLiteral("mysql-server-8.0"),
        QStringLiteral("mariadb-server"), QStringLiteral("postgresql"),
        QStringLiteral("redis-server"), QStringLiteral("redis"),
        QStringLiteral("mongodb-server"), QStringLiteral("memcached"),
        // Sprachen / Laufzeiten
        QStringLiteral("php"), QStringLiteral("php-fpm"), QStringLiteral("php8.1-fpm"),
        QStringLiteral("php7.4-fpm"), QStringLiteral("nodejs"),
        QStringLiteral("openjdk-17-jre-headless"),
        // Container / Infrastruktur
        QStringLiteral("docker.io"), QStringLiteral("docker-ce"), QStringLiteral("containerd"),
        QStringLiteral("samba"), QStringLiteral("vsftpd"), QStringLiteral("bind9"),
        QStringLiteral("exim4"), QStringLiteral("postfix"),
    };
    return packages;
}

QHash<QString, QString> parseOsRelease(const QString &text)
{
    QHash<QString, QString> data;
    const auto lines = text.split(QLatin1Char('\n'));
    for (QString line : lines) {
        line = line.trimmed();
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq >= 0 && !line.startsWith(QLatin1Char('#'))) {
            const QString key = line.left(eq).trimmed();
            QString value = line.mid(eq + 1).trimmed();
            value = stripChars(value, QStringLiteral("\""));
            value = stripChars(value, QStringLiteral("'"));
            data.insert(key, value);
        }
    }
    return data;
}

std::vector<AptUpgrade> parseAptUpgrade(const QString &text)
{
    std::vector<AptUpgrade> out;
    static const QRegularExpression re(
        QStringLiteral("Inst\\s+(\\S+)\\s+(?:\\[[^\\]]*\\]\\s+)?\\(([^)]*)\\)"));
    const auto lines = text.split(QLatin1Char('\n'));
    for (QString line : lines) {
        line = line.trimmed();
        if (!line.startsWith(QLatin1String("Inst ")))
            continue;
        const auto m = re.match(line);
        if (!m.hasMatch())
            continue;
        const QString paren = m.captured(2);
        const QStringList parts = whitespaceSplit(paren);
        AptUpgrade u;
        u.package = m.captured(1);
        u.newVersion = parts.isEmpty() ? QString() : parts.first();
        u.security = paren.toLower().contains(QLatin1String("security"));
        out.push_back(u);
    }
    return out;
}

std::vector<DnfCve> parseDnfCves(const QString &text)
{
    std::vector<DnfCve> out;
    const auto lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QStringList parts = whitespaceSplit(line);
        if (parts.size() >= 3 && parts.at(0).toUpper().startsWith(QLatin1String("CVE-")))
            out.push_back({parts.at(0), parts.at(1), parts.at(2)});
    }
    return out;
}

QHash<QString, QString> parseSshdConfig(const QString &text)
{
    QHash<QString, QString> cfg;
    const auto lines = text.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        if (line.toLower().startsWith(QLatin1String("match ")))
            break;
        const QStringList parts = splitOnce(line);
        if (parts.size() == 2)
            cfg.insert(parts.at(0).toLower(), parts.at(1).trimmed());
    }
    return cfg;
}

std::vector<SshdFinding> auditSshd(const QHash<QString, QString> &cfg)
{
    std::vector<SshdFinding> out;
    const auto g = [&](const char *key) {
        return cfg.value(QString::fromLatin1(key)).trimmed();
    };

    if (g("permitrootlogin").toLower() == QLatin1String("yes"))
        out.push_back({QStringLiteral("PermitRootLogin"), g("permitrootlogin"),
                       QStringLiteral("Important"),
                       _t("Direkter Root-Login per SSH ist erlaubt. Besser 'prohibit-password' oder 'no'.")});
    if (g("permitemptypasswords").toLower() == QLatin1String("yes"))
        out.push_back({QStringLiteral("PermitEmptyPasswords"), g("permitemptypasswords"),
                       QStringLiteral("Critical"),
                       _t("Leere Passwörter sind erlaubt — unbedingt auf 'no' setzen.")});
    if (g("passwordauthentication").toLower() == QLatin1String("yes"))
        out.push_back({QStringLiteral("PasswordAuthentication"), g("passwordauthentication"),
                       QStringLiteral("Moderate"),
                       _t("Passwort-Login ist aktiv. Key-Authentifizierung ist sicherer.")});
    const QString proto = g("protocol");
    if (!proto.isEmpty()) {
        bool hasV1 = false;
        for (const QString &p : proto.split(QLatin1Char(',')))
            if (p.trimmed() == QLatin1String("1"))
                hasV1 = true;
        if (hasV1)
            out.push_back({QStringLiteral("Protocol"), proto, QStringLiteral("Critical"),
                           _t("SSH-Protokoll 1 ist unsicher — nur Protokoll 2 verwenden.")});
    }
    if (g("x11forwarding").toLower() == QLatin1String("yes"))
        out.push_back({QStringLiteral("X11Forwarding"), g("x11forwarding"),
                       QStringLiteral("Low"),
                       _t("X11-Weiterleitung ist aktiv — nur bei Bedarf einschalten.")});
    if (g("pubkeyauthentication").toLower() == QLatin1String("no"))
        out.push_back({QStringLiteral("PubkeyAuthentication"), g("pubkeyauthentication"),
                       QStringLiteral("Moderate"),
                       _t("Public-Key-Authentifizierung ist deaktiviert.")});
    const QString ciphers = g("ciphers").toLower();
    if (ciphers.contains(QLatin1String("cbc")) || ciphers.contains(QLatin1String("arcfour"))
        || ciphers.contains(QLatin1String("3des")) || ciphers.contains(QLatin1String("des-")))
        out.push_back({QStringLiteral("Ciphers"), g("ciphers"), QStringLiteral("Important"),
                       _t("Veraltete Cipher konfiguriert (CBC/arcfour/3DES) — moderne Algorithmen verwenden.")});
    const QString macs = g("macs").toLower();
    if (macs.contains(QLatin1String("md5")) || macs.contains(QLatin1String("-96"))
        || macs.contains(QLatin1String("ripemd")))
        out.push_back({QStringLiteral("MACs"), g("macs"), QStringLiteral("Moderate"),
                       _t("Schwache MAC-Algorithmen konfiguriert (MD5/-96).")});
    return out;
}

std::optional<QString> parseUfwStatus(const QString &text)
{
    const auto lines = text.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString low = raw.trimmed().toLower();
        if (low.startsWith(QLatin1String("status:")))
            return low.contains(QLatin1String("inactive")) ? QStringLiteral("inactive")
                                                            : QStringLiteral("active");
    }
    return std::nullopt;
}

std::vector<ListeningSocket> parseListening(const QString &text)
{
    std::vector<ListeningSocket> out;
    const auto lines = text.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QStringList parts = whitespaceSplit(raw);
        if (parts.size() < 5)
            continue;
        const QString proto = parts.at(0).toLower();
        if (proto != QLatin1String("tcp") && proto != QLatin1String("udp"))
            continue;  // Kopfzeile/Sonstiges ueberspringen
        if (proto == QLatin1String("tcp") && parts.at(1).toUpper() != QLatin1String("LISTEN"))
            continue;
        const QString field = parts.at(4);
        const int colon = field.lastIndexOf(QLatin1Char(':'));
        if (colon < 0)
            continue;
        const QString port = field.mid(colon + 1);
        if (port.isEmpty())
            continue;
        const QString host = stripChars(field.left(colon), QStringLiteral("[]"));
        out.push_back({proto, host, port});
    }
    return out;
}

std::vector<PublicPort> publicPorts(const std::vector<ListeningSocket> &listening)
{
    std::vector<PublicPort> res;
    QSet<QString> seen;
    for (const ListeningSocket &sock : listening) {
        if (sock.host == QLatin1String("0.0.0.0") || sock.host == QLatin1String("::")
            || sock.host == QLatin1String("*") || sock.host.isEmpty()) {
            const QString key = sock.proto + QLatin1Char('\x1f') + sock.port;
            if (!seen.contains(key)) {
                seen.insert(key);
                res.push_back({sock.proto, sock.port});
            }
        }
    }
    return res;
}

std::vector<AccountFinding> auditAccounts(const QString &passwd, const QString &shadow)
{
    std::vector<AccountFinding> out;
    for (const QString &line : passwd.split(QLatin1Char('\n'))) {
        const QStringList p = line.split(QLatin1Char(':'));
        if (p.size() >= 3 && p.at(2) == QLatin1String("0") && !p.at(0).isEmpty()
            && p.at(0) != QLatin1String("root"))
            out.push_back({p.at(0), QStringLiteral("UID 0"), QStringLiteral("Critical"),
                           _t("Zusätzliches Konto mit Root-Rechten (UID 0).")});
    }
    for (const QString &line : shadow.split(QLatin1Char('\n'))) {
        const QStringList p = line.split(QLatin1Char(':'));
        if (p.size() >= 2 && p.at(1).isEmpty() && !p.at(0).isEmpty())
            out.push_back({p.at(0), _t("leeres Passwort"), QStringLiteral("Critical"),
                           _t("Konto ohne Passwort — Anmeldung ohne Passwort möglich.")});
    }
    return out;
}

bool unattendedEnabled(const QString &text)
{
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1String("//")))
            continue;
        if (line.contains(QLatin1String("Unattended-Upgrade")) && line.contains(QLatin1String("\"1\"")))
            return true;
    }
    return false;
}

QStringList parseCertCheck(const QString &text)
{
    QStringList out;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        if (line.startsWith(QLatin1String("SOON "))) {
            const QStringList parts = splitOnce(line);
            if (parts.size() == 2)
                out << parts.at(1);
        }
    }
    return out;
}

VulnSummary summarizeVuln(const QJsonObject &vuln)
{
    VulnSummary r;
    r.id = vuln.value(QStringLiteral("id")).toString(QStringLiteral("?"));
    QString summary = vuln.value(QStringLiteral("summary")).toString();
    if (summary.isEmpty())
        summary = vuln.value(QStringLiteral("details")).toString();
    summary = summary.trimmed();
    if (summary.length() > 400)
        summary = rstrip(summary.left(397)) + QStringLiteral("…");
    r.summary = summary;

    // Schwere: zuerst distro-/DB-spezifisches Label, dann CVSS-Score.
    QString sev;
    const QJsonObject dbs = vuln.value(QStringLiteral("database_specific")).toObject();
    for (const QString &key : {QStringLiteral("severity"), QStringLiteral("Severity")}) {
        if (dbs.value(key).isString()) {
            sev = dbs.value(key).toString();
            break;
        }
    }
    if (sev.isEmpty()) {
        const std::optional<double> score =
            maxCvssScore(vuln.value(QStringLiteral("severity")).toArray());
        if (score.has_value())
            sev = cvssBucket(*score);
    }

    QString url;
    for (const QJsonValue &refVal : vuln.value(QStringLiteral("references")).toArray()) {
        const QJsonObject ref = refVal.toObject();
        const QString u = ref.value(QStringLiteral("url")).toString();
        if (!u.isEmpty()) {
            url = u;
            const QString type = ref.value(QStringLiteral("type")).toString();
            if (type == QLatin1String("ADVISORY") || type == QLatin1String("REPORT"))
                break;
        }
    }

    r.severity = sev.isEmpty() ? QStringLiteral("CVE") : sev;
    r.url = url;
    return r;
}

std::optional<QString> osvEcosystem(const QString &osIdIn, const QString &versionIdIn)
{
    const QString osId = osIdIn.toLower();
    const QString versionId = versionIdIn;
    if (osId == QLatin1String("debian")) {
        const QString major = versionId.split(QLatin1Char('.')).value(0);
        return major.isEmpty() ? std::optional<QString>()
                               : std::optional<QString>(QStringLiteral("Debian:%1").arg(major));
    }
    if (osId == QLatin1String("ubuntu")) {
        if (versionId.isEmpty())
            return std::nullopt;
        // OSV nutzt fuer LTS-Releases (xx.04 mit gerader Hauptnummer) das Suffix
        // ":LTS"; fuer Zwischenrelease (z.B. 23.10) ohne Suffix.
        const QString mm = QStringList(versionId.split(QLatin1Char('.')).mid(0, 2)).join(QLatin1Char('.'));
        const QStringList parts = mm.split(QLatin1Char('.'));
        const bool isLts = (parts.size() == 2 && parts.at(1) == QLatin1String("04")
                            && isDigits(parts.at(0)) && parts.at(0).toInt() % 2 == 0);
        return isLts ? std::optional<QString>(QStringLiteral("Ubuntu:%1:LTS").arg(mm))
                     : std::optional<QString>(QStringLiteral("Ubuntu:%1").arg(mm));
    }
    if (osId == QLatin1String("alpine")) {
        const QString mm = QStringList(versionId.split(QLatin1Char('.')).mid(0, 2)).join(QLatin1Char('.'));
        return mm.isEmpty() ? std::optional<QString>()
                            : std::optional<QString>(QStringLiteral("Alpine:v%1").arg(mm));
    }
    return std::nullopt;
}

QJsonObject osvQuerybatch(const QJsonArray &queries, int timeoutSec)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("queries"), queries);
    QByteArray data = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QNetworkRequest req(QUrl(QStringLiteral("https://api.osv.dev/v1/querybatch")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    const QByteArray body = httpRequest(req, &data, timeoutSec);
    return QJsonDocument::fromJson(body).object();
}

QJsonObject osvGetVuln(const QString &vulnId, int timeoutSec)
{
    const QString url = QStringLiteral("https://api.osv.dev/v1/vulns/")
        + QString::fromLatin1(QUrl::toPercentEncoding(vulnId));
    QNetworkRequest req{QUrl(url)};
    const QByteArray body = httpRequest(req, nullptr, timeoutSec);
    return QJsonDocument::fromJson(body).object();
}

} // namespace ncssh::core
