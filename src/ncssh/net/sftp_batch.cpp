#include "ncssh/net/sftp_batch.hpp"

#include "ncssh/net/transfer.hpp"

#include <QDir>
#include <QFileInfo>

namespace ncssh::net {

QStringList tokenizeBatchLine(const QString &line)
{
    QStringList out;
    QString cur;
    bool inQuote = false;
    bool have = false;  // ob cur ein (evtl. leeres) Token darstellt
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (c == QLatin1Char('"')) {
            inQuote = !inQuote;
            have = true;
        } else if (c.isSpace() && !inQuote) {
            if (have) {
                out << cur;
                cur.clear();
                have = false;
            }
        } else {
            cur += c;
            have = true;
        }
    }
    if (have)
        out << cur;
    return out;
}

// Loest einen Remote-Pfad relativ zum aktuellen Remote-Verzeichnis auf.
static QString resolveRemote(FileSystemProvider *remote, const QString &cwd, const QString &p)
{
    if (p.startsWith(QLatin1Char('/')))
        return QDir::cleanPath(p);
    return remote->join(cwd, p);
}

// Loest einen lokalen Pfad relativ zum aktuellen lokalen Verzeichnis auf.
static QString resolveLocal(const QString &cwd, const QString &p)
{
    if (QDir::isAbsolutePath(p))
        return QDir::cleanPath(p);
    return QDir::cleanPath(cwd + QLatin1Char('/') + p);
}

BatchResult runSftpBatch(const QString &script, FileSystemProvider *local,
                         FileSystemProvider *remote, const QString &startRemoteDir,
                         const QString &startLocalDir,
                         const std::function<void(const QString &)> &onLog, bool stopOnError,
                         const CancelTokenPtr &cancel)
{
    BatchResult res;
    QString rcwd = startRemoteDir.isEmpty() ? QStringLiteral(".") : startRemoteDir;
    QString lcwd = startLocalDir.isEmpty() ? QDir::homePath() : startLocalDir;

    const auto logLine = [&](const QString &line) {
        res.log << line;
        if (onLog)
            onLog(line);
    };

    const QStringList lines = script.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        if (cancel && cancel->isCancelled()) {
            res.aborted = true;
            logLine(QStringLiteral("⚠ abgebrochen"));
            break;
        }
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;

        const QStringList tok = tokenizeBatchLine(line);
        if (tok.isEmpty())
            continue;
        const QString cmd = tok[0].toLower();
        const auto argCount = tok.size() - 1;

        try {
            if (cmd == QLatin1String("echo")) {
                logLine(line.mid(4).trimmed());
            } else if (cmd == QLatin1String("cd")) {
                if (argCount < 1)
                    throw std::runtime_error("cd: Pfad fehlt");
                rcwd = resolveRemote(remote, rcwd, tok[1]);
                logLine(QStringLiteral("✓ cd %1").arg(rcwd));
            } else if (cmd == QLatin1String("lcd")) {
                if (argCount < 1)
                    throw std::runtime_error("lcd: Pfad fehlt");
                lcwd = resolveLocal(lcwd, tok[1]);
                logLine(QStringLiteral("✓ lcd %1").arg(lcwd));
            } else if (cmd == QLatin1String("pwd")) {
                logLine(QStringLiteral("✓ pwd %1").arg(rcwd));
            } else if (cmd == QLatin1String("lpwd")) {
                logLine(QStringLiteral("✓ lpwd %1").arg(lcwd));
            } else if (cmd == QLatin1String("mkdir")) {
                if (argCount < 1)
                    throw std::runtime_error("mkdir: Pfad fehlt");
                const QString path = resolveRemote(remote, rcwd, tok[1]);
                remote->mkdir(path);
                logLine(QStringLiteral("✓ mkdir %1").arg(path));
            } else if (cmd == QLatin1String("rm")) {
                if (argCount < 1)
                    throw std::runtime_error("rm: Pfad fehlt");
                const QString path = resolveRemote(remote, rcwd, tok[1]);
                remote->remove(path, false);
                logLine(QStringLiteral("✓ rm %1").arg(path));
            } else if (cmd == QLatin1String("rmdir")) {
                if (argCount < 1)
                    throw std::runtime_error("rmdir: Pfad fehlt");
                const QString path = resolveRemote(remote, rcwd, tok[1]);
                remote->remove(path, true);
                logLine(QStringLiteral("✓ rmdir %1").arg(path));
            } else if (cmd == QLatin1String("rename") || cmd == QLatin1String("mv")) {
                if (argCount < 2)
                    throw std::runtime_error("rename: alt und neu noetig");
                const QString a = resolveRemote(remote, rcwd, tok[1]);
                const QString b = resolveRemote(remote, rcwd, tok[2]);
                remote->rename(a, b);
                logLine(QStringLiteral("✓ rename %1 → %2").arg(a, b));
            } else if (cmd == QLatin1String("chmod")) {
                if (argCount < 2)
                    throw std::runtime_error("chmod: Modus und Pfad noetig");
                bool okNum = false;
                const uint mode = tok[1].toUInt(&okNum, 8);
                if (!okNum)
                    throw std::runtime_error("chmod: ungueltiger Oktal-Modus");
                const QString path = resolveRemote(remote, rcwd, tok[2]);
                remote->chmod(path, mode);
                logLine(QStringLiteral("✓ chmod %1 %2").arg(tok[1], path));
            } else if (cmd == QLatin1String("ln")) {
                if (argCount < 2)
                    throw std::runtime_error("ln: ziel und link noetig");
                const QString link = resolveRemote(remote, rcwd, tok[2]);
                remote->symlink(tok[1], link);
                logLine(QStringLiteral("✓ ln %1 → %2").arg(link, tok[1]));
            } else if (cmd == QLatin1String("put")) {
                if (argCount < 1)
                    throw std::runtime_error("put: lokale Quelle fehlt");
                const QString lp = resolveLocal(lcwd, tok[1]);
                const QString base = QFileInfo(lp).fileName();
                const QString rp = (argCount >= 2) ? resolveRemote(remote, rcwd, tok[2])
                                                   : remote->join(rcwd, base);
                transfer(local, lp, remote, rp);
                logLine(QStringLiteral("✓ put %1 → %2").arg(lp, rp));
            } else if (cmd == QLatin1String("get")) {
                if (argCount < 1)
                    throw std::runtime_error("get: entfernte Quelle fehlt");
                const QString rp = resolveRemote(remote, rcwd, tok[1]);
                const QString base = remote->basename(rp);
                const QString lp = (argCount >= 2) ? resolveLocal(lcwd, tok[2])
                                                   : resolveLocal(lcwd, base);
                transfer(remote, rp, local, lp);
                logLine(QStringLiteral("✓ get %1 → %2").arg(rp, lp));
            } else {
                throw std::runtime_error(("unbekannter Befehl: " + cmd).toStdString());
            }
            ++res.ok;
        } catch (const std::exception &exc) {
            ++res.failed;
            logLine(QStringLiteral("✗ %1 — %2").arg(line, QString::fromUtf8(exc.what())));
            if (stopOnError) {
                res.aborted = true;
                break;
            }
        }
    }
    return res;
}

} // namespace ncssh::net
