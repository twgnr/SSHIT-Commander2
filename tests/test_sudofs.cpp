// Tests fuer net/sudofs — sudo-Provider ohne echten Server.
//
// Eine Fake-Ausfuehrung zeichnet jeden (Befehl, stdin) auf und liefert eine
// vorgegebene Antwort. Wichtigster Punkt: das sudo-Passwort darf NIE im stdin
// des Nutzbefehls (z.B. tee) landen — sonst stuende es in der Zieldatei.
// (Port von tests/test_sudofs.py)
#include "tests/harness.hpp"

#include "ncssh/core/filesystem.hpp"
#include "ncssh/net/sudofs.hpp"

#include <QList>
#include <QPair>
#include <QString>

using namespace ncssh::core;
using namespace ncssh::net;

namespace {

struct Call {
    QString command;
    QByteArray stdinData;
};

// Zeichnet Aufrufe auf; Antworten ueber (Teilstring -> Ergebnis).
class FakeExec
{
public:
    QList<Call> calls;
    QList<QPair<QString, ExecResult>> replies;

    void reply(const QString &needle, const QByteArray &out, const QByteArray &err = {},
               int exitStatus = 0)
    {
        ExecResult r;
        r.out = out;
        r.err = err;
        r.exitStatus = exitStatus;
        replies.append({needle, r});
    }

    ExecResult operator()(const QString &command, const QByteArray &stdinData)
    {
        calls.append({command, stdinData});
        for (const auto &[needle, res] : replies) {
            if (command.contains(needle))
                return res;
        }
        ExecResult ok;
        ok.exitStatus = 0;
        return ok;
    }

    QList<Call> matching(const QString &needle) const
    {
        QList<Call> out;
        for (const Call &c : calls) {
            if (c.command.contains(needle))
                out.append(c);
        }
        return out;
    }
};

// Minimale Pfadsemantik (POSIX) — ersetzt das SFTPFileSystem im Test.
class PosixPaths : public FileSystemProvider
{
public:
    PosixPaths() { label = QStringLiteral("srv"); isRemote = true; }

    std::vector<FileEntry> listDir(const QString &) override { return {}; }
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
        QString base = a;
        while (base.endsWith(QLatin1Char('/')) && base.size() > 1)
            base.chop(1);
        return base + QLatin1Char('/') + b;
    }
    QString parent(const QString &p) const override
    {
        const int i = p.lastIndexOf(QLatin1Char('/'));
        return i <= 0 ? QStringLiteral("/") : p.left(i);
    }
    QString basename(const QString &p) const override
    {
        return p.mid(p.lastIndexOf(QLatin1Char('/')) + 1);
    }
    QString home() override { return QStringLiteral("/root"); }
};

// Baut einen SudoFileSystem ueber die Test-Naht.
SudoFileSystem makeFs(PosixPaths &paths, FakeExec &exec, const QString &password = {})
{
    return SudoFileSystem(
        &paths,
        [&exec](const QString &c, const QByteArray &in) { return exec(c, in); },
        [password]() { return password; });
}

} // namespace

TEST(sudofs, write_password_never_in_data_stream)
{
    // Passwort-Modus: tee bekommt NUR die Daten, das Passwort geht an "sudo -v".
    PosixPaths paths;
    FakeExec exec;
    SudoFileSystem fs = makeFs(paths, exec, QStringLiteral("s3cret"));
    fs.writeBytes(QStringLiteral("/etc/x.conf"), QByteArrayLiteral("payload-bytes"));

    // Auffrisch-Aufruf mit Passwort ...
    const auto refresh = exec.matching(QStringLiteral("-v"));
    CHECK(!refresh.isEmpty());
    if (refresh.isEmpty())
        return;
    CHECK_EQ(refresh.first().stdinData, QByteArrayLiteral("s3cret\n"));

    // ... und der tee-Aufruf mit -n, dessen stdin AUSSCHLIESSLICH die Nutzdaten sind.
    const auto tee = exec.matching(QStringLiteral("tee"));
    CHECK(!tee.isEmpty());
    if (tee.isEmpty())
        return;
    CHECK(tee.first().command.startsWith(QStringLiteral("sudo -n ")));
    CHECK_EQ(tee.first().stdinData, QByteArrayLiteral("payload-bytes"));
    CHECK(!tee.first().stdinData.contains("s3cret"));
}

TEST(sudofs, write_nopasswd_single_call)
{
    // NOPASSWD: kein -v-Aufruf, tee erhaelt die Daten direkt.
    PosixPaths paths;
    FakeExec exec;
    SudoFileSystem fs = makeFs(paths, exec);
    fs.writeBytes(QStringLiteral("/etc/y.conf"), QByteArrayLiteral("abc"));

    CHECK(exec.matching(QStringLiteral("-v")).isEmpty());
    const auto tee = exec.matching(QStringLiteral("tee"));
    CHECK(!tee.isEmpty());
    if (tee.isEmpty())
        return;
    CHECK(tee.first().command.startsWith(QStringLiteral("sudo -n ")));
    CHECK_EQ(tee.first().stdinData, QByteArrayLiteral("abc"));
}

TEST(sudofs, read_and_size_parse)
{
    PosixPaths paths;
    FakeExec exec;
    exec.reply(QStringLiteral("head"), QByteArrayLiteral("hello"));
    exec.reply(QStringLiteral("stat"), QByteArrayLiteral("42\n"));
    SudoFileSystem fs = makeFs(paths, exec);
    CHECK_EQ(fs.readBytes(QStringLiteral("/r")), QByteArrayLiteral("hello"));
    CHECK_EQ(fs.size(QStringLiteral("/r")), qint64(42));
}

TEST(sudofs, is_dir_true_false)
{
    PosixPaths paths;
    FakeExec yes;
    yes.reply(QStringLiteral("test -d"), QByteArrayLiteral("D\n"));
    CHECK_EQ(makeFs(paths, yes).isDir(QStringLiteral("/d")), true);

    FakeExec no;
    no.reply(QStringLiteral("test -d"), QByteArrayLiteral("F\n"));
    CHECK_EQ(makeFs(paths, no).isDir(QStringLiteral("/f")), false);
}

TEST(sudofs, nonzero_exit_raises)
{
    PosixPaths paths;
    FakeExec exec;
    exec.reply(QStringLiteral("ls "), {}, QByteArrayLiteral("Permission denied"), 1);
    SudoFileSystem fs = makeFs(paths, exec);
    bool threw = false;
    try {
        fs.listDir(QStringLiteral("/root"));
    } catch (const std::exception &e) {
        threw = true;
        CHECK(QString::fromUtf8(e.what()).contains(QStringLiteral("Permission denied")));
    }
    CHECK(threw);
}

TEST(sudofs, paths_are_quoted)
{
    PosixPaths paths;
    FakeExec exec;
    SudoFileSystem fs = makeFs(paths, exec);
    fs.remove(QStringLiteral("/tmp/a b; rm -rf /"), /*recursive=*/true);

    const auto rm = exec.matching(QStringLiteral("rm "));
    CHECK(!rm.isEmpty());
    if (rm.isEmpty())
        return;
    // shQuote schuetzt Leerzeichen und Sonderzeichen
    CHECK(rm.first().command.contains(QStringLiteral("'/tmp/a b; rm -rf /'")));
    CHECK(rm.first().command.startsWith(QStringLiteral("sudo -n rm")));
}

TEST(sudofs, single_quote_in_path_is_escaped)
{
    PosixPaths paths;
    FakeExec exec;
    SudoFileSystem fs = makeFs(paths, exec);
    fs.mkdir(QStringLiteral("/tmp/it's"));
    const auto mk = exec.matching(QStringLiteral("mkdir"));
    CHECK(!mk.isEmpty());
    if (mk.isEmpty())
        return;
    // ' wird zu '"'"' — der Ausbruch aus dem Quoting ist damit unmoeglich.
    CHECK(mk.first().command.endsWith(QStringLiteral("'/tmp/it'\"'\"'s'")));
}
