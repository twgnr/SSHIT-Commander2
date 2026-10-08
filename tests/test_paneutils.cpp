// Tests fuer Pane-Hilfsmodule: natsort, gitstatus, fileops.
#include "tests/harness.hpp"

#include "ncssh/core/fileops.hpp"
#include "ncssh/core/gitstatus.hpp"
#include "ncssh/core/natsort.hpp"

#include <QDir>
#include <QFile>
#include <QProcess>
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

TEST(paneutils, parse_porcelain_in_subdirectory)
{
    // Porcelain-Pfade sind wurzelrelativ — in einem Unterordner muss der
    // Prefix abgeschnitten und Fremdes ignoriert werden.
    const QString text = QStringLiteral(
        " M src/gui/panel.cpp\n"
        "?? src/gui/neu/\n"
        " M src/core/x.cpp\n"
        " M README.md\n"
        "?? \"src/gui/mit leer.txt\"\n"
        " M src/gui/Ärger.txt\n");
    const auto st = parsePorcelain(text, QStringLiteral("src/gui/"));
    CHECK_EQ(st.size(), qsizetype(4));
    CHECK_EQ(st.value(QStringLiteral("panel.cpp")), QStringLiteral("M"));
    CHECK_EQ(st.value(QStringLiteral("neu")), QStringLiteral("?"));
    CHECK_EQ(st.value(QStringLiteral("mit leer.txt")), QStringLiteral("?"));
    CHECK_EQ(st.value(QStringLiteral("Ärger.txt")), QStringLiteral("M"));
    CHECK(!st.contains(QStringLiteral("README.md")));
    CHECK(!st.contains(QStringLiteral("x.cpp")));
    CHECK(!st.contains(kGitAllEntries));

    // Im neuen Ordner (oder darunter) meldet git nur den Ordner selbst.
    CHECK_EQ(parsePorcelain(QStringLiteral("?? src/gui/neu/\n"), QStringLiteral("src/gui/neu/"))
                 .value(kGitAllEntries),
             QStringLiteral("?"));
    CHECK_EQ(parsePorcelain(QStringLiteral("?? src/gui/neu/\n"),
                            QStringLiteral("src/gui/neu/tief/"))
                 .value(kGitAllEntries),
             QStringLiteral("?"));
    // Namensvetter ("neuer/") und geaenderte Ordner zaehlen nicht.
    CHECK(parsePorcelain(QStringLiteral("?? src/neuer/\n"), QStringLiteral("src/neu/")).isEmpty());
    CHECK(!parsePorcelain(QStringLiteral(" M src/gui/\n"), QStringLiteral("src/gui/"))
               .contains(kGitAllEntries));
}

TEST(paneutils, git_status_in_subdirectory)
{
    // Echtes Repo: Aenderungen im Unterordner muessen dort ankommen.
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    auto git = [&](const QStringList &args) {
        QProcess p;
        p.start(QStringLiteral("git"), QStringList{QStringLiteral("-C"), tmp.path()} + args);
        return p.waitForFinished(10000) && p.exitCode() == 0;
    };
    if (!git({QStringLiteral("init"), QStringLiteral("-q")}))
        return;   // kein git installiert
    QDir(tmp.path()).mkpath(QStringLiteral("sub/inner"));
    writeBytes(tmp.filePath(QStringLiteral("sub/alt.txt")), QByteArrayLiteral("a"));
    writeBytes(tmp.filePath(QStringLiteral("top.txt")), QByteArrayLiteral("t"));
    CHECK(git({QStringLiteral("add"), QStringLiteral(".")}));
    CHECK(git({QStringLiteral("-c"), QStringLiteral("user.name=t"), QStringLiteral("-c"),
               QStringLiteral("user.email=t@t"), QStringLiteral("commit"), QStringLiteral("-qm"),
               QStringLiteral("init")}));
    writeBytes(tmp.filePath(QStringLiteral("sub/alt.txt")), QByteArrayLiteral("b"));
    writeBytes(tmp.filePath(QStringLiteral("sub/inner/neu.txt")), QByteArrayLiteral("n"));
    writeBytes(tmp.filePath(QStringLiteral("top.txt")), QByteArrayLiteral("u"));

    const auto st = gitStatus(tmp.filePath(QStringLiteral("sub")));
    CHECK_EQ(st.value(QStringLiteral("alt.txt")), QStringLiteral("M"));
    CHECK_EQ(st.value(QStringLiteral("inner")), QStringLiteral("?"));
    CHECK(!st.contains(QStringLiteral("top.txt")));
    CHECK(!st.contains(QStringLiteral("sub")));

    // Im neuen Ordner selbst: jede neue Datei einzeln, Ignoriertes nicht
    // (frueher blieb dort alles ungefaerbt).
    writeBytes(tmp.filePath(QStringLiteral(".gitignore")), QByteArrayLiteral("*.log\n"));
    QDir(tmp.path()).mkpath(QStringLiteral("sub/inner/tief"));
    writeBytes(tmp.filePath(QStringLiteral("sub/inner/tief/z.txt")), QByteArrayLiteral("z"));
    writeBytes(tmp.filePath(QStringLiteral("sub/inner/debug.log")), QByteArrayLiteral("x"));
    const auto inner = gitStatus(tmp.filePath(QStringLiteral("sub/inner")));
    CHECK_EQ(inner.value(QStringLiteral("neu.txt")), QStringLiteral("?"));
    CHECK_EQ(inner.value(QStringLiteral("tief")), QStringLiteral("?"));
    CHECK(!inner.contains(QStringLiteral("debug.log")));
    CHECK(!inner.contains(kGitAllEntries));
    CHECK_EQ(gitStatus(tmp.filePath(QStringLiteral("sub/inner/tief"))).value(QStringLiteral("z.txt")),
             QStringLiteral("?"));
}

TEST(paneutils, child_towards_and_aggregate)
{
    CHECK_EQ(childTowards(QStringLiteral("C:\\Repository"),
                          QStringLiteral("C:\\Repository\\App\\sub")), QStringLiteral("App"));
    CHECK_EQ(childTowards(QStringLiteral("C:\\"), QStringLiteral("C:\\Repository\\App")),
             QStringLiteral("Repository"));
    CHECK_EQ(childTowards(QStringLiteral("C:/Repository/"), QStringLiteral("C:\\Repository\\App")),
             QStringLiteral("App"));
    // Der Repo-Ordner selbst bzw. Fremdes/Namensvettern -> nichts.
    CHECK(childTowards(QStringLiteral("C:\\Repository\\App"),
                       QStringLiteral("C:\\Repository\\App")).isEmpty());
    CHECK(childTowards(QStringLiteral("C:\\Repo"), QStringLiteral("C:\\Repository\\App")).isEmpty());
    CHECK(childTowards(QStringLiteral("D:\\"), QStringLiteral("C:\\Repository")).isEmpty());
#ifdef Q_OS_WIN
    CHECK_EQ(childTowards(QStringLiteral("c:\\repository"), QStringLiteral("C:\\Repository\\App")),
             QStringLiteral("App"));
#endif

    CHECK(aggregateBadge({}).isEmpty());
    CHECK_EQ(aggregateBadge({{QStringLiteral("a"), QStringLiteral("?")},
                             {QStringLiteral("b"), QStringLiteral("?")}}), QStringLiteral("?"));
    // Hinzugefuegt + unverfolgt ist beides "neu" (gruen), nicht "gemischt".
    CHECK_EQ(aggregateBadge({{QStringLiteral("a"), QStringLiteral("?")},
                             {QStringLiteral("b"), QStringLiteral("A")}}), QStringLiteral("A"));
    CHECK_EQ(aggregateBadge({{QStringLiteral("a"), QStringLiteral("?")},
                             {QStringLiteral("b"), QStringLiteral("M")}}), QStringLiteral("M"));
    CHECK_EQ(mergeGitBadges(QStringLiteral("D"), QStringLiteral("D")), QStringLiteral("D"));
    CHECK_EQ(mergeGitBadges(QStringLiteral("A"), QStringLiteral("D")), QStringLiteral("M"));
    CHECK_EQ(mergeGitBadges(QString(), QStringLiteral("?")), QStringLiteral("?"));
}

TEST(paneutils, repo_changes_marked_up_to_the_top)
{
    // Repo tief unten mit Aenderung -> jede Ebene darueber markiert das Kind
    // auf dem Weg dorthin; ein sauberes Repo markiert nichts.
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    const QString repo = tmp.filePath(QStringLiteral("projekte/web/app"));
    const QString clean = tmp.filePath(QStringLiteral("projekte/sauber"));
    QDir().mkpath(repo);
    QDir().mkpath(clean);
    auto git = [](const QString &dir, const QStringList &args) {
        QProcess p;
        p.start(QStringLiteral("git"), QStringList{QStringLiteral("-C"), dir} + args);
        return p.waitForFinished(10000) && p.exitCode() == 0;
    };
    if (!git(repo, {QStringLiteral("init"), QStringLiteral("-q")}))
        return;   // kein git installiert
    CHECK(git(clean, {QStringLiteral("init"), QStringLiteral("-q")}));
    writeBytes(repo + QStringLiteral("/neu.txt"), QByteArrayLiteral("n"));

    const QStringList roots{QDir::toNativeSeparators(repo), QDir::toNativeSeparators(clean)};
    const auto top = repoAncestorMarks(tmp.path(), roots);
    CHECK_EQ(top.size(), qsizetype(1));
    CHECK_EQ(top.value(QStringLiteral("projekte")), QStringLiteral("?"));
    const auto mid = repoAncestorMarks(tmp.filePath(QStringLiteral("projekte")), roots);
    CHECK_EQ(mid.value(QStringLiteral("web")), QStringLiteral("?"));
    CHECK(!mid.contains(QStringLiteral("sauber")));
    CHECK_EQ(repoAncestorMarks(tmp.filePath(QStringLiteral("projekte/web")), roots)
                 .value(QStringLiteral("app")), QStringLiteral("?"));
    CHECK(repoAncestorMarks(repo, roots).isEmpty());   // im Repo: normale Faerbung

    const GitRepoInfo info = gitRepoInfo(repo);
    CHECK_EQ(QDir::cleanPath(QDir::fromNativeSeparators(info.root)).toLower(),
             QDir::cleanPath(QDir::fromNativeSeparators(repo)).toLower());
    CHECK(info.originUrl.isEmpty());
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
