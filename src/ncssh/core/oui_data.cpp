#include "ncssh/core/oui_data.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QTextStream>

#include <mutex>

namespace ncssh::core {

namespace {

// Kuratiert (Grossbuchstaben, ohne Trenner): OUI -> Hersteller.
QHash<QString, QString> makeCuratedOui()
{
    return {
        {QStringLiteral("005056"), QStringLiteral("VMware")},
        {QStringLiteral("000C29"), QStringLiteral("VMware")},
        {QStringLiteral("000569"), QStringLiteral("VMware")},
        {QStringLiteral("001C14"), QStringLiteral("VMware")},
        {QStringLiteral("080027"), QStringLiteral("VirtualBox (Oracle)")},
        {QStringLiteral("525400"), QStringLiteral("QEMU/KVM")},
        {QStringLiteral("00155D"), QStringLiteral("Microsoft Hyper-V")},
        {QStringLiteral("0050F2"), QStringLiteral("Microsoft")},
        {QStringLiteral("000D3A"), QStringLiteral("Microsoft")},
        {QStringLiteral("00163E"), QStringLiteral("Xen")},
        {QStringLiteral("001C42"), QStringLiteral("Parallels")},
        {QStringLiteral("B827EB"), QStringLiteral("Raspberry Pi")},
        {QStringLiteral("DCA632"), QStringLiteral("Raspberry Pi")},
        {QStringLiteral("E45F01"), QStringLiteral("Raspberry Pi")},
        {QStringLiteral("28CDC1"), QStringLiteral("Raspberry Pi")},
        {QStringLiteral("D83ADD"), QStringLiteral("Raspberry Pi")},
        {QStringLiteral("000393"), QStringLiteral("Apple")},
        {QStringLiteral("001451"), QStringLiteral("Apple")},
        {QStringLiteral("F0189A"), QStringLiteral("Apple")},
        {QStringLiteral("001788"), QStringLiteral("Philips/Signify (Hue)")},
        {QStringLiteral("001132"), QStringLiteral("Synology")},
    };
}

QString norm(const QString &mac)
{
    QString out;
    out.reserve(mac.size());
    for (const QChar c : mac) {
        if (c.isLetterOrNumber())
            out.append(c.toUpper());
    }
    return out;
}

// Laedt optional assets/oui.csv (einmalig). Sucht neben der Anwendung sowie in
// den Qt-Ressourcen; fehlt die Datei, bleibt es bei der kuratierten Liste.
void loadExtra(QHash<QString, QString> &table)
{
    QStringList candidates;
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        candidates << appDir + QStringLiteral("/assets/oui.csv");
        candidates << appDir + QStringLiteral("/../assets/oui.csv");
    }
    candidates << QStringLiteral(":/assets/oui.csv");

    for (const QString &path : candidates) {
        QFile fh(path);
        if (!fh.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        QTextStream in(&fh);
        in.setEncoding(QStringConverter::Utf8);
        while (!in.atEnd()) {
            const QString line = in.readLine();
            const int comma = line.indexOf(QLatin1Char(','));
            if (comma < 0)
                continue;
            const QString key = norm(line.left(comma)).left(6);
            const QString vendor = line.mid(comma + 1).trimmed();
            if (key.size() == 6 && !vendor.isEmpty() && !table.contains(key))
                table.insert(key, vendor);
        }
        break;  // erste gefundene Datei gewinnt
    }
}

const QHash<QString, QString> &ouiTable()
{
    static const QHash<QString, QString> table = [] {
        QHash<QString, QString> t = makeCuratedOui();
        loadExtra(t);
        return t;
    }();
    return table;
}

} // namespace

QString ouiVendor(const QString &mac)
{
    const QString n = norm(mac);
    if (n.size() < 6)
        return {};
    const QString vendor = ouiTable().value(n.left(6));
    if (!vendor.isEmpty())
        return vendor;
    // Lokal verwaltete Adresse (Bit 1 des ersten Oktetts gesetzt) -> randomisiert.
    bool ok = false;
    const int firstOctet = n.left(2).toInt(&ok, 16);
    if (ok && (firstOctet & 0b10))
        return QStringLiteral("(lokal/zufällig)");
    return {};
}

} // namespace ncssh::core
