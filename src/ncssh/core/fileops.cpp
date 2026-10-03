#include "ncssh/core/fileops.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <algorithm>
#include <functional>
#include <stdexcept>

namespace ncssh::core {

// Rekursiver Durchlauf, der NICHT in Symlinks oder NTFS-Junctions absteigt.
// QDirIterator::Subdirectories folgt Junctions (Qt sieht sie nicht als Symlink):
// Zyklen (Junction auf einen Vorfahren) liefen endlos, Ziele ausserhalb des
// Baums wurden mitgezaehlt bzw. doppelt gezaehlt. visit() erhaelt jeden
// Eintrag (auch Link-Ordner, nur ohne Abstieg); false bricht ab.
static void walkNoLinks(const QString &root, const std::function<bool(const QFileInfo &)> &visit)
{
    QStringList pending{root};
    while (!pending.isEmpty()) {
        const QString dir = pending.takeLast();
        QDirIterator it(dir, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden
                                 | QDir::System);
        while (it.hasNext()) {
            it.next();
            const QFileInfo fi = it.fileInfo();
            if (!visit(fi))
                return;
            if (fi.isDir() && !fi.isSymLink() && !fi.isJunction())
                pending.append(fi.filePath());
        }
    }
}

// Vom Entpacken unterstuetzte Endungen (laengere zuerst).
static const QStringList kArchiveExts = {
    QStringLiteral(".tar.gz"), QStringLiteral(".tar.bz2"), QStringLiteral(".tar.xz"),
    QStringLiteral(".tgz"), QStringLiteral(".tbz2"), QStringLiteral(".txz"),
    QStringLiteral(".zip"), QStringLiteral(".tar"),
};

bool isArchive(const QString &name)
{
    const QString low = name.toLower();
    for (const QString &ext : kArchiveExts) {
        if (low.endsWith(ext))
            return true;
    }
    return false;
}

QString archiveStem(const QString &name)
{
    const QString low = name.toLower();
    for (const QString &ext : kArchiveExts) {
        if (low.endsWith(ext))
            return name.left(name.length() - ext.length());
    }
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    return dot > 0 ? name.left(dot) : name;
}

int extractArchive(const QString &archive, const QString &destDir)
{
    QDir().mkpath(destDir);
    if (!isArchive(archive))
        throw std::runtime_error("Nicht unterstütztes Archivformat.");
    // bsdtar (Windows 10+/Linux) entpackt zip und tar.* sicher; --no-same-owner
    // und die relative Extraktion nach destDir verhindern Pfad-Ausbruch.
    QProcess proc;
    proc.setWorkingDirectory(destDir);
    QStringList args{QStringLiteral("-x"), QStringLiteral("-f"), QFileInfo(archive).absoluteFilePath()};
    proc.start(QStringLiteral("tar"), args);
    if (!proc.waitForStarted(5000))
        throw std::runtime_error("Konnte 'tar' nicht starten (fuer Archiv-Entpacken).");
    if (!proc.waitForFinished(300000)) {
        proc.kill();
        throw std::runtime_error("Entpacken hat zu lange gedauert.");
    }
    if (proc.exitCode() != 0)
        throw std::runtime_error(("Entpacken fehlgeschlagen: "
                                  + QString::fromLocal8Bit(proc.readAllStandardError()))
                                     .toStdString());
    // Anzahl Eintraege: entpackte Dateien zaehlen (best-effort; ohne Links zu
    // folgen — ein Archiv kann Symlinks auf beliebige Ziele enthalten).
    int count = 0;
    walkNoLinks(destDir, [&count](const QFileInfo &) {
        ++count;
        return true;
    });
    return count;
}

static QCryptographicHash::Algorithm hashAlgo(const QString &algo)
{
    const QString a = algo.toLower();
    if (a == QLatin1String("md5")) return QCryptographicHash::Md5;
    if (a == QLatin1String("sha1")) return QCryptographicHash::Sha1;
    if (a == QLatin1String("sha512")) return QCryptographicHash::Sha512;
    return QCryptographicHash::Sha256;
}

QString hashFile(const QString &path, const QString &algo)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        throw std::runtime_error(("Kann Datei nicht lesen: " + path).toStdString());
    QCryptographicHash h(hashAlgo(algo));
    if (!h.addData(&f))
        throw std::runtime_error(("Lesefehler: " + path).toStdString());
    return QString::fromLatin1(h.result().toHex());
}

QString hashBytes(const QByteArray &data, const QString &algo)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, hashAlgo(algo)).toHex());
}

int makeZip(const QString &archive, const QString &baseDir, const QStringList &names)
{
    // bsdtar erzeugt aus der Endung das Format: "-a -cf out.zip" -> ZIP.
    QProcess proc;
    proc.setWorkingDirectory(baseDir);
    QStringList args{QStringLiteral("-a"), QStringLiteral("-c"), QStringLiteral("-f"),
                     QFileInfo(archive).absoluteFilePath(), QStringLiteral("--")};
    // Dateinamen duerfen nie als Option gelesen werden ("-C", "--exclude=*"):
    // "--" beendet die Optionen. bsdtar wertet "@name" aber auch danach noch
    // als "Inhalt eines anderen Archivs uebernehmen" aus — solche Namen (und
    // zur Sicherheit auch "-...") daher als "./name" uebergeben.
    for (const QString &name : names) {
        if (name.startsWith(QLatin1Char('@')) || name.startsWith(QLatin1Char('-')))
            args << QStringLiteral("./") + name;
        else
            args << name;
    }
    proc.start(QStringLiteral("tar"), args);
    if (!proc.waitForStarted(5000))
        throw std::runtime_error("Konnte 'tar' nicht starten (fuer ZIP-Erstellung).");
    if (!proc.waitForFinished(300000)) {
        proc.kill();
        throw std::runtime_error("Packen hat zu lange gedauert.");
    }
    if (proc.exitCode() != 0)
        throw std::runtime_error(("Packen fehlgeschlagen: "
                                  + QString::fromLocal8Bit(proc.readAllStandardError()))
                                     .toStdString());
    int count = 0;
    for (const QString &name : names) {
        const QString full = baseDir + QLatin1Char('/') + name;
        const QFileInfo top(full);
        if (top.isDir() && !top.isSymLink() && !top.isJunction()) {
            walkNoLinks(full, [&count](const QFileInfo &fi) {
                if (fi.isFile())
                    ++count;
                return true;
            });
        } else if (top.exists() || top.isSymLink()) {
            ++count;
        }
    }
    return count;
}

DirStats dirStats(const QString &path, int limitEntries)
{
    DirStats out;
    int seen = 0;
    const QDir base(path);
    // Ohne Symlinks/Junctions zu folgen (siehe walkNoLinks): sonst Zyklen und
    // doppelt gezaehlte Groessen.
    walkNoLinks(path, [&](const QFileInfo &fi) -> bool {
        if (fi.isDir()) {
            ++out.dirs;
            return true;
        }
        ++out.files;
        const qint64 sz = fi.size();
        out.size += sz;
        if (fi.fileName().startsWith(QLatin1Char('.')))
            ++out.hidden;
        QString ext = fi.suffix().isEmpty() ? QStringLiteral("(ohne)")
                                            : QLatin1Char('.') + fi.suffix().toLower();
        // Endungszaehlung
        bool found = false;
        for (auto &kv : out.topExt) {
            if (kv.first == ext) { ++kv.second; found = true; break; }
        }
        if (!found)
            out.topExt.emplace_back(ext, 1);
        const QDateTime mt = fi.lastModified();
        const QDateTime ct = fi.birthTime().isValid() ? fi.birthTime() : fi.metadataChangeTime();
        const QString rel = base.relativeFilePath(fi.filePath());
        if (!out.newestModified || mt > out.newestModified->second)
            out.newestModified = {rel, mt};
        if (!out.oldestModified || mt < out.oldestModified->second)
            out.oldestModified = {rel, mt};
        if (!out.newestCreated || ct > out.newestCreated->second)
            out.newestCreated = {rel, ct};
        if (!out.largest || sz > out.largest->second)
            out.largest = {rel, sz};
        if (++seen >= limitEntries) {
            out.truncated = true;
            return false;
        }
        return true;
    });
    std::sort(out.topExt.begin(), out.topExt.end(),
              [](const auto &a, const auto &b) {
                  if (a.second != b.second) return a.second > b.second;
                  return a.first < b.first;
              });
    if (out.topExt.size() > 5)
        out.topExt.resize(5);
    return out;
}

std::pair<qint64, bool> dirSize(const QString &path, int limitEntries)
{
    qint64 total = 0;
    int seen = 0;
    bool truncated = false;
    // Ohne Symlinks/Junctions zu folgen (siehe walkNoLinks).
    walkNoLinks(path, [&](const QFileInfo &fi) -> bool {
        if (fi.isDir())
            return true;
        total += fi.size();
        if (++seen >= limitEntries) {
            truncated = true;
            return false;
        }
        return true;
    });
    return {total, truncated};
}

} // namespace ncssh::core
