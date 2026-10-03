#include "ncssh/core/filealarm.hpp"

#include "ncssh/core/i18n.hpp"
#include "ncssh/core/settings.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <algorithm>

namespace ncssh::core {

QJsonObject AlarmSpec::toJson() const
{
    return QJsonObject{
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), name},
        {QStringLiteral("path"), path},
        {QStringLiteral("on_created"), onCreated},
        {QStringLiteral("on_modified"), onModified},
        {QStringLiteral("on_deleted"), onDeleted},
        {QStringLiteral("recursive"), recursive},
        {QStringLiteral("include_dirs"), includeDirs},
        {QStringLiteral("enabled"), enabled},
        {QStringLiteral("remote"), remote},
        {QStringLiteral("profile"), profile},
        {QStringLiteral("include_glob"), includeGlob},
        {QStringLiteral("exclude_glob"), excludeGlob},
        {QStringLiteral("action_cmd"), actionCmd},
    };
}

AlarmSpec AlarmSpec::fromJson(const QJsonObject &d)
{
    AlarmSpec a;
    a.id = d.value(QStringLiteral("id")).toInt(0);
    a.name = d.value(QStringLiteral("name")).toString();
    a.path = d.value(QStringLiteral("path")).toString();
    a.onCreated = d.value(QStringLiteral("on_created")).toBool(true);
    a.onModified = d.value(QStringLiteral("on_modified")).toBool(true);
    a.onDeleted = d.value(QStringLiteral("on_deleted")).toBool(true);
    a.recursive = d.value(QStringLiteral("recursive")).toBool(false);
    a.includeDirs = d.value(QStringLiteral("include_dirs")).toBool(true);
    a.enabled = d.value(QStringLiteral("enabled")).toBool(true);
    a.remote = d.value(QStringLiteral("remote")).toBool(false);
    a.profile = d.value(QStringLiteral("profile")).toString();
    a.includeGlob = d.value(QStringLiteral("include_glob")).toString();
    a.excludeGlob = d.value(QStringLiteral("exclude_glob")).toString();
    a.actionCmd = d.value(QStringLiteral("action_cmd")).toString();
    return a;
}

bool matchesGlobFilter(const QString &name, const QString &includeGlob,
                       const QString &excludeGlob)
{
    const auto patterns = [](const QString &s) {
        QStringList out;
        for (const QString &p : s.split(QLatin1Char(';'), Qt::SkipEmptyParts))
            out << p.trimmed();
        return out;
    };
    const QStringList inc = patterns(includeGlob);
    const QStringList exc = patterns(excludeGlob);
    if (!inc.isEmpty() && !QDir::match(inc, name))
        return false;
    if (!exc.isEmpty() && QDir::match(exc, name))
        return false;
    return true;
}

QString AlarmSpec::eventsLabel() const
{
    QStringList parts;
    if (onCreated)
        parts << _t("Neu");
    if (onModified)
        parts << _t("Geändert");
    if (onDeleted)
        parts << _t("Gelöscht");
    return parts.isEmpty() ? _t("—") : parts.join(QStringLiteral(", "));
}

Snapshot scanDir(const QString &path, bool recursive, bool includeDirs,
                 const QString &includeGlob, const QString &excludeGlob, int limit)
{
    Snapshot out;

    const auto record = [&](const QFileInfo &fi) -> bool {
        SnapshotEntry e;
        e.isDir = fi.isDir();
        e.mtime = fi.lastModified().toSecsSinceEpoch();
        e.size = e.isDir ? 0 : fi.size();
        out.insert(fi.filePath(), e);
        return out.size() < limit;
    };

    QDirIterator it(path, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
                    recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        if (fi.isDir() && !includeDirs)
            continue;
        // Namensfilter: die Traversierung laeuft weiter (Unterordner werden
        // trotz Filter durchsucht), nur die Aufnahme in den Schnappschuss haengt
        // am Muster.
        if (!matchesGlobFilter(fi.fileName(), includeGlob, excludeGlob))
            continue;
        if (!record(fi))
            break;
    }
    return out;
}

std::vector<std::tuple<QString, QString, bool>> diffSnapshots(
    const Snapshot &oldSnap, const Snapshot &newSnap,
    bool onCreated, bool onModified, bool onDeleted)
{
    std::vector<std::tuple<QString, QString, bool>> events;
    if (onCreated) {
        for (auto it = newSnap.begin(); it != newSnap.end(); ++it) {
            if (!oldSnap.contains(it.key()))
                events.emplace_back(QStringLiteral("created"), it.key(), it.value().isDir);
        }
    }
    if (onDeleted) {
        for (auto it = oldSnap.begin(); it != oldSnap.end(); ++it) {
            if (!newSnap.contains(it.key()))
                events.emplace_back(QStringLiteral("deleted"), it.key(), it.value().isDir);
        }
    }
    if (onModified) {
        for (auto it = newSnap.begin(); it != newSnap.end(); ++it) {
            if (!oldSnap.contains(it.key()))
                continue;
            const SnapshotEntry &n = it.value();
            const SnapshotEntry &o = oldSnap.value(it.key());
            if (!n.isDir && (n.mtime != o.mtime || n.size != o.size))
                events.emplace_back(QStringLiteral("modified"), it.key(), n.isDir);
        }
    }
    std::sort(events.begin(), events.end());
    return events;
}

QString alarmShellCommand(const QString &actionCmd, int count)
{
    // Platzhalter -> Variablenverweis (nie der Wert selbst, siehe Header).
    const auto ref = [](const char *var) {
#ifdef Q_OS_WIN
        // Mit cmd /v:on: Ersetzung erst nach dem Parsen der Befehlszeile.
        return QStringLiteral("!%1!").arg(QLatin1String(var));
#else
        // In sh werden Parameterwerte nie erneut als Befehl ausgewertet.
        return QStringLiteral("\"${%1}\"").arg(QLatin1String(var));
#endif
    };
    QString cmd = actionCmd.trimmed();
#ifdef Q_OS_WIN
    // Mit /v:on bekaeme ein woertliches "!" der Vorlage ("echo Achtung! {path}")
    // eine Sonderbedeutung und verschluckte den eingesetzten Pfad; in
    // Anfuehrungszeichen wuerde zudem "^" entfernt. Beide deshalb ebenfalls
    // ueber Variablen einsetzen. Ausserhalb von Anfuehrungszeichen bleibt "^"
    // unangetastet: dort ist es das vom Nutzer gewollte Escape-Zeichen.
    {
        QString escaped;
        bool inQuotes = false;
        for (const QChar c : std::as_const(cmd)) {
            if (c == QLatin1Char('"'))
                inQuotes = !inQuotes;
            if (c == QLatin1Char('!'))
                escaped += QStringLiteral("!ALARM_EXCL!");
            else if (c == QLatin1Char('^') && inQuotes)
                escaped += QStringLiteral("!ALARM_CARET!");
            else
                escaped += c;
        }
        cmd = escaped;
    }
#endif
    cmd.replace(QStringLiteral("{path}"), ref("ALARM_PATH"));
    cmd.replace(QStringLiteral("{kind}"), ref("ALARM_KIND"));
    cmd.replace(QStringLiteral("{name}"), ref("ALARM_NAME"));
    cmd.replace(QStringLiteral("{count}"), QString::number(count));
    return cmd;
}

QHash<QString, QString> alarmEnvironment(const QString &kind, const QString &path,
                                         const QString &name, int count)
{
    return {
        {QStringLiteral("ALARM_PATH"), path},
        {QStringLiteral("ALARM_KIND"), kind},
        {QStringLiteral("ALARM_NAME"), name},
        {QStringLiteral("ALARM_COUNT"), QString::number(count)},
        // Woertliche Sonderzeichen der Vorlage (siehe alarmShellCommand).
        {QStringLiteral("ALARM_EXCL"), QStringLiteral("!")},
        {QStringLiteral("ALARM_CARET"), QStringLiteral("^")},
    };
}

std::vector<AlarmSpec> loadAlarms()
{
    std::vector<AlarmSpec> out;
    const QVariantList list = getSetting(QStringLiteral("file_alarms")).toList();
    for (const QVariant &v : list)
        out.push_back(AlarmSpec::fromJson(QJsonObject::fromVariantMap(v.toMap())));
    return out;
}

void saveAlarms(const std::vector<AlarmSpec> &alarms)
{
    QJsonArray arr;
    for (const auto &a : alarms)
        arr.append(a.toJson());
    setSetting(QStringLiteral("file_alarms"), arr);
}

} // namespace ncssh::core
