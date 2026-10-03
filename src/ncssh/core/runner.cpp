#include "ncssh/core/runner.hpp"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStringDecoder>
#include <string>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

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

// Ausgabe lokaler Befehle dekodieren: UTF-8, wenn es gueltiges UTF-8 ist
// (PowerShell, git, Python …), sonst unter Windows die OEM-Codepage der
// Konsole — cmd-Befehle wie dir schreiben darin ("Datentraeger" kam sonst als
// "Datentr?ger" an).
static QString decodeLocalOutput(const QByteArray &raw)
{
#ifdef Q_OS_WIN
    QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    const QString text = utf8.decode(raw);
    if (!utf8.hasError())
        return text;
    const int n = MultiByteToWideChar(CP_OEMCP, 0, raw.constData(), int(raw.size()), nullptr, 0);
    std::wstring wide(size_t(n), L'\0');
    MultiByteToWideChar(CP_OEMCP, 0, raw.constData(), int(raw.size()), wide.data(), n);
    return QString::fromWCharArray(wide.data(), n);
#else
    return QString::fromUtf8(raw);
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
            onLine(decodeLocalOutput(raw));
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
        onLine(decodeLocalOutput(pending));

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
