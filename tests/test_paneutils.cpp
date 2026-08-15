// Tests fuer Pane-Hilfsmodule: natsort, gitstatus, fileops.
#include "tests/harness.hpp"

#include "ncssh/core/fileops.hpp"
#include "ncssh/core/gitstatus.hpp"
#include "ncssh/core/natsort.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <algorithm>

using namespace ncssh::core;

namespace {
void writeBytes(const QString &path, const QByteArray &data)
{
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(data);
    f.close();
}
} // namespace

TEST(paneutils, natural_sort_order)
{
    QStringList names = {QStringLiteral("datei10"), QStringLiteral("datei2"),
                         QStringLiteral("datei1"), QStringLiteral("Datei20")};
    std::sort(names.begin(), names.end(), naturalLess);
    CHECK_EQ(names, (QStringList{QStringLiteral("datei1"), QStringLiteral("datei2"),
                                 QStringLiteral("datei10"), QStringLiteral("Datei20")}));
}

TEST(paneutils, natural_key_mixed_no_crash)
{
    // Zahl- und Text-Abschnitte vergleichbar
    CHECK(naturalLess(QStringLiteral("a1"), QStringLiteral("a2")));
    CHECK(naturalLess(QStringLiteral("a2"), QStringLiteral("a10")));
    CHECK(!naturalLess(QString(), QString()));   // gleich -> kein "kleiner"
}

TEST(paneutils, parse_porcelain)
{
    const QString text = QStringLiteral(
        " M src/app.py\n"
        "?? neu.txt\n"
        "A  added.py\n"
        " D weg.txt\n"
        "R  alt.py -> neu.py\n"
        " M sub/inner.py\n"
        " M sub/other.py\n");
    const auto st = parsePorcelain(text);
    CHECK_EQ(st.value(QStringLiteral("src")), QStringLiteral("M"));
    CHECK_EQ(st.value(QStringLiteral("neu.txt")), QStringLiteral("?"));
    CHECK_EQ(st.value(QStringLiteral("added.py")), QStringLiteral("A"));
    CHECK_EQ(st.value(QStringLiteral("weg.txt")), QStringLiteral("D"));
    CHECK_EQ(st.value(QStringLiteral("neu.py")), QStringLiteral("R"));
    // mehrere Aenderungen im selben Ordner -> gemischt = "M"
    CHECK_EQ(st.value(QStringLiteral("sub")), QStringLiteral("M"));
}

TEST(paneutils, hash_file_and_bytes)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("a.bin"));
    writeBytes(path, QByteArrayLiteral("hello"));
    CHECK_EQ(hashFile(path, QStringLiteral("sha256")),
             hashBytes(QByteArrayLiteral("hello"), QStringLiteral("sha256")));
    CHECK_EQ(hashFile(path, QStringLiteral("md5")),
             hashBytes(QByteArrayLiteral("hello"), QStringLiteral("md5")));
}

TEST(paneutils, make_zip_and_dir_size)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    writeBytes(tmp.filePath(QStringLiteral("a.txt")), QByteArray(10, 'x'));
    QDir(tmp.path()).mkdir(QStringLiteral("sub"));
    writeBytes(tmp.filePath(QStringLiteral("sub/b.txt")), QByteArray(5, 'y'));

    const QString archive = tmp.filePath(QStringLiteral("out.zip"));
    try {
        const int n = makeZip(archive, tmp.path(),
                              {QStringLiteral("a.txt"), QStringLiteral("sub")});
        CHECK_EQ(n, 2);
        CHECK(QFile::exists(archive));
        CHECK(QFileInfo(archive).size() > 0);
    } catch (const std::exception &exc) {
        // Ohne bsdtar (tar.exe) laesst sich kein ZIP erzeugen — dann wird die
        // Ursache gemeldet, statt den Test stillschweigend zu bestehen.
        ncssh::tests::reportFailure(__FILE__, __LINE__,
                                    std::string("makeZip: ") + exc.what());
    }

    const auto [total, capped] = dirSize(tmp.path());
    CHECK(total >= 15);
    CHECK_EQ(capped, false);
    const auto [small, capped2] = dirSize(tmp.path(), 1);
    CHECK_EQ(capped2, true);
}
