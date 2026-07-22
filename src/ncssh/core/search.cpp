#include "ncssh/core/search.hpp"

#include <QRegularExpression>
#include <QStringList>

namespace ncssh::core {

QString shellQuote(const QString &s)
{
    if (s.isEmpty())
        return QStringLiteral("''");
    static const QRegularExpression unsafe(QStringLiteral("[^\\w@%+=:,./-]"));
    if (!s.contains(unsafe))
        return s;
    QString q = s;
    q.replace(QLatin1Char('\''), QLatin1String("'\"'\"'"));
    return QLatin1Char('\'') + q + QLatin1Char('\'');
}

// Kommagetrennte Liste -> bereinigte Einzelwerte (leere verworfen).
static QStringList splitCsv(const QString &csv)
{
    QStringList out;
    for (const QString &p : csv.split(QLatin1Char(','))) {
        const QString t = p.trimmed();
        if (!t.isEmpty())
            out.append(t);
    }
    return out;
}

// Entfernt Anfuehrungszeichen, damit ein Wert nicht aus dem cmd-Quoting
// ausbrechen kann.
static QString winq(const QString &s)
{
    QString t = s;
    t.remove(QLatin1Char('"'));
    return t;
}

QString nameSearchCmd(const QString &osType, const QString &root, const QString &pattern,
                      bool ignoreCase, int limit, const NameSearchOptions &opts)
{
    if (osType == QLatin1String("windows")) {
        const QString r = winq(root);
        const QString p = winq(pattern);
        if (opts.regex || opts.kind != QLatin1String("all")) {
            const QString attr = opts.kind == QLatin1String("file") ? QStringLiteral("/a-d ")
                                 : opts.kind == QLatin1String("dir") ? QStringLiteral("/ad ")
                                                                      : QString();
            if (opts.regex) {
                const QString ci = ignoreCase ? QStringLiteral("/i ") : QString();
                return QStringLiteral("dir /s /b %1\"%2\" 2>nul | findstr /r %3\"%4\"")
                    .arg(attr, r, ci, p);
            }
            return QStringLiteral("dir /s /b %1\"%2\\%3\" 2>nul").arg(attr, r, p);
        }
        return QStringLiteral("where /r \"%1\" \"%2\"").arg(r, p);
    }

    QStringList parts{QStringLiteral("find"), shellQuote(root)};
    if (opts.maxDepth)
        parts << QStringLiteral("-maxdepth") << QString::number(*opts.maxDepth);
    if (opts.kind == QLatin1String("file"))
        parts << QStringLiteral("-type") << QStringLiteral("f");
    else if (opts.kind == QLatin1String("dir"))
        parts << QStringLiteral("-type") << QStringLiteral("d");
    if (opts.regex) {
        parts << QStringLiteral("-regextype") << QStringLiteral("posix-extended");
        parts << (ignoreCase ? QStringLiteral("-iregex") : QStringLiteral("-regex"))
              << shellQuote(QStringLiteral(".*%1.*").arg(pattern));
    } else {
        parts << (ignoreCase ? QStringLiteral("-iname") : QStringLiteral("-name"))
              << shellQuote(pattern);
    }
    if (opts.minSize)
        parts << QStringLiteral("-size") << QStringLiteral("+%1c").arg(*opts.minSize);
    if (opts.maxSize)
        parts << QStringLiteral("-size") << QStringLiteral("-%1c").arg(*opts.maxSize);
    if (opts.newerThanDays)
        parts << QStringLiteral("-mtime")
              << QStringLiteral("-%1").arg(static_cast<int>(*opts.newerThanDays));
    return parts.join(QLatin1Char(' '))
           + QStringLiteral(" 2>/dev/null | head -n %1").arg(limit);
}

QString contentSearchCmd(const QString &osType, const QString &root, const QString &text,
                         const QString &include, bool ignoreCase, int limit,
                         const ContentSearchOptions &opts)
{
    if (osType == QLatin1String("windows")) {
        const QString r = winq(root);
        const QString t = winq(text);
        QStringList masks;
        for (const QString &m : splitCsv(include))
            masks << winq(m);
        if (masks.isEmpty())
            masks << QStringLiteral("*.*");
        QStringList parts{QStringLiteral("findstr"), QStringLiteral("/s")};
        parts << (opts.namesOnly ? QStringLiteral("/m") : QStringLiteral("/n"));
        if (ignoreCase)
            parts << QStringLiteral("/i");
        if (opts.invert)
            parts << QStringLiteral("/v");
        if (!opts.includeBinary)
            parts << QStringLiteral("/p");  // Dateien mit nicht druckbaren Zeichen ueberspringen
        if (opts.regex) {
            parts << QStringLiteral("/r");
            parts << QStringLiteral("\"%1\"").arg(t);
        } else {
            parts << QStringLiteral("/c:\"%1\"").arg(t);
        }
        for (const QString &m : masks)
            parts << QStringLiteral("\"%1\\%2\"").arg(r, m);
        return parts.join(QLatin1Char(' '));
    }

    QStringList parts{QStringLiteral("grep"), QStringLiteral("-rn")};
    parts << (opts.includeBinary ? QStringLiteral("-a") : QStringLiteral("-I"));
    if (ignoreCase)
        parts << QStringLiteral("-i");
    if (opts.wholeWord)
        parts << QStringLiteral("-w");
    if (opts.invert)
        parts << QStringLiteral("-v");
    if (opts.namesOnly)
        parts << QStringLiteral("-l");
    else if (opts.context > 0)
        parts << QStringLiteral("--context=%1").arg(opts.context);
    parts << (opts.regex ? QStringLiteral("-E") : QStringLiteral("-F"));
    for (const QString &inc : splitCsv(include))
        parts << QStringLiteral("--include=%1").arg(shellQuote(inc));
    for (const QString &ex : splitCsv(opts.exclude))
        parts << QStringLiteral("--exclude=%1").arg(shellQuote(ex));
    for (const QString &d : splitCsv(opts.excludeDir))
        parts << QStringLiteral("--exclude-dir=%1").arg(shellQuote(d));
    parts << QStringLiteral("-e") << shellQuote(text) << shellQuote(root);
    return parts.join(QLatin1Char(' '))
           + QStringLiteral(" 2>/dev/null | head -n %1").arg(limit);
}

QString stripMatchLocation(const QString &line)
{
    static const QRegularExpression re(QStringLiteral(":\\d+:.*$"));
    QString out = line;
    out.remove(re);
    return out.trimmed();
}

} // namespace ncssh::core
