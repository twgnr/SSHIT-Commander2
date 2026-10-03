#include "ncssh/core/runner.hpp"

#include <QDir>
#include <QFileInfo>
#include <QProcess>

namespace ncssh::core {

QString cmdNativeArguments(const QString &command, bool delayedExpansion)
{
    // /s: cmd entfernt genau das AEUSSERSTE Anfuehrungszeichenpaar und laesst
    // den Rest unangetastet. Ohne /s greift die "genau zwei Quotes"-Heuristik
    // und zerlegt z.B. "C:\Program Files\x.exe" "arg". Bewusst Verkettung
    // statt QString::arg — ein %1 im Befehl darf nicht ersetzt werden.
    QString args;
    if (delayedExpansion)
        args += QStringLiteral("/v:on ");
    args += QStringLiteral("/s /c \"");
    args += command;
    args += QLatin1Char('"');
    return args;
}

void setShellCommand(QProcess &proc, const QString &command, bool delayedExpansion)
{
#ifdef Q_OS_WIN
    proc.setProgram(QStringLiteral("cmd.exe"));
    proc.setArguments({});
    proc.setNativeArguments(cmdNativeArguments(command, delayedExpansion));
#else
    Q_UNUSED(delayedExpansion);
    proc.setProgram(QStringLiteral("/bin/sh"));
    proc.setArguments({QStringLiteral("-c"), command});
#endif
}

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
    // Befehl unveraendert an die Shell (siehe setShellCommand).
    setShellCommand(proc, command);
    proc.start();
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
