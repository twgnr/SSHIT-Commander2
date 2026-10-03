// Runde 2 der Fehlersuche (Kern): Abstuerze/Haenger durch manipulierte oder
// grosse Eingaben und Verzeichnis-Walks, die NTFS-Junctions folgten.
//  - PPK: negativer Zeilenzaehler -> Endlosschleife, riesige Feldlaenge -> OOB
//  - Datei-Vergleich: volle LCS-Tabelle -> GBs Speicher / bad_alloc
//  - ls-Parser: fuehrende Leerzeichen im Namen gingen verloren, Zeiten lokal
//  - dirSize/dirStats/Suche folgten Junctions (Zyklen, Doppelzaehlung)
//  - sudo-isDir maskierte eine sudo-Ablehnung als "keine Ordner"
//  - Massen-Umbenennen: Rueckabwicklung bei Fehler mitten im Zyklus
#include "tests/harness.hpp"

#include "ncssh/core/bulkrename.hpp"
#include "ncssh/core/filediff.hpp"
#include "ncssh/core/fileops.hpp"
#include "ncssh/core/filesearch.hpp"
#include "ncssh/core/filesystem.hpp"
#include "ncssh/core/lsparse.hpp"
#include "ncssh/core/ppk.hpp"
#include "ncssh/net/sudofs.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QRandomGenerator>
#include <QSet>
#include <QTemporaryDir>
#include <QTimeZone>

#include <algorithm>
#include <stdexcept>

using namespace ncssh;

namespace {

QByteArray be32(quint32 n)
{
    QByteArray out;
    out.append(char((n >> 24) & 0xFF));
    out.append(char((n >> 16) & 0xFF));
    out.append(char((n >> 8) & 0xFF));
    out.append(char(n & 0xFF));
    return out;
}

QByteArray sshString(const QByteArray &s)
{
    return be32(quint32(s.size())) + s;
}

QByteArray ppkText(const QByteArray &pubLinesHeader, const QByteArray &pubB64,
                   const QByteArray &privLinesHeader, const QByteArray &privB64)
{
    return "PuTTY-User-Key-File-2: ssh-ed25519\r\n"
           "Encryption: none\r\n"
           "Comment: test\r\n"
           "Public-Lines: " + pubLinesHeader + "\r\n" + pubB64 + "\r\n"
           "Private-Lines: " + privLinesHeader + "\r\n" + privB64 + "\r\n"
           "Private-MAC: 00\r\n";
}

void writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(data);
}

// Klassische LCS-Laenge (nur fuer kleine Testeingaben).
int lcsLength(const QStringList &a, const QStringList &b)
{
    std::vector<std::vector<int>> dp(size_t(a.size()) + 1,
                                     std::vector<int>(size_t(b.size()) + 1, 0));
    for (int i = int(a.size()) - 1; i >= 0; --i)
        for (int j = int(b.size()) - 1; j >= 0; --j)
            dp[size_t(i)][size_t(j)] = (a[i] == b[j])
                ? dp[size_t(i) + 1][size_t(j) + 1] + 1
                : std::max(dp[size_t(i) + 1][size_t(j)], dp[size_t(i)][size_t(j) + 1]);
    return dp[0][0];
}

#ifdef Q_OS_WIN
bool makeJunction(const QString &link, const QString &target)
{
    const int rc = QProcess::execute(
        QStringLiteral("cmd.exe"),
        {QStringLiteral("/c"), QStringLiteral("mklink"), QStringLiteral("/J"),
         QDir::toNativeSeparators(link), QDir::toNativeSeparators(target)});
    return rc == 0 && QFileInfo(link).isJunction();
}
#endif

// Minimale POSIX-Pfadsemantik fuer den sudo-Provider im Test.
class FixPosixPaths : public core::FileSystemProvider
{
public:
    FixPosixPaths() { label = QStringLiteral("srv"); isRemote = true; }
    std::vector<core::FileEntry> listDir(const QString &) override { return {}; }
    bool isDir(const QString &) override { return false; }
    void mkdir(const QString &) override {}
    void remove(const QString &, bool) override {}
    QString readText(const QString &, qint64) override { return {}; }
    void writeText(const QString &, const QString &) override {}
    void writeBytes(const QString &, const QByteArray &) override {}
    QByteArray readBytes(const QString &, qint64) override { return {}; }
    void rename(const QString &, const QString &) override {}
    void chmod(const QString &, quint32) override {}
    QString join(const QString &a, const QString &b) const override
    {
        return (a.endsWith(QLatin1Char('/')) ? a : a + QLatin1Char('/')) + b;
    }
    QString parent(const QString &p) const override
    {
        const int i = int(p.lastIndexOf(QLatin1Char('/')));
        return i <= 0 ? QStringLiteral("/") : p.left(i);
    }
    QString basename(const QString &p) const override
    {
        return p.mid(p.lastIndexOf(QLatin1Char('/')) + 1);
    }
    QString home() override { return QStringLiteral("/root"); }
};

} // namespace

// --- PPK -------------------------------------------------------------------

TEST(fix_core, ppk_negative_line_count_throws)
{
    // Frueher: i += -1 -> die Schleife las dieselbe Zeile endlos.
    const QByteArray data = ppkText("-1", "AAAA", "1", "AAAA");
    CHECK_THROWS(core::ppkToOpenssh(data));
}

TEST(fix_core, ppk_line_count_beyond_file_throws)
{
    const QByteArray data = ppkText("1", "AAAA", "99999", "AAAA");
    CHECK_THROWS(core::ppkToOpenssh(data));
    const QByteArray garbage = ppkText("abc", "AAAA", "1", "AAAA");
    CHECK_THROWS(core::ppkToOpenssh(garbage));
}

TEST(fix_core, ppk_huge_field_length_throws)
{
    // Feldlaenge 0x80000000: als int negativ -> bestand frueher die
    // Grenzpruefung, danach Lesen ausserhalb des Puffers.
    const QByteArray pub = sshString("ssh-ed25519") + be32(0x80000000u) + QByteArray(8, 'x');
    const QByteArray priv = sshString(QByteArray(32, '\x01'));
    const QByteArray data = ppkText("1", pub.toBase64(), "1", priv.toBase64());
    bool threw = false;
    try {
        core::ppkToOpenssh(data);
    } catch (const std::runtime_error &e) {
        threw = true;
        CHECK(QString::fromUtf8(e.what()).contains(QStringLiteral("beschaedigt")));
    }
    CHECK(threw);

    // Ebenso 0xFFFFFFFF.
    const QByteArray pub2 = sshString("ssh-ed25519") + be32(0xFFFFFFFFu);
    CHECK_THROWS(core::ppkToOpenssh(ppkText("1", pub2.toBase64(), "1", priv.toBase64())));
}

TEST(fix_core, ppk_valid_ed25519_still_converts)
{
    const QByteArray pub = sshString("ssh-ed25519") + sshString(QByteArray(32, '\x02'));
    const QByteArray priv = sshString(QByteArray(32, '\x01'));
    const QByteArray out = core::ppkToOpenssh(ppkText("1", pub.toBase64(), "1", priv.toBase64()));
    CHECK(out.startsWith("-----BEGIN OPENSSH PRIVATE KEY-----"));
}

// --- Datei-Vergleich -------------------------------------------------------

TEST(fix_core, diff_small_inputs_minimal_and_reconstructible)
{
    // Zufallseingaben gegen die klassische LCS: Myers muss minimal sein und
    // beide Texte exakt wiedergeben.
    QRandomGenerator rng(12345);
    for (int round = 0; round < 200; ++round) {
        QStringList a, b;
        const int n = int(rng.bounded(25));
        const int m = int(rng.bounded(25));
        for (int i = 0; i < n; ++i)
            a << QString(QChar(u'a' + int(rng.bounded(4))));
        for (int j = 0; j < m; ++j)
            b << QString(QChar(u'a' + int(rng.bounded(4))));
        const QString ta = a.join(QLatin1Char('\n'));
        const QString tb = b.join(QLatin1Char('\n'));
        bool approx = true;
        const auto rows = core::unified(ta, tb, QStringLiteral("A"), QStringLiteral("B"),
                                        100000, &approx);
        CHECK(!approx);
        const QStringList la = ta.split(QLatin1Char('\n'));
        const QStringList lb = tb.split(QLatin1Char('\n'));
        if (rows.empty()) {
            CHECK(la == lb);
            continue;
        }
        QStringList ra, rb;
        int adds = 0, dels = 0;
        for (const auto &[line, kind] : rows) {
            if (kind == QLatin1String("ctx")) { ra << line.mid(1); rb << line.mid(1); }
            else if (kind == QLatin1String("del")) { ra << line.mid(1); ++dels; }
            else if (kind == QLatin1String("add")) { rb << line.mid(1); ++adds; }
        }
        CHECK(ra == la);
        CHECK(rb == lb);
        const int lcs = lcsLength(la, lb);
        CHECK_EQ(dels, int(la.size()) - lcs);
        CHECK_EQ(adds, int(lb.size()) - lcs);
    }
}

TEST(fix_core, diff_large_files_few_changes_exact)
{
    // 30k Zeilen je Seite: die alte LCS-Tabelle haette ~3,6 GB gebraucht.
    QStringList a, b;
    for (int i = 0; i < 30000; ++i)
        a << QStringLiteral("zeile %1").arg(i);
    b = a;
    for (int i = 100; i < 30000; i += 600)
        b[i] = QStringLiteral("geaendert %1").arg(i);
    QElapsedTimer t;
    t.start();
    bool approx = true;
    const auto rows = core::unified(a.join(QLatin1Char('\n')), b.join(QLatin1Char('\n')),
                                    QStringLiteral("A"), QStringLiteral("B"), 3, &approx);
    CHECK(t.elapsed() < 20000);
    CHECK(!approx);
    int adds = 0, dels = 0, hunks = 0;
    for (const auto &[line, kind] : rows) {
        if (kind == QLatin1String("add")) ++adds;
        else if (kind == QLatin1String("del")) ++dels;
        else if (kind == QLatin1String("hunk")) ++hunks;
    }
    CHECK_EQ(adds, 50);
    CHECK_EQ(dels, 50);
    CHECK_EQ(hunks, 50);
}

TEST(fix_core, diff_large_completely_different_is_bounded)
{
    // Kein gemeinsamer Inhalt: Myers erreicht seine Grenze -> grober, aber
    // korrekter Vergleich statt bad_alloc / Einfrieren.
    QStringList a, b;
    for (int i = 0; i < 30000; ++i) {
        a << QStringLiteral("links %1").arg(i);
        b << QStringLiteral("rechts %1").arg(i);
    }
    QElapsedTimer t;
    t.start();
    bool approx = false;
    const auto rows = core::unified(a.join(QLatin1Char('\n')), b.join(QLatin1Char('\n')),
                                    QStringLiteral("A"), QStringLiteral("B"), 3, &approx);
    CHECK(t.elapsed() < 20000);
    CHECK(approx);
    // 2 Kopfzeilen + 1 Hunk + 30000 entfernt + 30000 neu
    CHECK_EQ(rows.size(), size_t(3 + 60000));
    CHECK_EQ(rows[2].first, QStringLiteral("@@ -1,30000 +1,30000 @@"));
}

// --- ls-Parser -------------------------------------------------------------

TEST(fix_core, lsparse_keeps_leading_spaces_in_names)
{
    const QString text = QStringLiteral(
        "-rw-r--r-- 1 0 0 1 2024-01-02 13:45 foo\n"
        "-rw-r--r-- 1 0 0 2 2024-01-02 13:45  foo\n"
        "-rw-r--r-- 1 0 0 3 2024-01-02 13:45   two  spaces \n"
        "-rw-r--r-- 1 0 0 4 2024-01-02 13:45  \n");
    QHash<QString, qint64> sizes;
    for (const core::FileEntry &e : core::parseLsLong(text))
        sizes.insert(e.name, e.size);
    CHECK_EQ(sizes.size(), qsizetype(4));
    CHECK_EQ(sizes.value(QStringLiteral("foo")), qint64(1));
    CHECK_EQ(sizes.value(QStringLiteral(" foo")), qint64(2));
    CHECK_EQ(sizes.value(QStringLiteral("  two  spaces ")), qint64(3));
    CHECK_EQ(sizes.value(QStringLiteral(" ")), qint64(4));
}

TEST(fix_core, lsparse_times_are_utc)
{
    const auto entries = core::parseLsLong(
        QStringLiteral("-rw-r--r-- 1 0 0 1 2024-07-02 13:45 f\n"));
    CHECK_EQ(entries.size(), size_t(1));
    if (entries.empty())
        return;
    const QDateTime expected(QDate(2024, 7, 2), QTime(13, 45), QTimeZone::utc());
    CHECK(entries.front().modified.isValid());
    CHECK_EQ(entries.front().modified.toSecsSinceEpoch(), expected.toSecsSinceEpoch());
}

// --- Verzeichnis-Walks und Junctions ---------------------------------------

TEST(fix_core, dir_walks_do_not_follow_junctions)
{
#ifdef Q_OS_WIN
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    const QString root = tmp.filePath(QStringLiteral("root"));
    const QString real = root + QStringLiteral("/real");
    const QString outside = tmp.filePath(QStringLiteral("outside"));
    QDir().mkpath(real);
    QDir().mkpath(outside);
    writeFile(real + QStringLiteral("/f.txt"), QByteArray(10, 'x'));
    writeFile(outside + QStringLiteral("/big.bin"), QByteArray(1000, 'y'));
    // Junction aus dem Baum heraus und eine auf einen Vorfahren (Zyklus).
    CHECK(makeJunction(root + QStringLiteral("/j"), outside));
    CHECK(makeJunction(real + QStringLiteral("/loop"), root));

    const auto [size, truncated] = core::dirSize(root);
    CHECK_EQ(size, qint64(10));
    CHECK(!truncated);

    const core::DirStats st = core::dirStats(root);
    CHECK_EQ(st.files, qint64(1));
    CHECK_EQ(st.size, qint64(10));
    CHECK(!st.truncated);

    core::SearchOptions opts;
    opts.mode = QStringLiteral("name");
    opts.pattern = QStringLiteral("*");
    QStringList hits;
    core::iterSearch(root, opts, [&hits](const QString &h) { hits << h; });
    for (const QString &h : hits) {
        CHECK(!h.contains(QStringLiteral("big.bin")));
        CHECK(!h.contains(QStringLiteral("loop\\real")));
        CHECK(!h.contains(QStringLiteral("loop/real")));
    }
    // real, f.txt, loop, j — die Links selbst erscheinen, ihr Inhalt nicht.
    CHECK_EQ(hits.size(), qsizetype(4));

    // Junctions vor dem Aufraeumen entfernen (rmdir loescht nur den Link):
    // removeRecursively des QTemporaryDir koennte dem Zyklus sonst folgen.
    QDir().rmdir(real + QStringLiteral("/loop"));
    QDir().rmdir(root + QStringLiteral("/j"));
#endif
}

// --- sudo-Provider ---------------------------------------------------------

TEST(fix_core, sudo_isdir_refreshes_instead_of_reporting_file)
{
    // Abgelaufener sudo-Timestamp: frueher endete "sudo -n test -d X || echo F"
    // mit Exit 0 und "F" — ein Ordner galt als Datei, ohne Auffrischen.
    FixPosixPaths paths;
    QStringList commands;
    int calls = 0;
    net::SudoFileSystem fs(
        &paths,
        [&](const QString &cmd, const QByteArray &) {
            commands << cmd;
            net::ExecResult r;
            if (cmd.contains(QStringLiteral("test -d")) && calls++ == 0) {
                r.exitStatus = 1;
                r.err = QByteArrayLiteral("sudo: a password is required");
                return r;
            }
            r.exitStatus = 0;
            if (cmd.contains(QStringLiteral("test -d")))
                r.out = QByteArrayLiteral("D\n");
            return r;
        },
        [] { return QStringLiteral("pw"); });
    CHECK(fs.isDir(QStringLiteral("/etc")));
    bool refreshed = false;
    for (const QString &c : commands)
        refreshed = refreshed || c.contains(QStringLiteral(" -v"));
    CHECK(refreshed);
}

TEST(fix_core, sudo_listing_uses_utc_and_literal_names)
{
    FixPosixPaths paths;
    QString listCmd;
    net::SudoFileSystem fs(
        &paths,
        [&](const QString &cmd, const QByteArray &) {
            net::ExecResult r;
            r.exitStatus = 0;
            if (cmd.contains(QStringLiteral(" ls "))) {
                listCmd = cmd;
                r.out = QByteArrayLiteral("-rw-r--r-- 1 0 0 1 2024-01-02 13:45  lead\n");
            }
            return r;
        },
        [] { return QString(); });
    const auto entries = fs.listDir(QStringLiteral("/srv"));
    CHECK(listCmd.contains(QStringLiteral("TZ=UTC0")));
    bool found = false;
    for (const core::FileEntry &e : entries)
        found = found || e.name == QStringLiteral(" lead");
    CHECK(found);
}

// --- Massen-Umbenennen -----------------------------------------------------

TEST(fix_core, rename_plan_rolls_back_on_failure)
{
    // Tausch a<->b: a->tmp, b->a, tmp->b. Scheitert der letzte Schritt, darf
    // weder die Temp-Datei liegen bleiben noch "a" seinen Inhalt verlieren.
    QHash<QString, QString> dir{{QStringLiteral("a"), QStringLiteral("A")},
                                {QStringLiteral("b"), QStringLiteral("B")}};
    const auto steps = core::planSafeOrder(
        {{QStringLiteral("a"), QStringLiteral("b")}, {QStringLiteral("b"), QStringLiteral("a")}});
    CHECK_EQ(steps.size(), size_t(3));
    int n = 0;
    const auto rename = [&](const QString &from, const QString &to) {
        if (++n == 3)
            throw std::runtime_error("Zugriff verweigert");
        if (!dir.contains(from) || dir.contains(to))
            throw std::runtime_error("ungueltiger Schritt");
        dir.insert(to, dir.take(from));
    };
    CHECK_THROWS(core::applyRenamePlan(steps, rename));
    CHECK_EQ(dir.size(), qsizetype(2));
    CHECK_EQ(dir.value(QStringLiteral("a")), QStringLiteral("A"));
    CHECK_EQ(dir.value(QStringLiteral("b")), QStringLiteral("B"));
}
