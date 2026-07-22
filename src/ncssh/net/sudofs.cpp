#include "ncssh/net/sudofs.hpp"

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
    : m_sftpFs(sftpFs), m_session(std::move(session))
{
    isRemote = true;
    label = sftpFs->label + QStringLiteral(" (sudo)");
}

QByteArray SudoFileSystem::run(const QString &command, const QByteArray &stdinData)
{
    // Das Passwort teilt sich NIE einen stdin-Strom mit den Nutzdaten: erst per
    // separatem "sudo -v" den Timestamp auffrischen, dann "sudo -n <command>".
    if (m_session->sudoPassword && !m_session->sudoPassword->isEmpty()) {
        m_session->exec(QStringLiteral("sudo -S -p '' -v"),
                        (*m_session->sudoPassword + QLatin1Char('\n')).toUtf8());
    }
    const ExecResult r = m_session->exec(QStringLiteral("sudo -n ") + command, stdinData);
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
    const QByteArray out =
        run(QStringLiteral("ls -lnA --time-style=long-iso -- ") + shQuote(path));
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
    const QByteArray out =
        run(QStringLiteral("test -d ") + shQuote(path) + QStringLiteral(" && echo D || echo F"));
    return out.trimmed() == "D";
}

QByteArray SudoFileSystem::readBytes(const QString &path, qint64 maxBytes)
{
    return run(QStringLiteral("head -c %1 -- %2").arg(maxBytes).arg(shQuote(path)));
}

QString SudoFileSystem::readText(const QString &path, qint64 maxBytes)
{
    return QString::fromUtf8(readBytes(path, maxBytes));
}

qint64 SudoFileSystem::size(const QString &path)
{
    try {
        const QByteArray out = run(QStringLiteral("stat -c %s -- ") + shQuote(path));
        return QString::fromUtf8(out).trimmed().toLongLong();
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
    run(QStringLiteral("mv -- %1 %2").arg(shQuote(oldPath), shQuote(newPath)));
}

void SudoFileSystem::chmod(const QString &path, quint32 mode)
{
    run(QStringLiteral("chmod %1 -- %2")
            .arg(QString::number(mode & 07777, 8), shQuote(path)));
}

QString SudoFileSystem::join(const QString &path, const QString &name) const
{
    return m_sftpFs->join(path, name);
}

QString SudoFileSystem::parent(const QString &path) const
{
    return m_sftpFs->parent(path);
}

QString SudoFileSystem::basename(const QString &path) const
{
    return m_sftpFs->basename(path);
}

QString SudoFileSystem::home()
{
    return m_sftpFs->home();
}

} // namespace ncssh::net
