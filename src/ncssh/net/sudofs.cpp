#include "ncssh/net/sudofs.hpp"
#include "ncssh/core/encodings.hpp"

#include "ncssh/core/lsparse.hpp"

#include <QDir>
#include <algorithm>
#include <stdexcept>

namespace ncssh::net {

using core::EntryType;
using core::FileEntry;

static QString shQuote(const QString &s)
{
    if (s.isEmpty())
        return QStringLiteral("''");
    QString q = s;
    q.replace(QLatin1Char('\''), QLatin1String("'\"'\"'"));
    return QLatin1Char('\'') + q + QLatin1Char('\'');
}

bool sudoNeedsPassword(const SSHSessionPtr &session)
{
    try {
        return session->exec(QStringLiteral("sudo -n true")).exitStatus != 0;
    } catch (...) {
        return true;
    }
}

bool verifySudoPassword(const SSHSessionPtr &session, const QString &password)
{
    try {
        const ExecResult r = session->exec(QStringLiteral("sudo -S -p '' true"),
                                           (password + QLatin1Char('\n')).toUtf8());
        return r.exitStatus == 0;
    } catch (...) {
        return false;
    }
}

SudoFileSystem::SudoFileSystem(SFTPFileSystem *sftpFs, SSHSessionPtr session)
    : m_pathFs(sftpFs), m_session(std::move(session))
{
    isRemote = true;
    label = sftpFs->label + QStringLiteral(" (sudo)");
    SSHSessionPtr s = m_session;
    m_exec = [s](const QString &command, const QByteArray &stdinData) {
        return s->exec(command, stdinData);
    };
    m_password = [s]() { return s->sudoPassword.value_or(QString()); };
}

SudoFileSystem::SudoFileSystem(core::FileSystemProvider *pathFs, ExecFn exec,
                               PasswordFn password)
    : m_pathFs(pathFs), m_exec(std::move(exec)), m_password(std::move(password))
{
    isRemote = true;
    label = pathFs->label + QStringLiteral(" (sudo)");
}

QByteArray SudoFileSystem::run(const QString &command, const QByteArray &stdinData)
{
    // Optimistisch ohne Auffrischen starten: nach verifySudoPassword (beim
    // Einschalten) bzw. dem letzten Refresh ist der sudo-Timestamp minutenlang
    // gueltig. Das fruehere "sudo -v" vor JEDEM Befehl kostete einen ganzen
    // Exec-Kanal (mehrere Netz-Roundtrips) pro Operation.
    ExecResult r = m_exec(QStringLiteral("sudo -n ") + command, stdinData);
    if (r.exitStatus != 0
        && QString::fromUtf8(r.err).trimmed().startsWith(QLatin1String("sudo:"))) {
        // sudo selbst hat abgelehnt (Timestamp abgelaufen) — der Befehl lief
        // nie an, Wiederholen ist gefahrlos. Das Passwort teilt sich dabei NIE
        // einen stdin-Strom mit den Nutzdaten: erst per separatem "sudo -v"
        // auffrischen, dann erneut "sudo -n <command>".
        const QString password = m_password ? m_password() : QString();
        if (!password.isEmpty()) {
            m_exec(QStringLiteral("sudo -S -p '' -v"), (password + QLatin1Char('\n')).toUtf8());
            r = m_exec(QStringLiteral("sudo -n ") + command, stdinData);
        }
    }
    if (r.exitStatus != 0) {
        const QString err = QString::fromUtf8(r.err).trimmed();
        throw std::runtime_error(
            (err.isEmpty() ? QStringLiteral("sudo: Exit %1").arg(r.exitStatus) : err).toStdString());
    }
    return r.out;
}

std::vector<FileEntry> SudoFileSystem::listDir(const QString &path)
{
    std::vector<FileEntry> entries;
    if (parent(path) != path) {
        FileEntry up;
        up.name = QStringLiteral("..");
        up.type = EntryType::Parent;
        entries.push_back(up);
    }
    // "env TZ=UTC0": Zeiten in UTC, damit parseLsLong sie unabhaengig von der
    // Server-Zeitzone korrekt einordnet (sudo setzt die Umgebung zurueck, ein
    // direktes "sudo TZ=..." waere je nach sudoers verboten). Literal-Quoting,
    // damit Namen ohne Anfuehrungszeichen/Escapes erscheinen.
    const QByteArray out =
        run(QStringLiteral("env TZ=UTC0 ls -lnA --time-style=long-iso --quoting-style=literal -- ")
            + shQuote(path));
    std::vector<FileEntry> items = core::parseLsLong(QString::fromUtf8(out));
    std::sort(items.begin(), items.end(), [](const FileEntry &a, const FileEntry &b) {
        if (a.isDir() != b.isDir()) return a.isDir();
        return a.name.toLower() < b.name.toLower();
    });
    entries.insert(entries.end(), items.begin(), items.end());
    return entries;
}

bool SudoFileSystem::isDir(const QString &path)
{
    // Frueher "sudo -n test -d X && echo D || echo F": das "|| echo F" lief
    // AUSSERHALB von sudo — lehnte sudo ab (Timestamp abgelaufen), endete der
    // Befehl trotzdem mit Exit 0 und "F", run() frischte nie auf und ein
    // Ordner galt still als Datei. Jetzt laeuft alles unter sudo in einer
    // Shell; der Pfad geht als $1 hinein (kein Quoting im Skript noetig).
    const QByteArray out =
        run(QStringLiteral("sh -c 'test -d \"$1\" && echo D || echo F' sh ") + shQuote(path));
    return out.trimmed() == "D";
}

QByteArray SudoFileSystem::readBytes(const QString &path, qint64 maxBytes)
{
    // "head -c -N" hiesse bei GNU "alles AUSSER den letzten N Bytes" — ein
    // negativer Wert (= unbegrenzt) darf daher nie durchgereicht werden.
    if (maxBytes < 0)
        return run(QStringLiteral("cat -- ") + shQuote(path));
    return run(QStringLiteral("head -c %1 -- %2").arg(maxBytes).arg(shQuote(path)));
}

QString SudoFileSystem::readText(const QString &path, qint64 maxBytes)
{
    // Kodierung erkennen (UTF-8, UTF-16 mit/ohne BOM, ANSI) statt stur UTF-8.
    return core::decodeAuto(readBytes(path, maxBytes));
}

qint64 SudoFileSystem::size(const QString &path)
{
    try {
        const QByteArray out = run(QStringLiteral("stat -c %s -- ") + shQuote(path));
        bool ok = false;
        const qint64 n = QString::fromUtf8(out).trimmed().toLongLong(&ok);
        return (ok && n >= 0) ? n : 0;
    } catch (...) {
        return 0;
    }
}

void SudoFileSystem::writeBytes(const QString &path, const QByteArray &data)
{
    run(QStringLiteral("tee -- ") + shQuote(path) + QStringLiteral(" > /dev/null"), data);
}

void SudoFileSystem::writeText(const QString &path, const QString &content)
{
    writeBytes(path, content.toUtf8());
}

void SudoFileSystem::mkdir(const QString &path)
{
    run(QStringLiteral("mkdir -p -- ") + shQuote(path));
}

void SudoFileSystem::remove(const QString &path, bool recursive)
{
    run(QStringLiteral("rm %1 -- %2").arg(recursive ? QStringLiteral("-rf") : QStringLiteral("-f"),
                                          shQuote(path)));
}

void SudoFileSystem::rename(const QString &oldPath, const QString &newPath)
{
    // -T: ist newPath ein vorhandener Ordner, wuerde "mv a b" sonst nach b/a
    // verschieben statt umzubenennen (Datei landet an unerwarteter Stelle).
    run(QStringLiteral("mv -T -- %1 %2").arg(shQuote(oldPath), shQuote(newPath)));
}

void SudoFileSystem::chmod(const QString &path, quint32 mode)
{
    run(QStringLiteral("chmod %1 -- %2")
            .arg(QString::number(mode & 07777, 8), shQuote(path)));
}

QString SudoFileSystem::join(const QString &path, const QString &name) const
{
    return m_pathFs->join(path, name);
}

QString SudoFileSystem::parent(const QString &path) const
{
    return m_pathFs->parent(path);
}

QString SudoFileSystem::basename(const QString &path) const
{
    return m_pathFs->basename(path);
}

QString SudoFileSystem::home()
{
    return m_pathFs->home();
}

} // namespace ncssh::net
