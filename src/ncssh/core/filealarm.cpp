#include "ncssh/core/filealarm.hpp"

#include "ncssh/core/i18n.hpp"
#include "ncssh/core/settings.hpp"

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
    return a;
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

Snapshot scanDir(const QString &path, bool recursive, bool includeDirs, int limit)
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
