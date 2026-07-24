// Tests fuer den skriptbaren SFTP-Batch (net::runSftpBatch).
//
// Da der Executor nur das FileSystemProvider-Interface nutzt, laesst er sich
// mit zwei lokalen Providern gegen ein Temp-Verzeichnis vollstaendig pruefen —
// ohne echte SSH-Verbindung.
#include "tests/harness.hpp"

#include "ncssh/core/filesystem.hpp"
#include "ncssh/net/sftp_batch.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

using ncssh::core::LocalFileSystem;
using ncssh::net::runSftpBatch;
using ncssh::net::tokenizeBatchLine;

static void writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(data);
    f.close();
}

TEST(sftp_batch, tokenize_plain)
{
    const QStringList t = tokenizeBatchLine(QStringLiteral("put a.txt /srv/a.txt"));
    CHECK_EQ(t.size(), qsizetype(3));
    CHECK_EQ(t[0], QStringLiteral("put"));
    CHECK_EQ(t[2], QStringLiteral("/srv/a.txt"));
}

TEST(sftp_batch, tokenize_quoted_paths_with_spaces)
{
    const QStringList t = tokenizeBatchLine(QStringLiteral("put \"mein ordner/a b.txt\" \"/srv/x y\""));
    CHECK_EQ(t.size(), qsizetype(3));
    CHECK_EQ(t[1], QStringLiteral("mein ordner/a b.txt"));
    CHECK_EQ(t[2], QStringLiteral("/srv/x y"));
}

TEST(sftp_batch, runs_mkdir_put_rename)
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString base = dir.path();
    writeFile(base + QStringLiteral("/src.txt"), QByteArrayLiteral("hallo welt"));

    LocalFileSystem local;
    LocalFileSystem remote;  // im Test ist "remote" ebenfalls lokal
    const QString script = QStringLiteral(
        "# Kommentar\n"
        "mkdir out\n"
        "put src.txt out/copy.txt\n"
        "rename out/copy.txt out/renamed.txt\n"
        "echo fertig\n");

    const auto res = runSftpBatch(script, &local, &remote, base, base);
    CHECK_EQ(res.failed, 0);
    CHECK(res.ok >= 3);
    CHECK(QFileInfo::exists(remote.join(remote.join(base, QStringLiteral("out")),
                                        QStringLiteral("renamed.txt"))));
    // Die Kopie unter altem Namen darf es nicht mehr geben.
    CHECK(!QFileInfo::exists(remote.join(remote.join(base, QStringLiteral("out")),
                                         QStringLiteral("copy.txt"))));
}

TEST(sftp_batch, get_downloads_file)
{
    QTemporaryDir dir;
    const QString base = dir.path();
    QDir(base).mkpath(QStringLiteral("remote"));
    QDir(base).mkpath(QStringLiteral("local"));
    writeFile(base + QStringLiteral("/remote/data.bin"), QByteArrayLiteral("0123456789"));

    LocalFileSystem local;
    LocalFileSystem remote;
    const QString script = QStringLiteral(
        "cd remote\n"
        "lcd local\n"
        "get data.bin holen.bin\n");
    const auto res = runSftpBatch(script, &local, &remote, base, base);
    CHECK_EQ(res.failed, 0);
    CHECK(QFileInfo::exists(base + QStringLiteral("/local/holen.bin")));
    CHECK_EQ(QFileInfo(base + QStringLiteral("/local/holen.bin")).size(), qint64(10));
}

TEST(sftp_batch, unknown_command_is_reported_and_continues)
{
    QTemporaryDir dir;
    const QString base = dir.path();
    LocalFileSystem local, remote;
    const QString script = QStringLiteral(
        "bloedsinn hier\n"
        "mkdir ordner\n");
    const auto res = runSftpBatch(script, &local, &remote, base, base);
    CHECK_EQ(res.failed, 1);        // die unbekannte Zeile
    CHECK_EQ(res.ok, 1);            // mkdir danach lief trotzdem
    CHECK(QFileInfo::exists(remote.join(base, QStringLiteral("ordner"))));
}

TEST(sftp_batch, stop_on_error_aborts)
{
    QTemporaryDir dir;
    const QString base = dir.path();
    LocalFileSystem local, remote;
    const QString script = QStringLiteral(
        "bloedsinn\n"
        "mkdir sollte_nicht_entstehen\n");
    const auto res = runSftpBatch(script, &local, &remote, base, base, {}, true);
    CHECK(res.aborted);
    CHECK_EQ(res.ok, 0);
    CHECK(!QFileInfo::exists(remote.join(base, QStringLiteral("sollte_nicht_entstehen"))));
}
