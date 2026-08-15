// Parser fuer "ls -lnA --time-style=long-iso".
#include "ncssh/core/lsparse.hpp"

#include <QDateTime>
#include <QStringList>

namespace ncssh::core {

// st_mode-Typbits (POSIX-Oktalwerte; unter MSVC nicht vollstaendig vorhanden).
namespace {
constexpr quint32 kIfReg = 0100000;
constexpr quint32 kIfDir = 0040000;
constexpr quint32 kIfLnk = 0120000;
constexpr quint32 kIfChr = 0020000;
constexpr quint32 kIfBlk = 0060000;
constexpr quint32 kIfFifo = 0010000;
constexpr quint32 kIfSock = 0140000;
constexpr quint32 kSuid = 04000;
constexpr quint32 kSgid = 02000;
constexpr quint32 kSvtx = 01000;

quint32 typeBits(QChar c)
{
    switch (c.unicode()) {
    case 'd': return kIfDir;
    case 'l': return kIfLnk;
    case 'c': return kIfChr;
    case 'b': return kIfBlk;
    case 'p': return kIfFifo;
    case 's': return kIfSock;
    default: return kIfReg;
    }
}

} // namespace  (modeFromPerms ist oeffentlich — der Parser-Test prueft ihn)

// rwx-/Typzeichen einer ls-Spalte (z.B. "drwxr-x---") -> st_mode-Bits.
quint32 modeFromPerms(const QString &sIn)
{
    const QString s = sIn.trimmed();
    if (s.isEmpty())
        return 0;
    quint32 mode = typeBits(s.at(0));
    QString p = s.mid(1, 9);
    while (p.size() < 9)
        p += QLatin1Char('-');
    if (p.at(0) == QLatin1Char('r')) mode |= 0400;
    if (p.at(1) == QLatin1Char('w')) mode |= 0200;
    if (p.at(3) == QLatin1Char('r')) mode |= 0040;
    if (p.at(4) == QLatin1Char('w')) mode |= 0020;
    if (p.at(6) == QLatin1Char('r')) mode |= 0004;
    if (p.at(7) == QLatin1Char('w')) mode |= 0002;
    // Ausfuehrbar + Sonderbits (setuid/setgid/sticky) aus dem jeweils 3. Zeichen.
    const QChar xo = p.at(2), xg = p.at(5), xt = p.at(8);
    if (xo == QLatin1Char('x') || xo == QLatin1Char('s')) mode |= 0100;
    if (xo == QLatin1Char('s') || xo == QLatin1Char('S')) mode |= kSuid;
    if (xg == QLatin1Char('x') || xg == QLatin1Char('s')) mode |= 0010;
    if (xg == QLatin1Char('s') || xg == QLatin1Char('S')) mode |= kSgid;
    if (xt == QLatin1Char('x') || xt == QLatin1Char('t')) mode |= 0001;
    if (xt == QLatin1Char('t') || xt == QLatin1Char('T')) mode |= kSvtx;
    return mode;
}

namespace {

QDateTime parseMtime(const QString &date, const QString &time)
{
    const QString joined = date + QLatin1Char(' ') + time;
    for (const char *fmt : {"yyyy-MM-dd HH:mm", "yyyy-MM-dd HH:mm:ss"}) {
        const QDateTime dt = QDateTime::fromString(joined, QString::fromLatin1(fmt));
        if (dt.isValid())
            return dt;
    }
    return {};
}

// Wie Pythons str.split(None, maxSplit): Whitespace-Laeufe trennen; nach
// maxSplit Trennungen behaelt der letzte Teil den Rest der Zeile
// (der Name — Teil 8 — darf Leerzeichen enthalten).
QStringList splitWs(const QString &line, int maxSplit)
{
    QStringList parts;
    const int n = line.size();
    int i = 0;
    while (i < n) {
        while (i < n && line.at(i).isSpace())
            ++i;
        if (i >= n)
            break;
        if (parts.size() == maxSplit) {
            parts << line.mid(i);
            break;
        }
        int j = i;
        while (j < n && !line.at(j).isSpace())
            ++j;
        parts << line.mid(i, j - i);
        i = j;
    }
    return parts;
}
} // namespace

std::vector<FileEntry> parseLsLong(const QString &text)
{
    std::vector<FileEntry> out;
    const QStringList rawLines = text.split(QLatin1Char('\n'));
    for (const QString &raw : rawLines) {
        QString line = raw;
        while (line.endsWith(QLatin1Char('\r')) || line.endsWith(QLatin1Char('\n')))
            line.chop(1);
        if (line.trimmed().isEmpty() || line.startsWith(QLatin1String("total ")))
            continue;
        QStringList parts = splitWs(line, 7);  // Name (Teil 8) darf Leerzeichen enthalten
        if (parts.size() < 8)
            continue;
        QString perms = parts.at(0);
        const QString uid = parts.at(2);
        const QString gid = parts.at(3);
        QString size = parts.at(4);
        QString date = parts.at(5);
        QString time = parts.at(6);
        QString name = parts.at(7);
        if (perms.isEmpty() || !QStringLiteral("dl-bcps").contains(perms.at(0)))
            continue;                          // keine Listing-Zeile
        if ((perms.at(0) == QLatin1Char('b') || perms.at(0) == QLatin1Char('c'))
            && size.endsWith(QLatin1Char(','))) {
            // Geraetedatei: statt der Groesse stehen ZWEI Felder ("major, minor") —
            // neu aufteilen, sonst verrutschen Datum/Zeit/Name um eine Spalte.
            parts = splitWs(line, 8);
            if (parts.size() < 9)
                continue;
            perms = parts.at(0);
            date = parts.at(6);
            time = parts.at(7);
            name = parts.at(8);
            size = QStringLiteral("0");
        }
        const EntryType etype = (perms.at(0) == QLatin1Char('l')) ? EntryType::Symlink
                              : (perms.at(0) == QLatin1Char('d')) ? EntryType::Dir
                                                                  : EntryType::File;
        QString linkTarget;
        if (etype == EntryType::Symlink) {
            const int arrow = name.indexOf(QLatin1String(" -> "));
            if (arrow >= 0) {
                linkTarget = name.mid(arrow + 4);
                name = name.left(arrow);
            }
        }
        bool ok = false;
        qint64 sz = size.toLongLong(&ok);
        if (!ok)
            sz = 0;
        FileEntry e;
        e.name = name;
        e.type = etype;
        e.size = sz;
        e.modified = parseMtime(date, time);
        e.permissions = modeFromPerms(perms);
        e.owner = uid;
        e.group = gid;
        e.linkTarget = linkTarget;
        out.push_back(std::move(e));
    }
    return out;
}

} // namespace ncssh::core
