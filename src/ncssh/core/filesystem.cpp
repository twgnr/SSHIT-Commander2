#include "ncssh/core/filesystem.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStorageInfo>
#include <algorithm>
#include <filesystem>
#include <stdexcept>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace ncssh::core {

static void throwErr(const QString &msg)
{
    throw std::runtime_error(msg.toStdString());
}

LocalFileSystem::LocalFileSystem()
{
    label = QStringLiteral("local");
    isRemote = false;
}

std::vector<FileEntry> LocalFileSystem::listDir(const QString &path)
{
    std::vector<FileEntry> entries;
    const QString norm = QDir::cleanPath(path);
    if (!QFileInfo(norm).isRoot() && !parent(norm).isEmpty() && parent(norm) != norm) {
        FileEntry up;
        up.name = QStringLiteral("..");
        up.type = EntryType::Parent;
        entries.push_back(up);
    }

    QDir dir(path);
    if (!dir.exists())
        throwErr(QStringLiteral("Verzeichnis nicht gefunden: %1").arg(path));

    const auto infos = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot
                                             | QDir::Hidden | QDir::System);
    for (const QFileInfo &fi : infos) {
        FileEntry e;
        e.name = fi.fileName();
        e.type = fi.isSymLink() ? EntryType::Symlink
                 : fi.isDir()   ? EntryType::Dir
                                : EntryType::File;
        e.size = fi.size();
        e.modified = fi.lastModified();
        e.created = fi.birthTime();
        e.accessed = fi.lastRead();
        e.owner = fi.owner();
        e.group = fi.group();
        if (fi.isSymLink())
            e.linkTarget = fi.symLinkTarget();
        // Versteckt: Dotfile (POSIX) oder Windows-Versteckt-/System-Attribut.
        e.hidden = fi.fileName().startsWith(QLatin1Char('.')) || fi.isHidden();
        // st_mode nachbilden: Qt-Permissions -> POSIX-Bits
        const auto p = fi.permissions();
        quint32 mode = 0;
        if (p & QFileDevice::ReadOwner)  mode |= 0400;
        if (p & QFileDevice::WriteOwner) mode |= 0200;
        if (p & QFileDevice::ExeOwner)   mode |= 0100;
        if (p & QFileDevice::ReadGroup)  mode |= 0040;
        if (p & QFileDevice::WriteGroup) mode |= 0020;
        if (p & QFileDevice::ExeGroup)   mode |= 0010;
        if (p & QFileDevice::ReadOther)  mode |= 0004;
        if (p & QFileDevice::WriteOther) mode |= 0002;
        if (p & QFileDevice::ExeOther)   mode |= 0001;
        e.permissions = mode;
        entries.push_back(std::move(e));
    }

    std::sort(entries.begin(), entries.end(), [](const FileEntry &a, const FileEntry &b) {
        if (a.type == EntryType::Parent) return true;
        if (b.type == EntryType::Parent) return false;
        if (a.isDir() != b.isDir()) return a.isDir();
        return a.name.toLower() < b.name.toLower();
    });
    return entries;
}

bool LocalFileSystem::isDir(const QString &path)
{
    return QFileInfo(path).isDir();
}

void LocalFileSystem::mkdir(const QString &path)
{
    if (QFileInfo::exists(path))
        throwErr(QStringLiteral("Existiert bereits: %1").arg(path));
    if (!QDir().mkpath(path))
        throwErr(QStringLiteral("Verzeichnis konnte nicht angelegt werden: %1").arg(path));
}

// Verweis statt echtem Ordner? NTFS-Junctions (mklink /J, npm/pnpm-Links)
// zaehlen in Qt 6 NICHT als isSymLink() — sie muessen extra erkannt werden.
static bool isLinkEntry(const QFileInfo &fi)
{
    return fi.isSymLink() || fi.isJunction();
}

// Entfernt nur den Verweis selbst, nie das Ziel. Ordner-Verweise (Junction,
// Ordner-Symlink) entfernt rmdir, Datei-Symlinks QFile::remove.
static bool removeLinkEntry(const QString &path)
{
    return QDir().rmdir(path) || QFile::remove(path);
}

static bool removeFileEntry(const QString &path)
{
    if (QFile::remove(path))
        return true;
    // Schreibgeschuetzte Dateien (Windows-Attribut) wie removeRecursively().
    QFile::setPermissions(path, QFile::permissions(path) | QFileDevice::WriteUser);
    return QFile::remove(path);
}

// Wie QDir::removeRecursively, aber ohne Verweisen zu folgen: dieses folgt
// Junctions und leerte deren ZIEL (z. B. das verlinkte Quellverzeichnis).
static bool removeTree(const QString &path)
{
    bool ok = true;
    QDirIterator it(path, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        const QString p = fi.absoluteFilePath();
        if (isLinkEntry(fi))
            ok = removeLinkEntry(p) && ok;
        else if (fi.isDir())
            ok = removeTree(p) && ok;
        else
            ok = removeFileEntry(p) && ok;
    }
    return QDir().rmdir(path) && ok;
}

void LocalFileSystem::remove(const QString &path, bool recursive)
{
    QFileInfo fi(path);
    if (isLinkEntry(fi)) {
        if (!removeLinkEntry(path))
            throwErr(QStringLiteral("Loeschen fehlgeschlagen: %1").arg(path));
        return;
    }
    if (fi.isDir()) {
        if (recursive) {
            if (!removeTree(path))
                throwErr(QStringLiteral("Loeschen fehlgeschlagen: %1").arg(path));
        } else {
            if (!QDir().rmdir(path))
                throwErr(QStringLiteral("Verzeichnis nicht leer oder gesperrt: %1").arg(path));
        }
    } else {
        if (!QFile::remove(path))
            throwErr(QStringLiteral("Loeschen fehlgeschlagen: %1").arg(path));
    }
}

QString LocalFileSystem::readText(const QString &path, qint64 maxBytes)
{
    return QString::fromUtf8(readBytes(path, maxBytes));
}

void LocalFileSystem::writeText(const QString &path, const QString &content)
{
    writeBytes(path, content.toUtf8());
}

void LocalFileSystem::writeBytes(const QString &path, const QByteArray &data)
{
    // Ueber eine Temp-Datei schreiben, die erst am Ende die alte ersetzt: bei
    // voller Platte oder weggefallenem Netzlaufwerk bleibt die alte Datei
    // unversehrt. Frueher wurde sie zuerst geleert, ein Fehler beim Schliessen
    // blieb unbemerkt und der Editor meldete "Gespeichert".
    // Verweise direkt beschreiben (ihr Ziel), sonst ersetzte die Temp-Datei
    // den Verweis durch eine normale Datei.
    if (!isLinkEntry(QFileInfo(path))) {
        QSaveFile f(path);
        f.setDirectWriteFallback(true);   // Ordner ohne Recht fuer Temp-Dateien
        if (!f.open(QIODevice::WriteOnly))
            throwErr(QStringLiteral("Kann Datei nicht schreiben: %1 (%2)").arg(path, f.errorString()));
        if (f.write(data) != data.size()) {
            f.cancelWriting();
            throwErr(QStringLiteral("Schreiben unvollstaendig: %1 (%2)").arg(path, f.errorString()));
        }
        if (!f.commit())
            throwErr(QStringLiteral("Speichern fehlgeschlagen: %1 (%2)").arg(path, f.errorString()));
        return;
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        throwErr(QStringLiteral("Kann Datei nicht schreiben: %1").arg(path));
    if (f.write(data) != data.size() || !f.flush())
        throwErr(QStringLiteral("Schreiben unvollstaendig: %1 (%2)").arg(path, f.errorString()));
    f.close();
    if (f.error() != QFileDevice::NoError)
        throwErr(QStringLiteral("Speichern fehlgeschlagen: %1 (%2)").arg(path, f.errorString()));
}

QByteArray LocalFileSystem::readBytes(const QString &path, qint64 maxBytes)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        throwErr(QStringLiteral("Kann Datei nicht lesen: %1").arg(path));
    return f.read(maxBytes);
}

void LocalFileSystem::rename(const QString &oldPath, const QString &newPath)
{
    if (!QFile::rename(oldPath, newPath))
        throwErr(QStringLiteral("Umbenennen fehlgeschlagen: %1 → %2").arg(oldPath, newPath));
}

void LocalFileSystem::chmod(const QString &path, quint32 mode)
{
    QFileDevice::Permissions p;
    if (mode & 0400) p |= QFileDevice::ReadOwner;
    if (mode & 0200) p |= QFileDevice::WriteOwner;
    if (mode & 0100) p |= QFileDevice::ExeOwner;
    if (mode & 0040) p |= QFileDevice::ReadGroup;
    if (mode & 0020) p |= QFileDevice::WriteGroup;
    if (mode & 0010) p |= QFileDevice::ExeGroup;
    if (mode & 0004) p |= QFileDevice::ReadOther;
    if (mode & 0002) p |= QFileDevice::WriteOther;
    if (mode & 0001) p |= QFileDevice::ExeOther;
    if (!QFile::setPermissions(path, p))
        throwErr(QStringLiteral("chmod fehlgeschlagen: %1").arg(path));
}

QString LocalFileSystem::join(const QString &path, const QString &name) const
{
    // Blosser Laufwerksbuchstabe ("C:") ist laufwerks-RELATIV — Separator
    // erzwingen, damit Dateien im Laufwerks-Root korrekt adressiert werden.
    QString p = path;
    if (p.length() == 2 && p[1] == QLatin1Char(':') && p[0].isLetter())
        p += QLatin1Char('/');
    return QDir::toNativeSeparators(QDir::cleanPath(p + QLatin1Char('/') + name));
}

QString LocalFileSystem::parent(const QString &path) const
{
    const QString clean = QDir::cleanPath(path);
    const int idx = clean.lastIndexOf(QLatin1Char('/'));
    if (idx < 0)
        return {};
    if (idx == 0)
        return QStringLiteral("/");
    QString p = clean.left(idx);
    if (p.length() == 2 && p[1] == QLatin1Char(':'))
        p += QLatin1Char('/');
    return QDir::toNativeSeparators(p);
}

QString LocalFileSystem::basename(const QString &path) const
{
    const QString clean = QDir::cleanPath(path);
    const int idx = clean.lastIndexOf(QLatin1Char('/'));
    return idx < 0 ? clean : clean.mid(idx + 1);
}

QString LocalFileSystem::home()
{
    return QDir::toNativeSeparators(QDir::homePath());
}

QString LocalFileSystem::normalize(const QString &path) const
{
    if (path.isEmpty())
        return path;
    QString p = path;
    // Blosser Laufwerksbuchstabe ("C:") ist laufwerks-RELATIV — als Root lesen.
    if (p.length() == 2 && p[1] == QLatin1Char(':') && p[0].isLetter())
        p += QLatin1Char('/');
    // Absolut machen (loest "." und Relativpfade gegen das Arbeitsverzeichnis
    // auf) und auf native Separatoren vereinheitlichen.
    return QDir::toNativeSeparators(QDir::cleanPath(QFileInfo(p).absoluteFilePath()));
}

qint64 LocalFileSystem::size(const QString &path)
{
    const QFileInfo info(path);
    return info.exists() ? info.size() : 0;
}

void LocalFileSystem::symlink(const QString &target, const QString &linkPath)
{
    // Echter symbolischer Link (kein .lnk-Verknuepfungsobjekt). Unter Windows
    // benoetigt das den Entwicklermodus bzw. erhoehte Rechte; scheitert das,
    // wird der Fehler nach oben gereicht.
    std::error_code ec;
    // UTF-8-korrekt (Windows: Konstruktion aus char8_t behandelt den Pfad als UTF-8).
    const QByteArray tgtU8 = target.toUtf8();
    const QByteArray lnkU8 = linkPath.toUtf8();
    const std::filesystem::path tgt(reinterpret_cast<const char8_t *>(tgtU8.constData()));
    const std::filesystem::path lnk(reinterpret_cast<const char8_t *>(lnkU8.constData()));
    if (std::filesystem::is_directory(tgt, ec))
        std::filesystem::create_directory_symlink(tgt, lnk, ec);
    else
        std::filesystem::create_symlink(tgt, lnk, ec);
    if (ec)
        throw std::runtime_error(
            ("Symlink konnte nicht angelegt werden: " + QString::fromStdString(ec.message()))
                .toStdString());
}

} // namespace ncssh::core
