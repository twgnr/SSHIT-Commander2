// Tests fuer den ls-Parser (core/lsparse) — rein, ohne I/O.
// (Port von tests/test_lsparse.py)
#include "tests/harness.hpp"

#include "ncssh/core/lsparse.hpp"
#include "ncssh/core/models.hpp"

#include <QHash>

using namespace ncssh::core;

namespace {
const QString kSample = QStringLiteral(
    "total 12\n"
    "drwxr-x--- 2 0 0 4096 2024-01-02 13:45 secret dir\n"    // Name mit Leerzeichen
    "-rw-r----- 1 0 1000 120 2024-06-01 09:00 key.pem\n"
    "lrwxrwxrwx 1 0 0 7 2023-12-31 23:59 link -> /etc/target\n");

QHash<QString, FileEntry> byName(const QString &text)
{
    QHash<QString, FileEntry> out;
    for (const FileEntry &e : parseLsLong(text))
        out.insert(e.name, e);
    return out;
}
} // namespace

TEST(lsparse, parse_basic_entries)
{
    const auto e = byName(kSample);
    CHECK_EQ(e.size(), 3);
    CHECK(e.contains(QStringLiteral("secret dir")));
    CHECK(e.contains(QStringLiteral("key.pem")));
    CHECK(e.contains(QStringLiteral("link")));

    const FileEntry d = e.value(QStringLiteral("secret dir"));
    CHECK(d.type == EntryType::Dir);
    CHECK(d.isDir());
    CHECK_EQ(d.size, qint64(4096));
    CHECK_EQ(d.owner, QStringLiteral("0"));
    CHECK_EQ(d.group, QStringLiteral("0"));
    CHECK_EQ(d.permString(), QStringLiteral("rwxr-x---"));
    CHECK_EQ(d.permOctal(), QStringLiteral("750"));
    CHECK(d.modified.isValid());
    CHECK_EQ(d.modified.date().year(), 2024);
}

TEST(lsparse, parse_file_and_symlink)
{
    const auto e = byName(kSample);
    const FileEntry f = e.value(QStringLiteral("key.pem"));
    CHECK(f.type == EntryType::File);
    CHECK_EQ(f.group, QStringLiteral("1000"));
    CHECK_EQ(f.permString(), QStringLiteral("rw-r-----"));

    const FileEntry s = e.value(QStringLiteral("link"));
    CHECK(s.type == EntryType::Symlink);
    CHECK_EQ(s.linkTarget, QStringLiteral("/etc/target"));
}

TEST(lsparse, parse_skips_total_and_garbage)
{
    CHECK(parseLsLong(QStringLiteral("total 0\n")).empty());
    CHECK(parseLsLong(QStringLiteral("\n  \nkaputt\n")).empty());
}

TEST(lsparse, mode_from_perms)
{
    // S_IFDIR / S_IFLNK / setuid / sticky nachbilden (POSIX-Konstanten).
    CHECK((modeFromPerms(QStringLiteral("drwxr-xr-x")) & 0170000) == 0040000);
    CHECK_EQ(int(modeFromPerms(QStringLiteral("drwxr-xr-x")) & 0777), 0755);
    CHECK_EQ(int(modeFromPerms(QStringLiteral("-rw-r--r--")) & 0777), 0644);
    CHECK((modeFromPerms(QStringLiteral("lrwxrwxrwx")) & 0170000) == 0120000);
    CHECK(modeFromPerms(QStringLiteral("-rwsr-xr-x")) & 04000);   // setuid erkannt
    CHECK(modeFromPerms(QStringLiteral("drwxr-xr-t")) & 01000);   // sticky erkannt
}
