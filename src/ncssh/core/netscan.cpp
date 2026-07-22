#include "ncssh/core/netscan.hpp"

#include "ncssh/core/oui_data.hpp"

#include <QElapsedTimer>
#include <QHostInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QThread>
#include <QMutex>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QHostAddress>
#include <QUrl>
#include <QSet>
#include <atomic>
#include <tuple>

#ifdef Q_OS_WIN
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#endif

namespace ncssh::core {

static const int kMaxHosts = 4096;

const QHash<QString, QVector<int>> &portPresets()
{
    static const QHash<QString, QVector<int>> presets = {
        {QStringLiteral("common"), {22, 80, 135, 139, 443, 445, 3389, 8080}},
        {QStringLiteral("smb"), {139, 445}},
        {QStringLiteral("web"), {80, 443, 8080, 8443}},
        {QStringLiteral("remote"), {22, 3389, 5900}},
        {QStringLiteral("all"), {21, 22, 23, 25, 53, 80, 110, 135, 139, 143, 443, 445,
                                 587, 993, 995, 1433, 3306, 3389, 5432, 5900, 8080, 8443}},
    };
    return presets;
}

static const QHash<int, QString> &servicesMap()
{
    static const QHash<int, QString> s = {
        {20, QStringLiteral("FTP-Data")}, {21, QStringLiteral("FTP")}, {22, QStringLiteral("SSH")},
        {23, QStringLiteral("Telnet")}, {25, QStringLiteral("SMTP")}, {53, QStringLiteral("DNS")},
        {67, QStringLiteral("DHCP")}, {80, QStringLiteral("HTTP")}, {110, QStringLiteral("POP3")},
        {111, QStringLiteral("RPC")}, {135, QStringLiteral("MS-RPC")}, {139, QStringLiteral("NetBIOS")},
        {143, QStringLiteral("IMAP")}, {161, QStringLiteral("SNMP")}, {389, QStringLiteral("LDAP")},
        {443, QStringLiteral("HTTPS")}, {445, QStringLiteral("SMB")}, {465, QStringLiteral("SMTPS")},
        {514, QStringLiteral("Syslog")}, {515, QStringLiteral("Printer")}, {548, QStringLiteral("AFP")},
        {587, QStringLiteral("SMTP")}, {631, QStringLiteral("IPP")}, {636, QStringLiteral("LDAPS")},
        {873, QStringLiteral("rsync")}, {993, QStringLiteral("IMAPS")}, {995, QStringLiteral("POP3S")},
        {1080, QStringLiteral("SOCKS")}, {1433, QStringLiteral("MSSQL")}, {1521, QStringLiteral("Oracle")},
        {1723, QStringLiteral("PPTP")}, {1883, QStringLiteral("MQTT")}, {2049, QStringLiteral("NFS")},
        {2375, QStringLiteral("Docker")}, {3000, QStringLiteral("HTTP-Alt")}, {3306, QStringLiteral("MySQL")},
        {3389, QStringLiteral("RDP")}, {5000, QStringLiteral("UPnP")}, {5432, QStringLiteral("PostgreSQL")},
        {5900, QStringLiteral("VNC")}, {5985, QStringLiteral("WinRM")}, {6379, QStringLiteral("Redis")},
        {8000, QStringLiteral("HTTP-Alt")}, {8080, QStringLiteral("HTTP-Proxy")}, {8443, QStringLiteral("HTTPS-Alt")},
        {9000, QStringLiteral("HTTP-Alt")}, {9100, QStringLiteral("JetDirect")}, {27017, QStringLiteral("MongoDB")},
    };
    return s;
}

QString serviceName(int port)
{
    return servicesMap().value(port, QString::number(port));
}

QStringList HostResult::services() const
{
    QStringList out;
    for (int p : openPorts)
        out << serviceName(p);
    return out;
}

QVector<int> parsePorts(const QString &text, int limit)
{
    QVector<int> out;
    QSet<int> seen;
    const QStringList toks = text.trimmed().split(QRegularExpression(QStringLiteral("[,\\s]+")),
                                                  Qt::SkipEmptyParts);
    for (const QString &tok : toks) {
        if (out.size() >= limit)
            break;
        if (tok.contains(QLatin1Char('-'))) {
            const QString lo = tok.section(QLatin1Char('-'), 0, 0).trimmed();
            const QString hi = tok.section(QLatin1Char('-'), 1, 1).trimmed();
            bool okLo = false, okHi = false;
            const int a = lo.toInt(&okLo);
            const int b = hi.toInt(&okHi);
            if (okLo && okHi) {
                int from = qMax(1, qMin(a, b));
                int to = qMin(65535, qMax(a, b));
                for (int p = from; p <= to; ++p) {
                    if (out.size() >= limit)
                        break;
                    if (!seen.contains(p)) {
                        seen.insert(p);
                        out.append(p);
                    }
                }
            }
        } else {
            bool ok = false;
            const int p = tok.toInt(&ok);
            if (ok && p > 0 && p < 65536 && !seen.contains(p)) {
                seen.insert(p);
                out.append(p);
            }
        }
    }
    return out;
}

static const QHash<int, QString> &webPortsMap()
{
    static const QHash<int, QString> m = {
        {80, QStringLiteral("http")}, {8000, QStringLiteral("http")},
        {8080, QStringLiteral("http")}, {443, QStringLiteral("https")},
        {8443, QStringLiteral("https")},
    };
    return m;
}

QStringList webUrls(const QString &ip, const QVector<int> &ports)
{
    QStringList urls;
    for (int port : ports) {
        const QString scheme = webPortsMap().value(port);
        if (scheme.isEmpty())
            continue;
        urls << ((port == 80 || port == 443)
                     ? QStringLiteral("%1://%2").arg(scheme, ip)
                     : QStringLiteral("%1://%2:%3").arg(scheme, ip).arg(port));
    }
    return urls;
}

// --- Ziel-Parsing ----------------------------------------------------------

// IPv4 als 32-Bit-Zahl (nur fuer Bereichs-/CIDR-Aufloesung).
static bool ipv4ToUint(const QString &s, quint32 &out)
{
    const QStringList parts = s.split(QLatin1Char('.'));
    if (parts.size() != 4)
        return false;
    quint32 v = 0;
    for (const QString &p : parts) {
        bool ok = false;
        const int n = p.toInt(&ok);
        if (!ok || n < 0 || n > 255)
            return false;
        v = (v << 8) | static_cast<quint32>(n);
    }
    out = v;
    return true;
}

static QString uintToIpv4(quint32 v)
{
    return QStringLiteral("%1.%2.%3.%4")
        .arg((v >> 24) & 0xFF).arg((v >> 16) & 0xFF).arg((v >> 8) & 0xFF).arg(v & 0xFF);
}

QStringList parseTargets(const QString &text, int limit)
{
    QStringList out;
    QSet<QString> seen;
    const auto add = [&](const QString &value) {
        if (!value.isEmpty() && !seen.contains(value)) {
            seen.insert(value);
            out.append(value);
        }
    };

    const QStringList raws = text.trimmed().split(QRegularExpression(QStringLiteral("[,\\s]+")),
                                                  Qt::SkipEmptyParts);
    for (const QString &raw : raws) {
        if (out.size() >= limit)
            break;
        if (raw.contains(QLatin1Char('/'))) {  // CIDR
            const QString base = raw.section(QLatin1Char('/'), 0, 0);
            bool okBits = false;
            const int bits = raw.section(QLatin1Char('/'), 1, 1).toInt(&okBits);
            quint32 ip = 0;
            if (okBits && bits >= 0 && bits <= 32 && ipv4ToUint(base, ip)) {
                const quint32 mask = bits == 0 ? 0 : (0xFFFFFFFFu << (32 - bits));
                const quint32 network = ip & mask;
                const quint32 bcast = network | ~mask;
                for (quint32 h = network + 1; h < bcast && out.size() < limit; ++h)
                    add(uintToIpv4(h));
            } else {
                add(raw);
            }
        } else if (raw.contains(QLatin1Char('-')) && raw.at(0).isDigit()) {  // Bereich
            const QString lo = raw.section(QLatin1Char('-'), 0, 0).trimmed();
            const QString hiRaw = raw.section(QLatin1Char('-'), 1, 1).trimmed();
            const QString hi = (hiRaw.contains(QLatin1Char('.')))
                                   ? hiRaw
                                   : QStringLiteral("%1.%2").arg(lo.section(QLatin1Char('.'), 0, 2), hiRaw);
            quint32 a = 0, b = 0;
            if (ipv4ToUint(lo, a) && ipv4ToUint(hi, b)) {
                if (a > b) std::swap(a, b);
                for (quint32 n = a; n <= b && out.size() < limit; ++n)
                    add(uintToIpv4(n));
            } else {
                add(raw);
            }
        } else {
            add(raw);  // einzelne IP oder Hostname
        }
    }
    return out.mid(0, limit);
}

QString bareUncHost(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("^\\\\\\\\([^\\\\/]+)[\\\\/]?$"));
    const auto m = re.match(text.trimmed());
    return m.hasMatch() ? m.captured(1) : QString();
}

QString localIpv4()
{
#ifdef Q_OS_WIN
    SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == INVALID_SOCKET)
        return {};
    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(80);
    inet_pton(AF_INET, "8.8.8.8", &dest.sin_addr);
    QString result;
    if (connect(s, reinterpret_cast<sockaddr *>(&dest), sizeof(dest)) == 0) {
        sockaddr_in local{};
        int len = sizeof(local);
        if (getsockname(s, reinterpret_cast<sockaddr *>(&local), &len) == 0) {
            char buf[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &local.sin_addr, buf, sizeof(buf));
            result = QString::fromLatin1(buf);
        }
    }
    closesocket(s);
    return result;
#else
    int s = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0)
        return {};
    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(80);
    inet_pton(AF_INET, "8.8.8.8", &dest.sin_addr);
    QString result;
    if (::connect(s, reinterpret_cast<sockaddr *>(&dest), sizeof(dest)) == 0) {
        sockaddr_in local{};
        socklen_t len = sizeof(local);
        if (getsockname(s, reinterpret_cast<sockaddr *>(&local), &len) == 0) {
            char buf[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &local.sin_addr, buf, sizeof(buf));
            result = QString::fromLatin1(buf);
        }
    }
    ::close(s);
    return result;
#endif
}

QString defaultRange()
{
    const QString ip = localIpv4();
    if (ip.isEmpty() || ip.contains(QLatin1Char(':')))
        return {};
    return QStringLiteral("%1.0/24").arg(ip.section(QLatin1Char('.'), 0, 2));
}

// --- Parser ----------------------------------------------------------------

static const QRegularExpression &macRe()
{
    static const QRegularExpression re(
        QStringLiteral("([0-9A-Fa-f]{2}([:-])[0-9A-Fa-f]{2}(?:\\2[0-9A-Fa-f]{2}){4})"));
    return re;
}

QString parseArp(const QString &text, const QString &ip)
{
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (!ip.isEmpty() && !line.contains(ip))
            continue;
        const auto m = macRe().match(line);
        if (m.hasMatch())
            return m.captured(1).toLower().replace(QLatin1Char('-'), QLatin1Char(':'));
    }
    if (ip.isEmpty()) {
        const auto m = macRe().match(text);
        if (m.hasMatch())
            return m.captured(1).toLower().replace(QLatin1Char('-'), QLatin1Char(':'));
    }
    return {};
}

QStringList parseNetView(const QString &text)
{
    QStringList shares;
    bool started = false;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QString s = line.trimmed();
        if (!s.isEmpty() && QString(s).remove(QLatin1Char('-')).isEmpty()) {  // Trennlinie
            started = true;
            continue;
        }
        if (!started)
            continue;
        const QString low = s.toLower();
        if (s.isEmpty() || low.startsWith(QLatin1String("the command"))
            || low.startsWith(QLatin1String("der befehl")))
            break;
        const QString name = s.split(QRegularExpression(QStringLiteral("\\s+"))).value(0);
        if (!name.isEmpty())
            shares.append(name);
    }
    return shares;
}

QStringList parseSmbclient(const QString &text)
{
    QStringList shares;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QStringList parts = line.split(QLatin1Char('|'));
        if (parts.size() >= 2 && parts[0].trimmed().toLower() == QLatin1String("disk")) {
            const QString name = parts[1].trimmed();
            if (!name.isEmpty())
                shares.append(name);
        }
    }
    return shares;
}

std::pair<int, double> parsePing(const QString &text)
{
    int ttl = 0;
    double latency = 0.0;
    static const QRegularExpression ttlRe(QStringLiteral("ttl[=:]\\s*(\\d+)"),
                                          QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression timeRe(QStringLiteral("(?:time|zeit)[=<]\\s*([\\d.,]+)\\s*ms"),
                                           QRegularExpression::CaseInsensitiveOption);
    auto m = ttlRe.match(text);
    if (m.hasMatch())
        ttl = m.captured(1).toInt();
    m = timeRe.match(text);
    if (m.hasMatch())
        latency = m.captured(1).replace(QLatin1Char(','), QLatin1Char('.')).toDouble();
    return {ttl, latency};
}

QString osFromTtl(int ttl)
{
    if (ttl <= 0)
        return {};
    if (ttl <= 64)
        return QStringLiteral("Linux/Unix");
    if (ttl <= 128)
        return QStringLiteral("Windows");
    return QStringLiteral("Netzwerkgerät");
}

QString parseNbtstat(const QString &text)
{
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QString s = line.trimmed();
        if (s.contains(QLatin1String("<00>")) && s.toUpper().contains(QLatin1String("UNIQUE"))
            && !s.toUpper().startsWith(QLatin1String("MAC")))
            return s.split(QRegularExpression(QStringLiteral("\\s+"))).value(0).trimmed();
    }
    return {};
}

QString parseHtmlTitle(const QString &html)
{
    static const QRegularExpression re(
        QStringLiteral("<title[^>]*>(.*?)</title>"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    const auto m = re.match(html);
    if (!m.hasMatch())
        return {};
    QString title = m.captured(1);
    title.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    title = title.trimmed();
    return title.left(80);
}

// --- Blockierende OS-Hilfen ------------------------------------------------

static QString runCmd(const QStringList &cmd, int timeoutMs)
{
    if (cmd.isEmpty())
        return {};
    QProcess proc;
    proc.start(cmd.first(), cmd.mid(1));
    if (!proc.waitForStarted(2000))
        return {};
    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        return {};
    }
    return QString::fromLocal8Bit(proc.readAllStandardOutput());
}

static std::tuple<bool, int, double> pingInfo(const QString &ip, double timeout)
{
    QStringList cmd;
#ifdef Q_OS_WIN
    cmd << QStringLiteral("ping") << QStringLiteral("-n") << QStringLiteral("1")
        << QStringLiteral("-w") << QString::number(qMax(200, int(timeout * 1000))) << ip;
#else
    cmd << QStringLiteral("ping") << QStringLiteral("-c") << QStringLiteral("1")
        << QStringLiteral("-W") << QString::number(qMax(1, int(timeout + 0.5))) << ip;
#endif
    QProcess proc;
    proc.start(cmd.first(), cmd.mid(1));
    if (!proc.waitForStarted(2000))
        return {false, 0, 0.0};
    if (!proc.waitForFinished(int((timeout + 2) * 1000))) {
        proc.kill();
        return {false, 0, 0.0};
    }
    const auto [ttl, latency] = parsePing(QString::fromLocal8Bit(proc.readAllStandardOutput()));
    return {proc.exitCode() == 0, ttl, latency};
}

static bool tcpOpen(const QString &ip, int port, double timeout)
{
    QTcpSocket sock;
    sock.connectToHost(ip, static_cast<quint16>(port));
    return sock.waitForConnected(int(timeout * 1000));
}

static QString grabBanner(const QString &ip, int port, double timeout)
{
    QTcpSocket sock;
    sock.connectToHost(ip, static_cast<quint16>(port));
    if (!sock.waitForConnected(int(timeout * 1000)))
        return {};
    if (port == 80 || port == 3000 || port == 8000 || port == 8080 || port == 9000) {
        sock.write("HEAD / HTTP/1.0\r\nHost: " + ip.toLatin1() + "\r\n\r\n");
        sock.waitForBytesWritten(int(timeout * 1000));
    }
    if (!sock.waitForReadyRead(int(timeout * 1000)))
        return {};
    const QString text = QString::fromLatin1(sock.read(256));
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        if (line.toLower().startsWith(QLatin1String("server:")))
            return line.section(QLatin1Char(':'), 1).trimmed().left(60);
    }
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        if (!line.trimmed().isEmpty())
            return line.trimmed().left(60);
    }
    return {};
}

static QString reverseDns(const QString &ip)
{
    const QHostInfo info = QHostInfo::fromName(ip);
    // Fuer eine IP liefert fromName den Reverse-Namen nicht direkt; QHostInfo
    // hat keinen synchronen Reverse-Lookup, aber hostName() der IP-Aufloesung
    // gibt bei erfolgreichem PTR den Namen. Fallback: leer.
    if (!info.hostName().isEmpty() && info.hostName() != ip)
        return info.hostName();
    return {};
}

static QString webTitleOf(const QString &url, double timeout)
{
    // Best-effort GET ueber QTcpSocket (nur http; https-Titel entfaellt ohne TLS-Client).
    const QUrl u(url);
    if (u.scheme() != QLatin1String("http"))
        return {};
    QTcpSocket sock;
    sock.connectToHost(u.host(), static_cast<quint16>(u.port(80)));
    if (!sock.waitForConnected(int(qMax(1.0, timeout * 4) * 1000)))
        return {};
    const QByteArray req = "GET / HTTP/1.0\r\nHost: " + u.host().toLatin1()
                           + "\r\nUser-Agent: ncssh-netscan\r\n\r\n";
    sock.write(req);
    sock.waitForBytesWritten(2000);
    QByteArray data;
    while (sock.waitForReadyRead(int(qMax(1.0, timeout * 4) * 1000)) && data.size() < 8192)
        data += sock.read(8192 - data.size());
    return parseHtmlTitle(QString::fromUtf8(data));
}

static QString netbiosName(const QString &ip)
{
#ifdef Q_OS_WIN
    return parseNbtstat(runCmd({QStringLiteral("nbtstat"), QStringLiteral("-A"), ip}, 4000));
#else
    Q_UNUSED(ip);
    return {};
#endif
}

static QString arpLookup(const QString &ip)
{
#ifdef Q_OS_WIN
    return parseArp(runCmd({QStringLiteral("arp"), QStringLiteral("-a"), ip}, 4000), ip);
#else
    return parseArp(runCmd({QStringLiteral("arp"), QStringLiteral("-n"), ip}, 4000), ip);
#endif
}

static QStringList smbShares(const QString &ip)
{
#ifdef Q_OS_WIN
    return parseNetView(runCmd({QStringLiteral("net"), QStringLiteral("view"),
                                QStringLiteral("\\\\%1").arg(ip), QStringLiteral("/all")}, 8000));
#else
    return parseSmbclient(runCmd({QStringLiteral("smbclient"), QStringLiteral("-N"),
                                  QStringLiteral("-g"), QStringLiteral("-L"),
                                  QStringLiteral("//%1").arg(ip)}, 8000));
#endif
}

// --- Host-Probe ------------------------------------------------------------

static HostResult probeHost(const QString &ip, const ScanOptions &opts)
{
    HostResult res;
    res.ip = ip;
    for (int port : opts.ports) {
        if (tcpOpen(ip, port, opts.timeout))
            res.openPorts.append(port);
    }
    bool alive = false;
    if (opts.ping) {
        const auto [a, ttl, latency] = pingInfo(ip, opts.timeout);
        alive = a;
        res.ttl = ttl;
        res.latency = latency;
    }
    res.alive = !res.openPorts.isEmpty() || alive;
    if (!res.alive)
        return res;
    res.osGuess = osFromTtl(res.ttl);
    res.web = webUrls(ip, res.openPorts);
    if (opts.resolveNames)
        res.hostname = reverseDns(ip);
    if (opts.detectMac) {
        res.mac = arpLookup(ip);
        if (!res.mac.isEmpty())
            res.vendor = ouiVendor(res.mac);
    }
    if (opts.detectShares && (res.openPorts.contains(445) || res.openPorts.contains(139)))
        res.shares = smbShares(ip);
    if (opts.identify) {
        int count = 0;
        for (int p : res.openPorts) {
            if (count >= 3)
                break;
            if (p == 21 || p == 22 || p == 23 || p == 25 || p == 80 || p == 110
                || p == 143 || p == 8000 || p == 8080) {
                const QString banner = grabBanner(ip, p, opts.timeout);
                if (!banner.isEmpty())
                    res.banners.append(QStringLiteral("%1 %2").arg(p).arg(banner));
                ++count;
            }
        }
        if (!res.web.isEmpty())
            res.webTitle = webTitleOf(res.web.first(), opts.timeout);
        if (res.hostname.isEmpty())
            res.netbios = netbiosName(ip);
    }
    return res;
}

HostResult scanHost(const QString &host)
{
    ScanOptions opts(QStringList{host});
    opts.onlyAlive = false;
    opts.ports = portPresets().value(QStringLiteral("common"));
    return probeHost(host, opts);
}

namespace {
// Einfacher Parallel-Runner: N Worker konsumieren die Ziel-Liste.
class ProbeShared {
public:
    ProbeShared(const ScanOptions &o,
                const std::function<void(int, int)> &prog,
                const std::function<void(const HostResult &)> &host,
                const CancelTokenPtr &c)
        : opts(o), onProgress(prog), onHost(host), cancel(c),
          total(o.targets.size()) {}

    const ScanOptions &opts;
    const std::function<void(int, int)> &onProgress;
    const std::function<void(const HostResult &)> &onHost;
    CancelTokenPtr cancel;
    int total;

    QMutex mutex;
    int nextIndex = 0;
    int done = 0;

    int takeNext()
    {
        QMutexLocker lock(&mutex);
        if (nextIndex >= total)
            return -1;
        return nextIndex++;
    }

    void report(const HostResult &res)
    {
        QMutexLocker lock(&mutex);
        ++done;
        if (onProgress)
            onProgress(done, total);
        if (!(opts.onlyAlive && !res.alive) && onHost)
            onHost(res);
    }
};
} // namespace

void scanEvents(const ScanOptions &opts,
                const std::function<void(int, int)> &onProgress,
                const std::function<void(const HostResult &)> &onHost,
                const CancelTokenPtr &cancel)
{
    ProbeShared shared(opts, onProgress, onHost, cancel);
    const int workers = qBound(1, opts.concurrency, 256);

    QVector<QThread *> threads;
    const auto work = [&shared]() {
        while (true) {
            if (shared.cancel && shared.cancel->isCancelled())
                return;
            const int idx = shared.takeNext();
            if (idx < 0)
                return;
            const HostResult res = probeHost(shared.opts.targets.at(idx), shared.opts);
            shared.report(res);
        }
    };

    // Worker als einfache QThreads (probeHost ist blockierend/IO-lastig).
    QVector<QThread *> pool;
    for (int i = 0; i < workers; ++i) {
        QThread *t = QThread::create(work);
        t->start();
        pool.append(t);
    }
    for (QThread *t : pool) {
        t->wait();
        delete t;
    }
}

bool wakeOnLan(const QString &mac, const QString &broadcast, int port)
{
    QString hex;
    for (QChar c : mac) {
        if (c.isLetterOrNumber())
            hex.append(c);
    }
    if (hex.length() != 12)
        return false;
    QByteArray payload(6, char(0xFF));
    const QByteArray macBytes = QByteArray::fromHex(hex.toLatin1());
    for (int i = 0; i < 16; ++i)
        payload.append(macBytes);

    QUdpSocket sock;
    sock.bind();
    sock.setSocketOption(QAbstractSocket::MulticastLoopbackOption, 1);
    const qint64 sent = sock.writeDatagram(payload, QHostAddress(broadcast),
                                           static_cast<quint16>(port));
    return sent == payload.size();
}

} // namespace ncssh::core
