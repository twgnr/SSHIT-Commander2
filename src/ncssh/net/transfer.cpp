#include "ncssh/net/transfer.hpp"

#include "ncssh/net/ssh.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <libssh2.h>
#include <libssh2_sftp.h>
#include <mutex>
#include <stdexcept>

namespace ncssh::net {

using core::EntryType;
using core::FileEntry;
using core::LocalFileSystem;

static constexpr qint64 kLocalChunk = 1024 * 1024;   // 1 MiB
static constexpr qint64 kSftpChunk = 128 * 1024;     // 128 KiB
static constexpr qint64 kGenericMax = 256 * 1024 * 1024;

QString directionOf(FileSystemProvider *src, FileSystemProvider *dst)
{
    const bool s = src->isRemote, d = dst->isRemote;
    if (!s && d) return QStringLiteral("upload");
    if (s && !d) return QStringLiteral("download");
    if (s && d) return QStringLiteral("remote");
    return QStringLiteral("local");
}

qint64 pathSize(FileSystemProvider *provider, const QString &path)
{
    // Jeder Provider liefert seine Groesse selbst (lokal via stat, SFTP via
    // Attribute, sudo via stat-Befehl); die Basis meldet 0 = unbekannt.
    try {
        return provider->size(path);
    } catch (...) {
        return 0;
    }
}

bool pathIsDir(FileSystemProvider *provider, const QString &path)
{
    if (auto *local = dynamic_cast<LocalFileSystem *>(provider))
        return QFileInfo(path).isDir();
    return provider->isDir(path);
}

// --- lokale Kopie mit kumuliertem Fortschritt ------------------------------

static void copyFileLocal(const QString &src, const QString &dst,
                          qint64 &copied, qint64 total, const ProgressFn &progress)
{
    QFile in(src), out(dst);
    if (!in.open(QIODevice::ReadOnly))
        throw std::runtime_error(("Kann Quelle nicht lesen: " + src).toStdString());
    if (!out.open(QIODevice::WriteOnly))
        throw std::runtime_error(("Kann Ziel nicht schreiben: " + dst).toStdString());
    QByteArray buf;
    while (!(buf = in.read(kLocalChunk)).isEmpty()) {
        out.write(buf);
        copied += buf.size();
        progress(copied, total);
    }
    out.setPermissions(in.permissions());
}

static void localCopyTree(const QString &src, const QString &dst, const ProgressFn &progress)
{
    if (QFileInfo(src).isDir()) {
        QStringList files;
        qint64 total = 0;
        QDirIterator it(src, QDir::Files | QDir::Hidden | QDir::System,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            total += it.fileInfo().size();
            files << it.filePath();
        }
        QDir().mkpath(dst);
        qint64 copied = 0;
        for (const QString &fp : files) {
            const QString rel = QDir(src).relativeFilePath(fp);
            const QString target = dst + QLatin1Char('/') + rel;
            QDir().mkpath(QFileInfo(target).path());
            copyFileLocal(fp, target, copied, total, progress);
        }
        if (files.isEmpty())
            progress(0, 0);
    } else {
        const qint64 total = QFileInfo(src).size();
        QDir().mkpath(QFileInfo(dst).path());
        qint64 copied = 0;
        copyFileLocal(src, dst, copied, total, progress);
    }
}

// --- SFTP-gestuetztes Streaming einer einzelnen Datei ----------------------

static void streamUpload(const QString &localPath, SFTPFileSystem *sftp, const QString &remotePath,
                         qint64 baseCopied, qint64 total, const ProgressFn &progress)
{
    QFile in(localPath);
    if (!in.open(QIODevice::ReadOnly))
        throw std::runtime_error(("Kann Quelle nicht lesen: " + localPath).toStdString());
    std::lock_guard<std::recursive_mutex> lock(sftp->session()->mutex());
    LIBSSH2_SFTP *s = sftp->session()->sftp();
    LIBSSH2_SFTP_HANDLE *h = libssh2_sftp_open(
        s, remotePath.toUtf8().constData(),
        LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC, 0644);
    if (!h)
        throw std::runtime_error(("Kann Remote-Datei nicht schreiben: " + remotePath).toStdString());
    qint64 copied = baseCopied;
    QByteArray buf;
    while (!(buf = in.read(kSftpChunk)).isEmpty()) {
        qint64 sent = 0;
        while (sent < buf.size()) {
            const ssize_t n = libssh2_sftp_write(h, buf.constData() + sent, buf.size() - sent);
            if (n < 0) {
                libssh2_sftp_close(h);
                throw std::runtime_error(("Upload fehlgeschlagen: " + remotePath).toStdString());
            }
            sent += n;
        }
        copied += buf.size();
        progress(copied, total);
    }
    libssh2_sftp_close(h);
}

static void streamDownload(SFTPFileSystem *sftp, const QString &remotePath, const QString &localPath,
                           qint64 baseCopied, qint64 total, const ProgressFn &progress)
{
    QFile out(localPath);
    if (!out.open(QIODevice::WriteOnly))
        throw std::runtime_error(("Kann Ziel nicht schreiben: " + localPath).toStdString());
    std::lock_guard<std::recursive_mutex> lock(sftp->session()->mutex());
    LIBSSH2_SFTP *s = sftp->session()->sftp();
    LIBSSH2_SFTP_HANDLE *h = libssh2_sftp_open(s, remotePath.toUtf8().constData(),
                                               LIBSSH2_FXF_READ, 0);
    if (!h)
        throw std::runtime_error(("Kann Remote-Datei nicht lesen: " + remotePath).toStdString());
    qint64 copied = baseCopied;
    char buf[131072];
    for (;;) {
        const ssize_t n = libssh2_sftp_read(h, buf, sizeof(buf));
        if (n <= 0)
            break;
        out.write(buf, n);
        copied += n;
        progress(copied, total);
    }
    libssh2_sftp_close(h);
}

// Kopiert EINE Datei mit dem schnellsten passenden Pfad.
static void copyOneFile(FileSystemProvider *src, const QString &sp,
                        FileSystemProvider *dst, const QString &dp,
                        qint64 &copied, qint64 total, const ProgressFn &progress)
{
    auto *lsrc = dynamic_cast<LocalFileSystem *>(src);
    auto *ldst = dynamic_cast<LocalFileSystem *>(dst);
    auto *rsrc = dynamic_cast<SFTPFileSystem *>(src);
    auto *rdst = dynamic_cast<SFTPFileSystem *>(dst);

    if (lsrc && ldst) {
        copyFileLocal(sp, dp, copied, total, progress);
    } else if (lsrc && rdst) {
        // Fortschritt meldet streamUpload selbst; die Gesamtsumme fuehrt der
        // Aufrufer (transferWithProgress) anhand der bekannten Dateigroesse.
        streamUpload(sp, rdst, dp, copied, total, progress);
    } else if (rsrc && ldst) {
        streamDownload(rsrc, sp, dp, copied, total, progress);
    } else {
        // sudo-Provider / gemischt: ganze Datei ueber die Provider-Methoden.
        const qint64 sz = pathSize(src, sp);
        const qint64 limit = sz ? sz : kGenericMax;
        QByteArray data = src->readBytes(sp, limit + 1);
        if (data.size() > limit) {
            data = src->readBytes(sp, kGenericMax + 1);
            if (data.size() > kGenericMax)
                throw std::runtime_error(("Datei zu groß für generisches Kopieren: " + sp).toStdString());
        }
        dst->writeBytes(dp, data);
        copied += data.size();
        progress(copied, total);
    }
}

// Sammelt rekursiv (quelle, ziel, groesse) und legt Zielordner an.
static void collectTree(FileSystemProvider *src, const QString &sp,
                        FileSystemProvider *dst, const QString &dp,
                        std::vector<std::tuple<QString, QString, qint64>> &out)
{
    if (pathIsDir(src, sp)) {
        if (!pathIsDir(dst, dp))
            dst->mkdir(dp);
        for (const FileEntry &e : src->listDir(sp)) {
            if (e.type == EntryType::Parent)
                continue;
            collectTree(src, src->join(sp, e.name), dst, dst->join(dp, e.name), out);
        }
    } else {
        out.emplace_back(sp, dp, pathSize(src, sp));
    }
}

void transferWithProgress(FileSystemProvider *src, const QString &srcPath,
                          FileSystemProvider *dst, const QString &dstPath,
                          const ProgressFn &progress, bool /*resume*/)
{
    auto *lsrc = dynamic_cast<LocalFileSystem *>(src);
    auto *ldst = dynamic_cast<LocalFileSystem *>(dst);
    if (lsrc && ldst) {
        localCopyTree(srcPath, dstPath, progress);
        return;
    }

    if (!pathIsDir(src, srcPath)) {
        // Einzeldatei
        const qint64 total = pathSize(src, srcPath);
        qint64 copied = 0;
        copyOneFile(src, srcPath, dst, dstPath, copied, total, progress);
        return;
    }

    // Verzeichnis: Baum einsammeln, dann Datei fuer Datei mit Gesamtfortschritt.
    std::vector<std::tuple<QString, QString, qint64>> tree;
    // scp-Semantik: existiert das Zielverzeichnis, wird HINEIN gemischt.
    QString effectiveDst = dstPath;
    if (pathIsDir(dst, dstPath))
        effectiveDst = dst->join(dstPath, src->basename(srcPath));
    collectTree(src, srcPath, dst, effectiveDst, tree);
    if (tree.empty()) {
        progress(0, 0);
        return;
    }
    qint64 total = 0;
    for (const auto &[sp, dp, sz] : tree)
        total += sz;
    qint64 copied = 0;
    for (const auto &[sp, dp, sz] : tree) {
        qint64 before = copied;
        copyOneFile(src, sp, dst, dp, before, total, progress);
        copied += sz;
        progress(copied, total);
    }
}

bool verifyTree(FileSystemProvider *src, const QString &srcPath,
                FileSystemProvider *dst, const QString &dstPath)
{
    for (const FileEntry &e : src->listDir(srcPath)) {
        if (e.type == EntryType::Parent || e.type == EntryType::Symlink)
            continue;
        const QString sp = src->join(srcPath, e.name);
        const QString dp = dst->join(dstPath, e.name);
        if (e.isDir()) {
            if (!pathIsDir(dst, dp) || !verifyTree(src, sp, dst, dp))
                return false;
        } else {
            const qint64 ssize = e.size ? e.size : pathSize(src, sp);
            if (pathSize(dst, dp) != ssize)
                return false;
        }
    }
    return true;
}

QString transfer(FileSystemProvider *src, const QString &srcPath,
                 FileSystemProvider *dst, const QString &dstPath)
{
    transferWithProgress(src, srcPath, dst, dstPath, [](qint64, qint64) {});
    return QStringLiteral("Übertragen: %1 → %2").arg(srcPath, dstPath);
}

} // namespace ncssh::net
