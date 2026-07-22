#include "ncssh/core/gitstatus.hpp"

#include <QDir>
#include <QFileInfo>
#include <QProcess>

namespace ncssh::core {

bool inGitRepo(const QString &directory)
{
    QString d = QDir(directory).absolutePath();
    while (true) {
        if (QFileInfo::exists(d + QStringLiteral("/.git")))
            return true;
        const QString parent = QFileInfo(d).path();
        if (parent == d)
            return false;
        d = parent;
    }
}

// Ein Buchstabe als Sammelstatus aus den beiden Porcelain-Spalten.
static QString badge(QChar x, QChar y)
{
    if (x == QLatin1Char('?') || y == QLatin1Char('?'))
        return QStringLiteral("?");
    static const char codes[] = {'U', 'A', 'D', 'R', 'C', 'M', 'T'};
    for (char c : codes) {
        if (x == QLatin1Char(c) || y == QLatin1Char(c))
            return QString(QLatin1Char(c));
    }
    return QStringLiteral("M");
}

QHash<QString, QString> parsePorcelain(const QString &text)
{
    QHash<QString, QString> out;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        if (line.length() < 4)
            continue;
        const QChar x = line.at(0);
        const QChar y = line.at(1);
        QString path = line.mid(3);
        if (path.contains(QLatin1String(" -> ")))  // umbenannt: "alt -> neu"
            path = path.section(QLatin1String(" -> "), -1);
        path = path.trimmed();
        while (path.startsWith(QLatin1Char('"'))) path.remove(0, 1);
        while (path.endsWith(QLatin1Char('"'))) path.chop(1);
        if (path.isEmpty())
            continue;
        const QString name = path.section(QLatin1Char('/'), 0, 0);
        const QString code = badge(x, y);
        const QString prev = out.value(name);
        out.insert(name, (!prev.isEmpty() && prev != code) ? QStringLiteral("M") : code);
    }
    return out;
}

QHash<QString, QString> gitStatus(const QString &directory, int timeoutMs)
{
    if (!inGitRepo(directory))
        return {};  // kein Repo -> gar keinen git-Prozess starten
    QProcess proc;
    proc.start(QStringLiteral("git"),
               {QStringLiteral("-C"), directory, QStringLiteral("status"),
                QStringLiteral("--porcelain"), QStringLiteral("--untracked-files=normal")});
    if (!proc.waitForStarted(2000))
        return {};
    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        return {};
    }
    if (proc.exitCode() != 0)
        return {};
    return parsePorcelain(QString::fromUtf8(proc.readAllStandardOutput()));
}

} // namespace ncssh::core
