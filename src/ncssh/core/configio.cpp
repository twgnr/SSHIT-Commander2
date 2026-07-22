#include "ncssh/core/configio.hpp"

#include "ncssh/config.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <stdexcept>

namespace ncssh::core {

static const QString kFormat = QStringLiteral("ncssh-config");

QStringList sectionOrder()
{
    return {QStringLiteral("settings"), QStringLiteral("servers"),
            QStringLiteral("bookmarks"), QStringLiteral("tab_favorites"),
            QStringLiteral("history")};
}

static QHash<QString, QString> sectionPaths()
{
    return {
        {QStringLiteral("settings"), ncssh::configDir() + QStringLiteral("/settings.json")},
        {QStringLiteral("servers"), ncssh::profilesFile()},
        {QStringLiteral("bookmarks"), ncssh::bookmarksFile()},
        {QStringLiteral("tab_favorites"), ncssh::tabFavoritesFile()},
        {QStringLiteral("history"), ncssh::historyFile()},
    };
}

QJsonObject buildBundle()
{
    QJsonObject files;
    const auto paths = sectionPaths();
    for (auto it = paths.begin(); it != paths.end(); ++it) {
        QFile f(it.value());
        QJsonValue value;  // null bei Fehler/fehlender Datei
        if (f.open(QIODevice::ReadOnly)) {
            QJsonParseError err{};
            const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
            if (err.error == QJsonParseError::NoError && !doc.isNull())
                value = doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array());
        }
        files.insert(it.key(), value);
    }
    return QJsonObject{
        {QStringLiteral("_format"), kFormat},
        {QStringLiteral("version"), 1},
        {QStringLiteral("files"), files},
    };
}

void writeExport(const QString &path)
{
    const QJsonDocument doc(buildBundle());
    ncssh::atomicWriteText(path, QString::fromUtf8(doc.toJson(QJsonDocument::Indented)));
}

QJsonObject readBundle(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        throw std::runtime_error(
            QStringLiteral("Datei nicht lesbar: %1").arg(path).toStdString());
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()
        || doc.object().value(QStringLiteral("_format")).toString() != kFormat)
        throw std::runtime_error("Keine gültige SSHIT-Commander-Konfigurationsdatei.");
    return doc.object();
}

QStringList availableSections(const QJsonObject &bundle)
{
    const QJsonObject files = bundle.value(QStringLiteral("files")).toObject();
    QStringList result;
    for (const QString &key : sectionOrder()) {
        if (files.contains(key) && !files.value(key).isNull())
            result.append(key);
    }
    return result;
}

QStringList applyBundle(const QJsonObject &bundle, const QStringList &sections)
{
    const QJsonObject files = bundle.value(QStringLiteral("files")).toObject();
    const auto paths = sectionPaths();
    QStringList applied;
    for (const QString &key : sections) {
        if (!paths.contains(key) || !files.contains(key) || files.value(key).isNull())
            continue;
        const QJsonValue v = files.value(key);
        const QJsonDocument doc = v.isArray() ? QJsonDocument(v.toArray())
                                              : QJsonDocument(v.toObject());
        ncssh::atomicWriteText(paths.value(key),
                               QString::fromUtf8(doc.toJson(QJsonDocument::Indented)));
        applied.append(key);
    }
    return applied;
}

} // namespace ncssh::core
