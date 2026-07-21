#include "ncssh/core/runner.hpp"

#include <QDir>
#include <QFileInfo>
#include <QProcess>

namespace ncssh::core {

void CommandRunner::runTerminal(const QString &command, const QString &cwd,
                                const LineCallback &onChunk, const CancelTokenPtr &cancel,
                                int /*cols*/, int /*rows*/)
{
    stream(command, cwd, [&onChunk](const QString &line) {
        onChunk(line + QStringLiteral("\r\n"));
    }, cancel);
}

LocalCommandRunner::LocalCommandRunner()
{
    label = QStringLiteral("local");
}

// Beendet den GESAMTEN Prozessbaum. terminate() traefe nur die Shell
// (cmd.exe/sh); deren Kinder (z.B. ping) liefen verwaist weiter.
static void killTree(qint64 pid)
{
#ifdef Q_OS_WIN
    QProcess::execute(QStringLiteral("taskkill"),
                      {QStringLiteral("/PID"), QString::number(pid),
                       QStringLiteral("/T"), QStringLiteral("/F")});
#else
    QProcess::execute(QStringLiteral("kill"),
                      {QStringLiteral("-TERM"), QStringLiteral("-%1").arg(pid)});
#endif
}

void LocalCommandRunner::stream(const QString &command, const QString &cwd,
                                const LineCallback &onLine, const CancelTokenPtr &cancel)
{
    lastExitStatus.reset();
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    if (!cwd.isEmpty())
        proc.setWorkingDirectory(cwd);
#ifdef Q_OS_WIN
    proc.start(QStringLiteral("cmd.exe"), {QStringLiteral("/c"), command});
#else
    proc.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), command});
#endif
    if (!proc.waitForStarted(10000)) {
        lastExitStatus = -1;
        throw std::runtime_error("Prozessstart fehlgeschlagen");
    }

    QByteArray pending;
    const auto flushLines = [&] {
        int idx;
        while ((idx = pending.indexOf('\n')) >= 0) {
            QByteArray raw = pending.left(idx);
            pending.remove(0, idx + 1);
            while (raw.endsWith('\r'))
                raw.chop(1);
            onLine(QString::fromUtf8(raw));
        }
    };

    bool killed = false;
    while (proc.state() != QProcess::NotRunning) {
        if (cancel && cancel->isCancelled()) {
            // Bei Abbruch den Prozess beenden, statt ihn verwaist weiterlaufen
            // zu lassen.
            killTree(proc.processId());
            proc.waitForFinished(3000);
            killed = true;
            break;
        }
        if (proc.waitForReadyRead(100)) {
            pending += proc.readAll();
            flushLines();
        }
    }
    pending += proc.readAll();
    flushLines();
    if (!pending.isEmpty())
        onLine(QString::fromUtf8(pending));

    lastExitStatus = killed ? -1 : proc.exitCode();
}

std::optional<QString> LocalCommandRunner::resolveDir(const QString &cwd, const QString &target)
{
    QString t = target;
    if (t.startsWith(QLatin1Char('~')))
        t = QDir::homePath() + t.mid(1);
    QString candidate = QFileInfo(t).isAbsolute()
                            ? t
                            : cwd + QLatin1Char('/') + t;
    candidate = QDir::toNativeSeparators(QDir::cleanPath(candidate));
    if (QFileInfo(candidate).isDir())
        return candidate;
    return std::nullopt;
}

} // namespace ncssh::core
