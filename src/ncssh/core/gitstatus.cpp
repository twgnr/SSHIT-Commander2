#include "ncssh/core/gitstatus.hpp"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStringList>

#include <optional>

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

QHash<QString, QString> parsePorcelain(const QString &text, const QString &prefix)
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
#ifdef Q_OS_WIN
        // Der Prefix stammt aus dem (evtl. anders geschriebenen) Pfad.
        const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
        const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
        // Innerhalb eines neuen Ordners meldet git nur den Ordner selbst
        // ("?? neu/") — dann ist alles im angezeigten Verzeichnis neu.
        if (!prefix.isEmpty() && (x == QLatin1Char('?') || y == QLatin1Char('?'))
            && path.endsWith(QLatin1Char('/')) && prefix.startsWith(path, cs)) {
            out.insert(kGitAllEntries, QStringLiteral("?"));
            continue;
        }
        if (!prefix.isEmpty()) {
            if (!path.startsWith(prefix, cs))
                continue;   // liegt nicht im angezeigten Verzeichnis
            path.remove(0, prefix.length());
        }
        if (path.isEmpty())
            continue;
        const QString name = path.section(QLatin1Char('/'), 0, 0);
        const QString code = badge(x, y);
        const QString prev = out.value(name);
        out.insert(name, mergeGitBadges(prev, code));
    }
    return out;
}

// git im Verzeichnis ausfuehren; nullopt bei Start-/Zeit-/Exit-Fehler.
static std::optional<QString> runGit(const QString &directory, const QStringList &args,
                                     int timeoutMs)
{
    QProcess proc;
    // quotePath=false: Umlaute nicht als Oktal-Escapes ("\303\244") ausgeben.
    proc.start(QStringLiteral("git"),
               QStringList{QStringLiteral("-C"), directory, QStringLiteral("-c"),
                           QStringLiteral("core.quotePath=false")}
                   + args);
    if (!proc.waitForStarted(2000))
        return std::nullopt;
    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        proc.waitForFinished(1000);
        return std::nullopt;
    }
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
        return std::nullopt;
    return QString::fromUtf8(proc.readAllStandardOutput());
}

QHash<QString, QString> gitStatus(const QString &directory, int timeoutMs)
{
    if (!inGitRepo(directory))
        return {};  // kein Repo -> gar keinen git-Prozess starten
    // Lage des Verzeichnisses im Repo: Porcelain-Pfade sind wurzelrelativ.
    const auto prefix = runGit(directory, {QStringLiteral("rev-parse"),
                                           QStringLiteral("--show-prefix")}, timeoutMs);
    if (!prefix)
        return {};  // z. B. innerhalb von .git oder "dubious ownership"
    // Pathspec "." beschraenkt die Ausgabe auf das angezeigte Verzeichnis.
    const auto status = runGit(directory,
                               {QStringLiteral("status"), QStringLiteral("--porcelain"),
                                QStringLiteral("--untracked-files=normal"),
                                QStringLiteral("--"), QStringLiteral(".")},
                               timeoutMs);
    if (!status)
        return {};
    QString pre = *prefix;
    while (pre.endsWith(QLatin1Char('\n')) || pre.endsWith(QLatin1Char('\r')))
        pre.chop(1);
    QHash<QString, QString> result = parsePorcelain(*status, pre);
    if (result.contains(kGitAllEntries)) {
        // Das angezeigte Verzeichnis ist selbst neu: Dateien einzeln auflisten
        // (nur dieser Teilbaum). So bleiben darin ignorierte Dateien ungefaerbt;
        // klappt das nicht, gilt "alles neu".
        const auto all = runGit(directory,
                                {QStringLiteral("status"), QStringLiteral("--porcelain"),
                                 QStringLiteral("--untracked-files=all"),
                                 QStringLiteral("--"), QStringLiteral(".")},
                                timeoutMs);
        if (all) {
            QHash<QString, QString> detailed = parsePorcelain(*all, pre);
            detailed.remove(kGitAllEntries);
            return detailed;
        }
    }
    return result;
}

QString mergeGitBadges(const QString &a, const QString &b)
{
    if (a.isEmpty() || a == b)
        return b;
    if (b.isEmpty())
        return a;
    const auto isNew = [](const QString &c) {
        return c == QLatin1String("A") || c == QLatin1String("?");
    };
    return (isNew(a) && isNew(b)) ? QStringLiteral("A") : QStringLiteral("M");
}

QString aggregateBadge(const QHash<QString, QString> &status)
{
    QString out;
    for (const QString &code : status)
        out = mergeGitBadges(out, code);
    return out;
}

QString childTowards(const QString &directory, const QString &path)
{
    QString dir = QDir::cleanPath(QDir::fromNativeSeparators(directory));
    const QString target = QDir::cleanPath(QDir::fromNativeSeparators(path));
    if (!dir.endsWith(QLatin1Char('/')))
        dir += QLatin1Char('/');   // "C:/" bleibt, "C:/a" -> "C:/a/"
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    if (target.length() <= dir.length() || !target.startsWith(dir, cs))
        return {};
    return target.mid(dir.length()).section(QLatin1Char('/'), 0, 0);
}

QHash<QString, QString> repoAncestorMarks(const QString &directory, const QStringList &repoRoots,
                                          int timeoutMs)
{
    QHash<QString, QString> out;
    for (const QString &root : repoRoots) {
        const QString child = childTowards(directory, root);
        if (child.isEmpty())
            continue;   // Repo liegt nicht unterhalb des angezeigten Ordners
        const QString code = aggregateBadge(gitStatus(root, timeoutMs));
        if (code.isEmpty())
            continue;   // sauber -> nichts markieren
        out.insert(child, mergeGitBadges(out.value(child), code));
    }
    return out;
}

GitRepoInfo gitRepoInfo(const QString &directory, int timeoutMs)
{
    if (!inGitRepo(directory))
        return {};
    const auto top = runGit(directory, {QStringLiteral("rev-parse"),
                                        QStringLiteral("--show-toplevel")}, timeoutMs);
    if (!top)
        return {};
    GitRepoInfo info;
    info.root = QDir::toNativeSeparators(top->trimmed());
    // Ohne origin endet git config mit Exit 1 -> URL bleibt leer.
    if (const auto url = runGit(directory, {QStringLiteral("config"), QStringLiteral("--get"),
                                            QStringLiteral("remote.origin.url")}, timeoutMs))
        info.originUrl = url->trimmed();
    return info;
}

} // namespace ncssh::core
