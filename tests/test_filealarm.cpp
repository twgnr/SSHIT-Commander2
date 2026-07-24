// Tests fuer die Datei-Alarm-Logik (scanDir/diffSnapshots/Spec) — ohne Qt-GUI.
// (Port von tests/test_filealarm.py)
#include "tests/harness.hpp"

#include "ncssh/core/filealarm.hpp"

#include <QDir>
#include <QFile>
#include <QSet>
#include <QTemporaryDir>

using namespace ncssh::core;

namespace {
SnapshotEntry entry(qint64 mtime, qint64 size, bool isDir)
{
    SnapshotEntry e;
    e.mtime = mtime;
    e.size = size;
    e.isDir = isDir;
    return e;
}

// (art, pfad)-Paare als Set, damit die Reihenfolge egal ist.
QSet<QString> kindPathSet(const std::vector<std::tuple<QString, QString, bool>> &events)
{
    QSet<QString> out;
    for (const auto &[kind, path, isDir] : events)
        out.insert(kind + QLatin1Char('|') + path);
    return out;
}

void touch(const QString &path)
{
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.close();
}

bool anyPathEndsWith(const Snapshot &snap, const QString &suffix)
{
    for (auto it = snap.begin(); it != snap.end(); ++it) {
        if (it.key().endsWith(suffix))
            return true;
    }
    return false;
}
} // namespace

TEST(filealarm, diff_created_and_modified)
{
    const Snapshot oldSnap = {{QStringLiteral("/a"), entry(100, 5, false)},
                              {QStringLiteral("/b"), entry(100, 5, false)}};
    const Snapshot newSnap = {{QStringLiteral("/a"), entry(100, 5, false)},
                              {QStringLiteral("/b"), entry(200, 9, false)},
                              {QStringLiteral("/c"), entry(100, 5, false)}};
    const auto kinds = kindPathSet(diffSnapshots(oldSnap, newSnap));
    CHECK(kinds.contains(QStringLiteral("created|/c")));
    CHECK(kinds.contains(QStringLiteral("modified|/b")));
    CHECK(!kinds.contains(QStringLiteral("modified|/a")));   // unveraendert
}

TEST(filealarm, diff_deleted)
{
    const Snapshot oldSnap = {{QStringLiteral("/x"), entry(1, 1, false)}};
    const auto events = diffSnapshots(oldSnap, Snapshot{});
    CHECK_EQ(events.size(), size_t(1));
    CHECK_EQ(std::get<0>(events[0]), QStringLiteral("deleted"));
    CHECK_EQ(std::get<1>(events[0]), QStringLiteral("/x"));
}

TEST(filealarm, diff_respects_options)
{
    const Snapshot newSnap = {{QStringLiteral("/n"), entry(1, 1, false)}};
    CHECK(diffSnapshots(Snapshot{}, newSnap, /*onCreated=*/false).empty());
    const auto kinds = kindPathSet(diffSnapshots(Snapshot{}, newSnap, /*onCreated=*/true));
    CHECK(kinds.contains(QStringLiteral("created|/n")));
}

TEST(filealarm, diff_skips_dir_modified)
{
    const Snapshot oldSnap = {{QStringLiteral("/d"), entry(100, 0, true)}};
    const Snapshot newSnap = {{QStringLiteral("/d"), entry(200, 0, true)}};
    // Ordner-mtime wird bewusst ignoriert (zu "laut").
    CHECK(diffSnapshots(oldSnap, newSnap).empty());
}

TEST(filealarm, scan_dir_basic_and_recursive)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    touch(tmp.filePath(QStringLiteral("a.txt")));
    const Snapshot s1 = scanDir(tmp.path());
    CHECK(anyPathEndsWith(s1, QStringLiteral("a.txt")));

    touch(tmp.filePath(QStringLiteral("b.txt")));
    const Snapshot s2 = scanDir(tmp.path());
    const auto kinds = kindPathSet(diffSnapshots(s1, s2));
    bool sawCreatedB = false;
    for (const QString &k : kinds) {
        if (k.startsWith(QStringLiteral("created|")) && k.endsWith(QStringLiteral("b.txt")))
            sawCreatedB = true;
    }
    CHECK(sawCreatedB);

    QDir(tmp.path()).mkdir(QStringLiteral("sub"));
    touch(tmp.filePath(QStringLiteral("sub/c.txt")));
    CHECK(anyPathEndsWith(scanDir(tmp.path(), /*recursive=*/true), QStringLiteral("c.txt")));
    CHECK(!anyPathEndsWith(scanDir(tmp.path(), /*recursive=*/false), QStringLiteral("c.txt")));
}

TEST(filealarm, scan_dir_include_dirs)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    QDir(tmp.path()).mkdir(QStringLiteral("sub"));
    CHECK(anyPathEndsWith(scanDir(tmp.path(), false, /*includeDirs=*/true),
                          QStringLiteral("sub")));
    CHECK(!anyPathEndsWith(scanDir(tmp.path(), false, /*includeDirs=*/false),
                           QStringLiteral("sub")));
}

TEST(filealarm, glob_filter_include_exclude)
{
    // Ohne Filter passt alles.
    CHECK(matchesGlobFilter(QStringLiteral("x.tmp"), QString(), QString()));
    // Include: nur passende Namen.
    CHECK(matchesGlobFilter(QStringLiteral("app.log"), QStringLiteral("*.log;*.csv"), QString()));
    CHECK(!matchesGlobFilter(QStringLiteral("app.txt"), QStringLiteral("*.log;*.csv"), QString()));
    // Exclude sticht: passt zwar zum Include, wird aber ausgeschlossen.
    CHECK(!matchesGlobFilter(QStringLiteral("a~"), QStringLiteral("*"), QStringLiteral("*~;*.tmp")));
    CHECK(matchesGlobFilter(QStringLiteral("a.log"), QStringLiteral("*"), QStringLiteral("*~;*.tmp")));
}

TEST(filealarm, scan_dir_applies_glob_filter)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    touch(tmp.filePath(QStringLiteral("keep.log")));
    touch(tmp.filePath(QStringLiteral("skip.tmp")));
    touch(tmp.filePath(QStringLiteral("other.txt")));

    const Snapshot only = scanDir(tmp.path(), false, true, QStringLiteral("*.log"), QString());
    CHECK(anyPathEndsWith(only, QStringLiteral("keep.log")));
    CHECK(!anyPathEndsWith(only, QStringLiteral("skip.tmp")));
    CHECK(!anyPathEndsWith(only, QStringLiteral("other.txt")));

    const Snapshot noTmp = scanDir(tmp.path(), false, true, QString(), QStringLiteral("*.tmp"));
    CHECK(anyPathEndsWith(noTmp, QStringLiteral("other.txt")));
    CHECK(!anyPathEndsWith(noTmp, QStringLiteral("skip.tmp")));
}

TEST(filealarm, alarmspec_json_roundtrip)
{
    AlarmSpec a;
    a.id = 3;
    a.name = QStringLiteral("Backup");
    a.path = QStringLiteral("/data");
    a.onModified = false;
    a.recursive = true;
    a.includeGlob = QStringLiteral("*.log");
    a.excludeGlob = QStringLiteral("*.tmp");
    a.actionCmd = QStringLiteral("echo {kind} {path}");

    const AlarmSpec back = AlarmSpec::fromJson(a.toJson());
    CHECK_EQ(back.id, a.id);
    CHECK_EQ(back.name, a.name);
    CHECK_EQ(back.path, a.path);
    CHECK_EQ(back.onCreated, a.onCreated);
    CHECK_EQ(back.onModified, a.onModified);
    CHECK_EQ(back.onDeleted, a.onDeleted);
    CHECK_EQ(back.recursive, a.recursive);
    CHECK_EQ(back.includeDirs, a.includeDirs);
    CHECK_EQ(back.enabled, a.enabled);
    CHECK_EQ(back.includeGlob, a.includeGlob);
    CHECK_EQ(back.excludeGlob, a.excludeGlob);
    CHECK_EQ(back.actionCmd, a.actionCmd);
}
