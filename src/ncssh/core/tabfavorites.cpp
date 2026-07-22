#include "ncssh/core/tabfavorites.hpp"

#include "ncssh/config.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <algorithm>
#include <stdexcept>

namespace ncssh::core {

TabFavoritesStore::TabFavoritesStore()
{
    load();
}

void TabFavoritesStore::load()
{
    const QString path = ncssh::tabFavoritesFile();
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
        if (!it.value().isArray())
            continue;  // nur Listen-Werte uebernehmen
        std::vector<QJsonObject> tabs;
        for (const QJsonValue v : it.value().toArray())
            tabs.push_back(v.toObject());
        m_data.emplace_back(it.key(), std::move(tabs));
    }
}

void TabFavoritesStore::save() const
{
    QJsonObject data;
    for (const Entry &e : m_data) {
        QJsonArray arr;
        for (const QJsonObject &t : e.second)
            arr.append(t);
        data.insert(e.first, arr);
    }
    const QString path = ncssh::tabFavoritesFile();
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        throw std::runtime_error(
            (QStringLiteral("Kann Datei nicht schreiben: ") + path).toStdString());
    f.write(QJsonDocument(data).toJson(QJsonDocument::Indented));
    if (f.error() != QFileDevice::NoError)
        throw std::runtime_error(
            (QStringLiteral("Schreiben fehlgeschlagen: ") + path).toStdString());
}

// --- Zugriff ---------------------------------------------------------------

std::vector<TabFavoritesStore::Entry>::iterator
TabFavoritesStore::findEntry(const QString &name)
{
    return std::find_if(m_data.begin(), m_data.end(),
                        [&](const Entry &e) { return e.first == name; });
}

std::vector<TabFavoritesStore::Entry>::const_iterator
TabFavoritesStore::findEntry(const QString &name) const
{
    return std::find_if(m_data.cbegin(), m_data.cend(),
                        [&](const Entry &e) { return e.first == name; });
}

QStringList TabFavoritesStore::names() const
{
    QStringList out;
    for (const Entry &e : m_data)
        out.append(e.first);
    return out;
}

std::vector<QJsonObject> TabFavoritesStore::get(const QString &name) const
{
    const auto it = findEntry(name);
    return it != m_data.cend() ? it->second : std::vector<QJsonObject>{};
}

int TabFavoritesStore::count(const QString &name) const
{
    const auto it = findEntry(name);
    return it != m_data.cend() ? static_cast<int>(it->second.size()) : 0;
}

bool TabFavoritesStore::contains(const QString &name) const
{
    return findEntry(name) != m_data.cend();
}

void TabFavoritesStore::put(const QString &name, const std::vector<QJsonObject> &tabs)
{
    const auto it = findEntry(name);
    if (it != m_data.end())
        it->second = tabs;
    else
        m_data.emplace_back(name, tabs);
    save();
}

void TabFavoritesStore::remove(const QString &name)
{
    std::erase_if(m_data, [&](const Entry &e) { return e.first == name; });
    save();
}

bool TabFavoritesStore::rename(const QString &oldName, const QString &newName)
{
    if (!contains(oldName) || newName.isEmpty() || newName == oldName || contains(newName))
        return false;
    // Reihenfolge beibehalten
    findEntry(oldName)->first = newName;
    save();
    return true;
}

} // namespace ncssh::core
