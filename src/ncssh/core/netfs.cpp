#include "ncssh/core/netfs.hpp"

#include <QVariant>
#include <algorithm>

namespace ncssh::core {

static const QString kScheme = QStringLiteral("net://");

NetworkScanProvider::NetworkScanProvider(const std::vector<HostResult> &hosts)
{
    label = QStringLiteral("Netzwerk");
    isRemote = false;
    setHosts(hosts);
}

void NetworkScanProvider::setHosts(const std::vector<HostResult> &hosts)
{
    m_hosts = hosts;
    m_byId.clear();
    for (const auto &h : m_hosts)
        m_byId.insert(displayOf(h), h);
}

QString NetworkScanProvider::displayOf(const HostResult &host)
{
    return host.hostname.isEmpty() ? host.ip : host.hostname;
}

QStringList NetworkScanProvider::segments(const QString &path)
{
    QString rest = path.startsWith(kScheme) ? path.mid(kScheme.length()) : path;
    return rest.split(QLatin1Char('/'), Qt::SkipEmptyParts);
}

QString NetworkScanProvider::toUnc(const QString &path) const
{
    const QStringList segs = segments(path);
    const HostResult host = segs.isEmpty() ? HostResult{} : m_byId.value(segs.first());
    const QString ip = !host.ip.isEmpty() ? host.ip : (segs.isEmpty() ? QString() : segs.first());
    QString unc = QStringLiteral("\\\\") + ip;
    if (segs.size() > 1)
        unc += QStringLiteral("\\") + segs.mid(1).join(QLatin1Char('\\'));
    return unc;
}

bool NetworkScanProvider::isHostList(const QString &path) const
{
    return segments(path).isEmpty();
}

std::vector<FileEntry> NetworkScanProvider::listDir(const QString &path)
{
    const QStringList segs = segments(path);
    if (segs.isEmpty())
        return hostEntries();
    if (segs.size() == 1) {  // Freigaben eines Hosts
        const HostResult host = m_byId.value(segs.first());
        std::vector<FileEntry> out;
        for (const QString &s : host.shares) {
            FileEntry e;
            e.name = s;
            e.type = EntryType::Dir;
            out.push_back(e);
        }
        return out;
    }
    return m_local.listDir(toUnc(path));  // Dateien (UNC)
}

std::vector<FileEntry> NetworkScanProvider::hostEntries() const
{
    std::vector<FileEntry> out;
    for (const auto &h : m_hosts) {
        FileEntry e;
        e.name = displayOf(h);
        e.type = EntryType::Dir;
        QVariantList ports, web, banners;
        for (int p : h.openPorts) ports << p;
        for (const QString &w : h.web) web << w;
        for (const QString &b : h.banners) banners << b;
        e.extra = QVariantMap{
            {QStringLiteral("ip"), h.ip},
            {QStringLiteral("host"), h.hostname},
            {QStringLiteral("mac"), h.mac},
            {QStringLiteral("shares"), h.hasShares()},
            {QStringLiteral("ports"), ports},
            {QStringLiteral("web"), web},
            {QStringLiteral("latency"), h.latency},
            {QStringLiteral("ttl"), h.ttl},
            {QStringLiteral("os"), h.osGuess},
            {QStringLiteral("vendor"), h.vendor},
            {QStringLiteral("netbios"), h.netbios},
            {QStringLiteral("web_title"), h.webTitle},
            {QStringLiteral("banners"), banners},
            {QStringLiteral("services"), h.services()},
        };
        out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(), [](const FileEntry &a, const FileEntry &b) {
        return a.name.toLower() < b.name.toLower();
    });
    return out;
}

bool NetworkScanProvider::isDir(const QString &path)
{
    const QStringList segs = segments(path);
    if (segs.size() <= 1)  // Root oder Host -> immer navigierbar
        return true;
    if (segs.size() == 2)  // Freigaben-Wurzel
        return true;
    return m_local.isDir(toUnc(path));
}

QString NetworkScanProvider::join(const QString &path, const QString &name) const
{
    if (path.endsWith(QLatin1String("://")))
        return path + name;
    QString p = path;
    while (p.endsWith(QLatin1Char('/'))) p.chop(1);
    return p + QLatin1Char('/') + name;
}

QString NetworkScanProvider::parent(const QString &path) const
{
    const QStringList segs = segments(path);
    if (segs.size() <= 1)
        return kScheme;
    return kScheme + QStringList(segs.mid(0, segs.size() - 1)).join(QLatin1Char('/'));
}

QString NetworkScanProvider::basename(const QString &path) const
{
    const QStringList segs = segments(path);
    return segs.isEmpty() ? label : segs.last();
}

QString NetworkScanProvider::home()
{
    return kScheme;
}

QString NetworkScanProvider::readText(const QString &path, qint64 maxBytes)
{
    return m_local.readText(toUnc(path), maxBytes);
}

QByteArray NetworkScanProvider::readBytes(const QString &path, qint64 maxBytes)
{
    return m_local.readBytes(toUnc(path), maxBytes);
}

void NetworkScanProvider::writeText(const QString &path, const QString &content)
{
    m_local.writeText(toUnc(path), content);
}

void NetworkScanProvider::writeBytes(const QString &path, const QByteArray &data)
{
    m_local.writeBytes(toUnc(path), data);
}

void NetworkScanProvider::mkdir(const QString &path)
{
    m_local.mkdir(toUnc(path));
}

void NetworkScanProvider::remove(const QString &path, bool recursive)
{
    m_local.remove(toUnc(path), recursive);
}

void NetworkScanProvider::rename(const QString &oldPath, const QString &newPath)
{
    m_local.rename(toUnc(oldPath), toUnc(newPath));
}

void NetworkScanProvider::chmod(const QString &path, quint32 mode)
{
    m_local.chmod(toUnc(path), mode);
}

} // namespace ncssh::core
