// Tab-Favoriten: gespeicherte Tab-Layouts (pro Tab: Verbindung + Pane-Pfade).
// (Port von core/tabfavorites.py)
//
// Bewusst getrennt von den Pfad-Lesezeichen (core/bookmarks). Hier wird ein
// kompletter Satz Tabs als benannter Favorit abgelegt, um ihn spaeter wieder
// zu oeffnen. Persistiert als JSON: {name: [tab_spec, ...]}.
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <utility>
#include <vector>

namespace ncssh::core {

class TabFavoritesStore {
public:
    TabFavoritesStore();

    void load();
    void save() const;

    // --- Zugriff -----------------------------------------------------------
    QStringList names() const;
    std::vector<QJsonObject> get(const QString &name) const;
    int count(const QString &name) const;
    bool contains(const QString &name) const;

    // Legt einen Favorit an oder ueberschreibt ihn.
    void put(const QString &name, const std::vector<QJsonObject> &tabs);
    void remove(const QString &name);
    bool rename(const QString &oldName, const QString &newName);

private:
    // Eintraege in Einfuege-Reihenfolge (wie ein Python-Dict).
    using Entry = std::pair<QString, std::vector<QJsonObject>>;

    std::vector<Entry>::iterator findEntry(const QString &name);
    std::vector<Entry>::const_iterator findEntry(const QString &name) const;

    std::vector<Entry> m_data;
};

} // namespace ncssh::core
