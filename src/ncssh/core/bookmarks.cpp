#include "ncssh/core/bookmarks.hpp"

#include "ncssh/config.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <stdexcept>

namespace ncssh::core {

namespace {

QString jsonToString(const QJsonValue &v)
{
    return v.isString() ? v.toString() : v.toVariant().toString();
}

} // namespace

BookmarkStore::BookmarkStore()
{
    load();
}

// --- Persistenz ------------------------------------------------------------

void BookmarkStore::load()
{
    const QString path = ncssh::bookmarksFile();
    QFile f(path);
    if (!f.exists())
        return;
    if (!f.open(QIODevice::ReadOnly))
        throw std::runtime_error(
            (QStringLiteral("Kann Datei nicht lesen: ") + path).toStdString());
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    m_data.clear();
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return;
    const QJsonObject raw = doc.object();
    for (auto it = raw.constBegin(); it != raw.constEnd(); ++it) {
        QStringList paths;
        for (const QJsonValue v : it.value().toArray())
            paths.append(jsonToString(v));
        m_data.insert(it.key(), paths);
    }
}

QJsonObject BookmarkStore::toJson() const
{
    QJsonObject data;
    for (auto it = m_data.constBegin(); it != m_data.constEnd(); ++it)
        data.insert(it.key(), QJsonArray::fromStringList(it.value()));
    return data;
}

void BookmarkStore::save() const
{
    ncssh::atomicWriteText(
        ncssh::bookmarksFile(),
        QString::fromUtf8(QJsonDocument(toJson()).toJson(QJsonDocument::Indented)));
}

// --- Zugriff ---------------------------------------------------------------

QStringList BookmarkStore::list(const QString &key) const
{
    return m_data.value(key);
}

bool BookmarkStore::contains(const QString &key, const QString &path) const
{
    return m_data.value(key).contains(path);
}

void BookmarkStore::add(const QString &key, const QString &path)
{
    QStringList &paths = m_data[key];
    if (!paths.contains(path)) {
        paths.append(path);
        paths.sort();
        save();
    }
}

void BookmarkStore::remove(const QString &key, const QString &path)
{
    const auto it = m_data.find(key);
    if (it == m_data.end())
        return;
    if (it->removeOne(path)) {
        if (it->isEmpty())
            m_data.erase(it);
        save();
    }
}

bool BookmarkStore::toggle(const QString &key, const QString &path)
{
    if (contains(key, path)) {
        remove(key, path);
        return false;
    }
    add(key, path);
    return true;
}

// --- Sync (Export/Import) ---------------------------------------------------

void BookmarkStore::exportTo(const QString &path) const
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        throw std::runtime_error(
            (QStringLiteral("Kann Datei nicht schreiben: ") + path).toStdString());
    f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
    if (f.error() != QFileDevice::NoError)
        throw std::runtime_error(
            (QStringLiteral("Schreiben fehlgeschlagen: ") + path).toStdString());
}

int BookmarkStore::importFrom(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        throw std::runtime_error(
            (QStringLiteral("Kann Datei nicht lesen: ") + path).toStdString());
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError)
        throw std::runtime_error(
            (QStringLiteral("Ungültiges JSON: ") + err.errorString()).toStdString());
    if (!doc.isObject())
        throw std::runtime_error(
            QStringLiteral("Keine gültige Lesezeichen-Datei.").toStdString());
    int added = 0;
    const QJsonObject data = doc.object();
    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        QStringList &existing = m_data[it.key()];
        for (const QJsonValue pv : it.value().toArray()) {
            const QString p = jsonToString(pv);
            if (!existing.contains(p)) {
                existing.append(p);
                ++added;
            }
        }
        existing.sort();
    }
    save();
    return added;
}

} // namespace ncssh::core
