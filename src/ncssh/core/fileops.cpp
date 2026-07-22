#include "ncssh/core/fileops.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <algorithm>
#include <stdexcept>

namespace ncssh::core {

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
    // Anzahl Eintraege: entpackte Dateien zaehlen (best-effort).
    int count = 0;
    QDirIterator it(destDir, QDir::AllEntries | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        ++count;
    }
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
                     QFileInfo(archive).absoluteFilePath()};
    args += names;
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
        if (QFileInfo(full).isDir()) {
            QDirIterator it(full, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) { it.next(); ++count; }
        } else if (QFileInfo::exists(full)) {
            ++count;
        }
    }
    return count;
}

DirStats dirStats(const QString &path, int limitEntries)
{
    DirStats out;
    QDirIterator it(path, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    int seen = 0;
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        if (fi.isDir()) {
            ++out.dirs;
            continue;
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
        const QString rel = QDir(path).relativeFilePath(fi.filePath());
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
            break;
        }
    }
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
    QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
        if (++seen >= limitEntries)
            return {total, true};
    }
    return {total, false};
}

} // namespace ncssh::core
