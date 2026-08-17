#include "ncssh/net/transfer.hpp"

#include "ncssh/net/ssh.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <libssh2.h>
#include <libssh2_sftp.h>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace ncssh::net {

using core::EntryType;
using core::FileEntry;
using core::LocalFileSystem;

static constexpr qint64 kLocalChunk = 1024 * 1024;   // 1 MiB
// Gross genug, dass libssh2 mehrere SFTP-Requests im Flug haelt (Read-Ahead /
// Write-Pipelining) — 128 KiB und weniger degenerieren auf hoher Latenz zu
// einem synchronen Roundtrip je Block.
static constexpr qint64 kSftpChunk = 256 * 1024;
static constexpr qint64 kGenericMax = 256 * 1024 * 1024;
// Ab dieser Groesse lohnt eine eigene Datenverbindung (der zusaetzliche
// SSH-Handshake kostet selbst um eine Sekunde).
static constexpr qint64 kDedicatedSessionMin = 8 * 1024 * 1024;

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
                          qint64 &copied, qint64 total, const ProgressFn &progress,
                          qint64 offset = 0)
{
    QFile in(src), out(dst);
    if (!in.open(QIODevice::ReadOnly))
        throw std::runtime_error(("Kann Quelle nicht lesen: " + src).toStdString());
    // Resume: an das vorhandene Ziel anhaengen (ReadWrite, nicht abschneiden);
    // ohne Offset unveraendert neu schreiben.
    if (!out.open(offset > 0 ? QIODevice::ReadWrite : QIODevice::WriteOnly))
        throw std::runtime_error(("Kann Ziel nicht schreiben: " + dst).toStdString());
    if (offset > 0) {
        in.seek(offset);
        out.seek(offset);
        copied += offset;   // bereits vorhandene Bytes zaehlen
        progress(copied, total);
    }
    QByteArray buf;
    while (!(buf = in.read(kLocalChunk)).isEmpty()) {
        out.write(buf);
        copied += buf.size();
        progress(copied, total);
    }
    out.setPermissions(in.permissions());
}

// Resume-Offset einer lokalen Zieldatei: vorhandene Groesse, falls kleiner als
// die Quelle (sonst 0 = neu bzw. schon vollstaendig).
static qint64 localResumeOffset(const QString &srcFile, const QString &dstFile, bool resume)
{
    if (!resume)
        return 0;
    const qint64 already = QFileInfo(dstFile).size();
    const qint64 full = QFileInfo(srcFile).size();
    return (already > 0 && full > 0 && already < full) ? already : 0;
}

static void localCopyTree(const QString &src, const QString &dst, const ProgressFn &progress,
                          bool resume = false)
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
            copyFileLocal(fp, target, copied, total, progress,
                          localResumeOffset(fp, target, resume));
        }
        if (files.isEmpty())
            progress(0, 0);
    } else {
        const qint64 total = QFileInfo(src).size();
        QDir().mkpath(QFileInfo(dst).path());
        qint64 copied = 0;
        copyFileLocal(src, dst, copied, total, progress,
                      localResumeOffset(src, dst, resume));
    }
}

// --- SFTP-gestuetztes Streaming einer einzelnen Datei ----------------------

static void streamUpload(const QString &localPath, SFTPFileSystem *sftp, const QString &remotePath,
                         qint64 baseCopied, qint64 total, const ProgressFn &progress,
                         qint64 offset = 0)
{
    QFile in(localPath);
    if (!in.open(QIODevice::ReadOnly))
        throw std::runtime_error(("Kann Quelle nicht lesen: " + localPath).toStdString());
    // Session-Mutex nur je Block halten: ein Upload darf Listings und Konsole
    // nicht bis zum Dateiende blockieren.
    auto &mutex = sftp->session()->mutex();
    LIBSSH2_SFTP_HANDLE *h = nullptr;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        LIBSSH2_SFTP *s = sftp->session()->sftp();
        // Beim Fortsetzen nicht abschneiden (kein TRUNC), sonst wie bisher.
        const long flags = offset > 0 ? (LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT)
                                      : (LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC);
        h = libssh2_sftp_open(s, remotePath.toUtf8().constData(), flags, 0644);
        if (!h)
            throw std::runtime_error(("Kann Remote-Datei nicht schreiben: " + remotePath).toStdString());
        if (offset > 0)
            libssh2_sftp_seek64(h, static_cast<libssh2_uint64_t>(offset));
    }
    qint64 copied = baseCopied;
    if (offset > 0) {
        in.seek(offset);
        copied += offset;
        progress(copied, total);
    }
    QByteArray buf;
    while (!(buf = in.read(kSftpChunk)).isEmpty()) {
        qint64 sent = 0;
        while (sent < buf.size()) {
            ssize_t n;
            {
                std::lock_guard<std::recursive_mutex> lock(mutex);
                if (sftp->session()->closing)
                    throw std::runtime_error("Sitzung geschlossen.");
                n = libssh2_sftp_write(h, buf.constData() + sent, buf.size() - sent);
            }
            if (n < 0) {
                std::lock_guard<std::recursive_mutex> lock(mutex);
                if (!sftp->session()->closing)
                    libssh2_sftp_close(h);
                throw std::runtime_error(("Upload fehlgeschlagen: " + remotePath).toStdString());
            }
            sent += n;
        }
        copied += buf.size();
        progress(copied, total);
    }
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (!sftp->session()->closing)
        libssh2_sftp_close(h);
}

static void streamDownload(SFTPFileSystem *sftp, const QString &remotePath, const QString &localPath,
                           qint64 baseCopied, qint64 total, const ProgressFn &progress,
                           qint64 offset = 0)
{
    QFile out(localPath);
    // Beim Fortsetzen an das vorhandene lokale Ziel anhaengen.
    if (!out.open(offset > 0 ? QIODevice::ReadWrite : QIODevice::WriteOnly))
        throw std::runtime_error(("Kann Ziel nicht schreiben: " + localPath).toStdString());
    // Session-Mutex nur je Block halten (siehe streamUpload).
    auto &mutex = sftp->session()->mutex();
    LIBSSH2_SFTP_HANDLE *h = nullptr;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        LIBSSH2_SFTP *s = sftp->session()->sftp();
        h = libssh2_sftp_open(s, remotePath.toUtf8().constData(), LIBSSH2_FXF_READ, 0);
        if (!h)
            throw std::runtime_error(("Kann Remote-Datei nicht lesen: " + remotePath).toStdString());
        if (offset > 0)
            libssh2_sftp_seek64(h, static_cast<libssh2_uint64_t>(offset));
    }
    qint64 copied = baseCopied;
    if (offset > 0) {
        out.seek(offset);
        copied += offset;
        progress(copied, total);
    }
    std::vector<char> buf(kSftpChunk);
    for (;;) {
        ssize_t n;
        {
            std::lock_guard<std::recursive_mutex> lock(mutex);
            if (sftp->session()->closing)
                throw std::runtime_error("Sitzung geschlossen.");
            n = libssh2_sftp_read(h, buf.data(), buf.size());
        }
        if (n <= 0)
            break;
        out.write(buf.data(), n);
        copied += n;
        progress(copied, total);
    }
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (!sftp->session()->closing)
        libssh2_sftp_close(h);
}

// Kopiert EINE Datei mit dem schnellsten passenden Pfad.
static void copyOneFile(FileSystemProvider *src, const QString &sp,
                        FileSystemProvider *dst, const QString &dp,
                        qint64 &copied, qint64 total, const ProgressFn &progress,
                        bool resume = false)
{
    auto *lsrc = dynamic_cast<LocalFileSystem *>(src);
    auto *ldst = dynamic_cast<LocalFileSystem *>(dst);
    auto *rsrc = dynamic_cast<SFTPFileSystem *>(src);
    auto *rdst = dynamic_cast<SFTPFileSystem *>(dst);

    // Resume: am vorhandenen Ziel-Offset fortsetzen. Ist das Ziel schon so gross
    // wie (oder groesser als) die Quelle, gilt die Datei als fertig.
    qint64 offset = 0;
    if (resume) {
        const qint64 already = pathSize(dst, dp);
        const qint64 full = pathSize(src, sp);
        if (already > 0 && full > 0 && already < full)
            offset = already;
        else if (already >= full && full > 0) {
            copied += full;
            progress(copied, total);
            return;   // bereits vollstaendig uebertragen
        }
    }

    if (lsrc && ldst) {
        copyFileLocal(sp, dp, copied, total, progress, offset);
    } else if (lsrc && rdst) {
        // Fortschritt meldet streamUpload selbst; die Gesamtsumme fuehrt der
        // Aufrufer (transferWithProgress) anhand der bekannten Dateigroesse.
        streamUpload(sp, rdst, dp, copied, total, progress, offset);
    } else if (rsrc && ldst) {
        streamDownload(rsrc, sp, dp, copied, total, progress, offset);
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
            // Sicherheit (defense in depth): Namen mit Pfad-Trennern oder ".."
            // eines boesartigen Servers ueberspringen, damit dst->join den
            // Zielbaum nicht verlassen kann.
            if (e.name == QLatin1String("..") || e.name.contains(QLatin1Char('/'))
                || e.name.contains(QLatin1Char('\\')))
                continue;
            const QString csp = src->join(sp, e.name);
            const QString cdp = dst->join(dp, e.name);
            // NUR echte Verzeichnisse rekursiv verfolgen. Symlinks werden wie im
            // als Blatt behandelt (nicht hineinlaufen) — sonst wuerde
            // ein Server-Symlink auf z. B. "/" den ganzen Zielbaum aufziehen.
            if (e.type == EntryType::Dir)
                collectTree(src, csp, dst, cdp, out);
            else
                // Groesse aus dem Listing uebernehmen — ein stat je Datei
                // waere bei SFTP ein eigener Netz-Roundtrip pro Eintrag.
                out.emplace_back(csp, cdp, e.size ? e.size : pathSize(src, csp));
        }
    } else {
        out.emplace_back(sp, dp, pathSize(src, sp));
    }
}

void transferWithProgress(FileSystemProvider *src, const QString &srcPath,
                          FileSystemProvider *dst, const QString &dstPath,
                          const ProgressFn &progress, bool resume)
{
    auto *lsrc = dynamic_cast<LocalFileSystem *>(src);
    auto *ldst = dynamic_cast<LocalFileSystem *>(dst);
    if (lsrc && ldst) {
        localCopyTree(srcPath, dstPath, progress, resume);
        return;
    }

    const bool srcIsDir = pathIsDir(src, srcPath);
    const qint64 singleSize = srcIsDir ? 0 : pathSize(src, srcPath);

    // Eigene Datenverbindung fuer die Remote-Seite: der Transfer teilt sich
    // sonst den Session-Mutex mit Listings und Konsole — waehrend eines langen
    // Uploads fuehlte sich jede Navigation zaeh an. Nur wenn es sich lohnt
    // (Verzeichnisbaum oder grosse Datei), denn der zusaetzliche Handshake
    // kostet selbst um eine Sekunde. Schlaegt der Aufbau fehl (Server-Limit,
    // 2FA, anderer Host-Key), laeuft alles wie bisher ueber die geteilte
    // Session.
    auto *rsrc = dynamic_cast<SFTPFileSystem *>(src);
    auto *rdst = dynamic_cast<SFTPFileSystem *>(dst);
    SFTPFileSystem *remote = rsrc ? rsrc : rdst;
    std::unique_ptr<SFTPFileSystem> dedicated;
    if (remote && (srcIsDir || singleSize >= kDedicatedSessionMin)) {
        if (SSHSessionPtr twin = openSiblingSession(remote->session())) {
            dedicated = std::make_unique<SFTPFileSystem>(std::move(twin));
            if (rsrc)
                src = dedicated.get();
            else
                dst = dedicated.get();
        }
    }

    if (!srcIsDir) {
        // Einzeldatei
        qint64 copied = 0;
        copyOneFile(src, srcPath, dst, dstPath, copied, singleSize, progress, resume);
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
        copyOneFile(src, sp, dst, dp, before, total, progress, resume);
        copied += sz;
        progress(copied, total);
    }
}

bool verifyTree(FileSystemProvider *src, const QString &srcPath,
                FileSystemProvider *dst, const QString &dstPath)
{
    // Das Ziel je Verzeichnis EINMAL listen statt fuer jede Datei einzeln zu
    // statten — bei SFTP war das ein Netz-Roundtrip pro Datei.
    QHash<QString, qint64> dstFileSize;
    QSet<QString> dstDirs, dstLinks;
    try {
        for (const FileEntry &d : dst->listDir(dstPath)) {
            if (d.type == EntryType::Parent)
                continue;
            if (d.type == EntryType::Dir)
                dstDirs.insert(d.name);
            else if (d.type == EntryType::Symlink)
                dstLinks.insert(d.name);   // Zieltyp unklar -> unten einzeln pruefen
            else
                dstFileSize.insert(d.name, d.size);
        }
    } catch (...) {
        return false;   // Ziel nicht lesbar -> nicht verifizierbar
    }
    for (const FileEntry &e : src->listDir(srcPath)) {
        if (e.type == EntryType::Parent || e.type == EntryType::Symlink)
            continue;
        const QString sp = src->join(srcPath, e.name);
        const QString dp = dst->join(dstPath, e.name);
        if (e.isDir()) {
            const bool isDir =
                dstDirs.contains(e.name) || (dstLinks.contains(e.name) && pathIsDir(dst, dp));
            if (!isDir || !verifyTree(src, sp, dst, dp))
                return false;
        } else {
            const qint64 ssize = e.size ? e.size : pathSize(src, sp);
            qint64 dsize = -1;
            if (dstFileSize.contains(e.name))
                dsize = dstFileSize.value(e.name);
            else if (dstLinks.contains(e.name))
                dsize = pathSize(dst, dp);
            if (dsize != ssize)
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
