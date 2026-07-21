#include "ncssh/core/filesystem.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>
#include <algorithm>
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

void LocalFileSystem::remove(const QString &path, bool recursive)
{
    QFileInfo fi(path);
    if (fi.isDir() && !fi.isSymLink()) {
        if (recursive) {
            QDir d(path);
            if (!d.removeRecursively())
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
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        throwErr(QStringLiteral("Kann Datei nicht schreiben: %1").arg(path));
    if (f.write(data) != data.size())
        throwErr(QStringLiteral("Schreiben unvollstaendig: %1").arg(path));
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

} // namespace ncssh::core
