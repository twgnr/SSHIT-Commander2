// Tests fuer den Transfer (net/transfer) — Inhalt, Fortschritt, Baum.
// (Port von tests/test_transfer_local.py)
//
// Der lokale Pfad laeuft ueber localCopyTree, der generische ueber die
// Provider-Methoden; beides wird hier ueber transferWithProgress angesteuert
// (die Hilfsfunktionen sind im Original privat und hier dateilokal).
#include "tests/harness.hpp"

#include "ncssh/core/filesystem.hpp"
#include "ncssh/net/transfer.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QRandomGenerator>
#include <QSet>
#include <QTemporaryDir>
#include <algorithm>

using namespace ncssh::core;
using namespace ncssh::net;

namespace {

// In-Memory-Provider (genug fuer den generischen Kopierpfad).
class FakeFS : public FileSystemProvider
{
public:
    FakeFS()
    {
        isRemote = true;
        label = QStringLiteral("fake");
        dirs.insert(QStringLiteral("/"));
    }

    QMap<QString, QByteArray> files;
    QSet<QString> dirs;

    bool isDir(const QString &p) override { return dirs.contains(p); }

    std::vector<FileEntry> listDir(const QString &p) override
    {
        std::vector<FileEntry> out;
        for (auto it = files.begin(); it != files.end(); ++it) {
            if (parent(it.key()) == p) {
                FileEntry e;
                e.name = basename(it.key());
                e.type = EntryType::File;
                e.size = it.value().size();
                out.push_back(e);
            }
        }
        for (const QString &d : dirs) {
            if (d != p && parent(d) == p) {
                FileEntry e;
                e.name = basename(d);
                e.type = EntryType::Dir;
                out.push_back(e);
            }
        }
        return out;
    }

    void mkdir(const QString &p) override { dirs.insert(p); }
    void remove(const QString &p, bool) override { files.remove(p); dirs.remove(p); }

    QByteArray readBytes(const QString &p, qint64 maxBytes) override
    {
        return files.value(p).left(int(maxBytes));
    }
    void writeBytes(const QString &p, const QByteArray &data) override { files.insert(p, data); }

    QString readText(const QString &p, qint64 maxBytes) override
    {
        return QString::fromUtf8(readBytes(p, maxBytes));
    }
    void writeText(const QString &p, const QString &c) override { writeBytes(p, c.toUtf8()); }

    void rename(const QString &o, const QString &n) override
    {
        if (files.contains(o))
            files.insert(n, files.take(o));
    }
    void chmod(const QString &, quint32) override {}

    QString join(const QString &a, const QString &b) const override
    {
        if (a.endsWith(QLatin1Char('/')))
            return a + b;
        return a + QLatin1Char('/') + b;
    }
    QString parent(const QString &p) const override
    {
        const int i = p.lastIndexOf(QLatin1Char('/'));
        if (i <= 0)
            return QStringLiteral("/");
        return p.left(i);
    }
    QString basename(const QString &p) const override
    {
        return p.mid(p.lastIndexOf(QLatin1Char('/')) + 1);
    }
    QString home() override { return QStringLiteral("/"); }

    qint64 size(const QString &p) override { return files.value(p).size(); }
};

void writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(data);
    f.close();
}

QByteArray readFile(const QString &path)
{
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    return f.readAll();
}

QByteArray randomBytes(int n)
{
    QByteArray out(n, Qt::Uninitialized);
    QRandomGenerator::global()->fillRange(reinterpret_cast<quint32 *>(out.data()), n / 4);
    for (int i = (n / 4) * 4; i < n; ++i)
        out[i] = char(QRandomGenerator::global()->bounded(256));
    return out;
}

} // namespace

TEST(transfer, provider_copy_tree)
{
    FakeFS src;
    src.dirs.insert(QStringLiteral("/s"));
    src.dirs.insert(QStringLiteral("/s/sub"));
    src.files.insert(QStringLiteral("/s/a.txt"), QByteArrayLiteral("hello"));
    src.files.insert(QStringLiteral("/s/sub/b.bin"), QByteArrayLiteral("XY"));
    FakeFS dst;

    std::vector<std::pair<qint64, qint64>> seen;
    transferWithProgress(&src, QStringLiteral("/s"), &dst, QStringLiteral("/d"),
                         [&](qint64 c, qint64 t) { seen.emplace_back(c, t); });

    CHECK_EQ(dst.files.value(QStringLiteral("/d/a.txt")), QByteArrayLiteral("hello"));
    CHECK_EQ(dst.files.value(QStringLiteral("/d/sub/b.bin")), QByteArrayLiteral("XY"));
    CHECK(dst.dirs.contains(QStringLiteral("/d/sub")));
    CHECK(!seen.empty());
    // 5 + 2 Bytes, vollstaendig
    CHECK_EQ(seen.back().first, qint64(7));
    CHECK_EQ(seen.back().second, qint64(7));
}

TEST(transfer, provider_copy_single_file)
{
    FakeFS src;
    src.files.insert(QStringLiteral("/f"), QByteArrayLiteral("data"));
    FakeFS dst;
    transferWithProgress(&src, QStringLiteral("/f"), &dst, QStringLiteral("/g"),
                         [](qint64, qint64) {});
    CHECK_EQ(dst.files.value(QStringLiteral("/g")), QByteArrayLiteral("data"));
}

TEST(transfer, copy_file_content_and_progress)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    const QString src = tmp.filePath(QStringLiteral("a.bin"));
    const QString dst = tmp.filePath(QStringLiteral("b.bin"));
    const QByteArray data = randomBytes(2 * 1024 * 1024 + 123);
    writeFile(src, data);

    LocalFileSystem fs;
    std::vector<qint64> seen;
    transferWithProgress(&fs, src, &fs, dst, [&](qint64 c, qint64) { seen.push_back(c); });

    CHECK_EQ(readFile(dst), data);
    CHECK(!seen.empty());
    // Fortschritt endet bei der Gesamtgroesse
    CHECK_EQ(seen.back(), qint64(data.size()));
    // monoton steigend
    CHECK(std::is_sorted(seen.begin(), seen.end()));
}

TEST(transfer, local_copy_tree)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    const QString src = tmp.filePath(QStringLiteral("src"));
    writeFile(src + QStringLiteral("/a.txt"), QByteArrayLiteral("hello"));
    writeFile(src + QStringLiteral("/sub/b.txt"), QByteArrayLiteral("world!!"));
    const QString out = tmp.filePath(QStringLiteral("out"));

    LocalFileSystem fs;
    std::vector<std::pair<qint64, qint64>> seen;
    transferWithProgress(&fs, src, &fs, out,
                         [&](qint64 c, qint64 t) { seen.emplace_back(c, t); });

    CHECK_EQ(readFile(out + QStringLiteral("/a.txt")), QByteArrayLiteral("hello"));
    CHECK_EQ(readFile(out + QStringLiteral("/sub/b.txt")), QByteArrayLiteral("world!!"));
    CHECK(!seen.empty());
    // 5 + 7 Bytes, vollstaendig
    CHECK_EQ(seen.back().first, qint64(12));
    CHECK_EQ(seen.back().second, qint64(12));
}

TEST(transfer, overwrite_existing_target)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    const QString src = tmp.filePath(QStringLiteral("a.bin"));
    const QString dst = tmp.filePath(QStringLiteral("b.bin"));
    writeFile(src, QByteArrayLiteral("NEU"));
    writeFile(dst, QByteArrayLiteral("ALT-und-laenger"));

    LocalFileSystem fs;
    transferWithProgress(&fs, src, &fs, dst, [](qint64, qint64) {});
    // Ziel wird ersetzt, nicht angehaengt
    CHECK_EQ(readFile(dst), QByteArrayLiteral("NEU"));
}

TEST(transfer, rejects_path_traversal_names)
{
    // Boesartiger Server: listDir liefert einen Namen mit ".." und Trenner.
    class EvilFS : public FakeFS
    {
    public:
        std::vector<FileEntry> listDir(const QString &p) override
        {
            std::vector<FileEntry> out;
            if (p == QLatin1String("/s")) {
                FileEntry ok;
                ok.name = QStringLiteral("gut.txt");
                ok.type = EntryType::File;
                ok.size = 3;
                out.push_back(ok);
                FileEntry evil;
                evil.name = QStringLiteral("../../evil.txt");   // Traversal-Versuch
                evil.type = EntryType::File;
                evil.size = 5;
                out.push_back(evil);
            }
            return out;
        }
        qint64 size(const QString &p) override
        {
            return p.endsWith(QLatin1String("gut.txt")) ? 3 : 5;
        }
        bool isDir(const QString &p) override { return p == QLatin1String("/s"); }
    };

    EvilFS src;
    src.files.insert(QStringLiteral("/s/gut.txt"), QByteArrayLiteral("abc"));
    FakeFS dst;
    dst.dirs.insert(QStringLiteral("/d"));
    transferWithProgress(&src, QStringLiteral("/s"), &dst, QStringLiteral("/d/s"),
                         [](qint64, qint64) {});

    // Die harmlose Datei kommt an ...
    CHECK_EQ(dst.files.value(QStringLiteral("/d/s/gut.txt")), QByteArrayLiteral("abc"));
    // ... der Traversal-Name wird NICHT ausserhalb des Ziels geschrieben.
    CHECK(!dst.files.contains(QStringLiteral("/evil.txt")));
    CHECK(!dst.files.contains(QStringLiteral("/d/../../evil.txt")));
    for (auto it = dst.files.begin(); it != dst.files.end(); ++it)
        CHECK(it.key().startsWith(QLatin1String("/d/")));
}

TEST(transfer, resume_continues_partial_file)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    const QString src = tmp.filePath(QStringLiteral("a.bin"));
    const QString dst = tmp.filePath(QStringLiteral("b.bin"));
    const QByteArray data = randomBytes(500000);
    writeFile(src, data);
    // Teil-Ziel: erste 200000 Bytes sind bereits (korrekt) da.
    const qint64 partial = 200000;
    writeFile(dst, data.left(int(partial)));

    LocalFileSystem fs;
    std::vector<qint64> seen;
    transferWithProgress(&fs, src, &fs, dst, [&](qint64 c, qint64) { seen.push_back(c); },
                         /*resume=*/true);

    CHECK_EQ(readFile(dst), data);             // vollstaendig und korrekt
    CHECK(!seen.empty());
    CHECK_EQ(seen.back(), qint64(data.size())); // Fortschritt endet bei der Gesamtgroesse
    // Resume: der Fortschritt beginnt bei den bereits vorhandenen Bytes, nicht bei 0.
    CHECK(seen.front() >= partial);
}

TEST(transfer, resume_skips_completed_file)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    const QString src = tmp.filePath(QStringLiteral("a.bin"));
    const QString dst = tmp.filePath(QStringLiteral("b.bin"));
    const QByteArray data = randomBytes(4096);
    writeFile(src, data);
    writeFile(dst, data);   // Ziel ist schon vollstaendig

    LocalFileSystem fs;
    transferWithProgress(&fs, src, &fs, dst, [](qint64, qint64) {}, /*resume=*/true);
    CHECK_EQ(readFile(dst), data);   // bleibt korrekt (keine Beschaedigung)
}

TEST(transfer, direction_and_verify_tree)
{
    LocalFileSystem local;
    FakeFS remote;
    CHECK_EQ(directionOf(&local, &local), QStringLiteral("local"));
    CHECK_EQ(directionOf(&local, &remote), QStringLiteral("upload"));
    CHECK_EQ(directionOf(&remote, &local), QStringLiteral("download"));
    CHECK_EQ(directionOf(&remote, &remote), QStringLiteral("remote"));

    FakeFS src;
    src.dirs.insert(QStringLiteral("/s"));
    src.files.insert(QStringLiteral("/s/a.txt"), QByteArrayLiteral("hello"));
    FakeFS dst;
    transferWithProgress(&src, QStringLiteral("/s"), &dst, QStringLiteral("/d"),
                         [](qint64, qint64) {});
    CHECK(verifyTree(&src, QStringLiteral("/s"), &dst, QStringLiteral("/d")));

    // Ziel manipulieren -> Verifikation muss anschlagen
    dst.files.insert(QStringLiteral("/d/a.txt"), QByteArrayLiteral("kurz"));
    CHECK(!verifyTree(&src, QStringLiteral("/s"), &dst, QStringLiteral("/d")));
}
