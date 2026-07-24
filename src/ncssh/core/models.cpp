#include "ncssh/core/models.hpp"

namespace ncssh::core {

QString FileEntry::permString() const
{
    if (!permissions)
        return {};
    const quint32 m = permissions;
    QString s;
    static const char *rwx[] = {"---", "--x", "-w-", "-wx", "r--", "r-x", "rw-", "rwx"};
    s += QLatin1String(rwx[(m >> 6) & 7]);
    s += QLatin1String(rwx[(m >> 3) & 7]);
    s += QLatin1String(rwx[m & 7]);
    // Sonderbits wie Python stat.filemode: setuid/setgid/sticky
    if (m & 04000) s[2] = (s[2] == QLatin1Char('x')) ? QLatin1Char('s') : QLatin1Char('S');
    if (m & 02000) s[5] = (s[5] == QLatin1Char('x')) ? QLatin1Char('s') : QLatin1Char('S');
    if (m & 01000) s[8] = (s[8] == QLatin1Char('x')) ? QLatin1Char('t') : QLatin1Char('T');
    return s;
}

QString FileEntry::permOctal() const
{
    if (!permissions)
        return {};
    return QString::number(permissions & 0777, 8).rightJustified(3, QLatin1Char('0'));
}

QJsonObject TunnelSpec::toJson() const
{
    return QJsonObject{
        {QStringLiteral("kind"), kind},
        {QStringLiteral("listen_host"), listenHost},
        {QStringLiteral("listen_port"), listenPort},
        {QStringLiteral("dest_host"), destHost},
        {QStringLiteral("dest_port"), destPort},
    };
}

TunnelSpec TunnelSpec::fromJson(const QJsonObject &data)
{
    TunnelSpec t;
    t.kind = data.value(QStringLiteral("kind")).toString(QStringLiteral("local"));
    t.listenHost = data.value(QStringLiteral("listen_host")).toString(QStringLiteral("127.0.0.1"));
    t.listenPort = data.value(QStringLiteral("listen_port")).toInt(0);
    t.destHost = data.value(QStringLiteral("dest_host")).toString();
    t.destPort = data.value(QStringLiteral("dest_port")).toInt(0);
    return t;
}

QString TunnelSpec::label() const
{
    const QString listen = QStringLiteral("%1:%2").arg(listenHost).arg(listenPort);
    if (kind == QLatin1String("dynamic"))
        return QStringLiteral("SOCKS @ %1").arg(listen);
    const QString arrow = (kind == QLatin1String("remote")) ? QStringLiteral("←")
                                                            : QStringLiteral("→");
    return QStringLiteral("%1 %2 %3:%4").arg(listen, arrow, destHost).arg(destPort);
}

QJsonObject ServerProfile::toJson() const
{
    QJsonArray tunnelArr;
    for (const auto &t : tunnels)
        tunnelArr.append(t.toJson());
    return QJsonObject{
        {QStringLiteral("name"), name},
        {QStringLiteral("host"), host},
        {QStringLiteral("port"), port},
        {QStringLiteral("username"), username},
        {QStringLiteral("auth_method"), authMethod},
        {QStringLiteral("key_path"), keyPath},
        {QStringLiteral("save_password"), savePassword},
        {QStringLiteral("known_hosts_policy"), knownHostsPolicy},
        {QStringLiteral("start_path"), startPath},
        {QStringLiteral("tunnels"), tunnelArr},
        {QStringLiteral("color"), color},
        {QStringLiteral("proxy_jump"), proxyJump},
        {QStringLiteral("last_connected"), lastConnected},
        {QStringLiteral("keepalive_seconds"), keepaliveSeconds},
        {QStringLiteral("connect_timeout"), connectTimeout},
        {QStringLiteral("compression"), compression},
        {QStringLiteral("ciphers"), ciphers},
        {QStringLiteral("kex_algorithms"), kexAlgorithms},
        {QStringLiteral("agent_forwarding"), agentForwarding},
    };
}

ServerProfile ServerProfile::fromJson(const QJsonObject &data)
{
    ServerProfile p;
    p.name = data.value(QStringLiteral("name")).toString();
    p.host = data.value(QStringLiteral("host")).toString();
    p.port = data.value(QStringLiteral("port")).toInt(22);
    p.username = data.value(QStringLiteral("username")).toString();
    p.authMethod = data.value(QStringLiteral("auth_method")).toString(QStringLiteral("key"));
    p.keyPath = data.value(QStringLiteral("key_path")).toString();
    p.passphrase = data.value(QStringLiteral("passphrase")).toString();
    p.savePassword = data.value(QStringLiteral("save_password")).toBool(false);
    p.password = data.value(QStringLiteral("password")).toString();
    p.knownHostsPolicy =
        data.value(QStringLiteral("known_hosts_policy")).toString(QStringLiteral("accept-new"));
    p.startPath = data.value(QStringLiteral("start_path")).toString(QStringLiteral("."));
    for (const auto &v : data.value(QStringLiteral("tunnels")).toArray())
        p.tunnels.push_back(TunnelSpec::fromJson(v.toObject()));
    p.color = data.value(QStringLiteral("color")).toString();
    p.proxyJump = data.value(QStringLiteral("proxy_jump")).toString();
    p.lastConnected = data.value(QStringLiteral("last_connected")).toString();
    p.keepaliveSeconds = data.value(QStringLiteral("keepalive_seconds")).toInt(30);
    p.connectTimeout = data.value(QStringLiteral("connect_timeout")).toInt(20);
    p.compression = data.value(QStringLiteral("compression")).toBool(false);
    p.ciphers = data.value(QStringLiteral("ciphers")).toString();
    p.kexAlgorithms = data.value(QStringLiteral("kex_algorithms")).toString();
    p.agentForwarding = data.value(QStringLiteral("agent_forwarding")).toBool(false);
    return p;
}

QString ServerProfile::display() const
{
    if (!username.isEmpty())
        return QStringLiteral("%1@%2:%3").arg(username, host).arg(port);
    return QStringLiteral("%1:%2").arg(host).arg(port);
}

} // namespace ncssh::core
