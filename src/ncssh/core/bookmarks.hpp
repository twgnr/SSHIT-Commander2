// Pfad-Lesezeichen, pro Server gruppiert (Key = Profilname bzw. "local").
#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace ncssh::core {

class BookmarkStore {
public:
    BookmarkStore();

    // --- Persistenz --------------------------------------------------------
    void load();
    void save() const;

    // --- Zugriff -----------------------------------------------------------
    QStringList list(const QString &key) const;
    bool contains(const QString &key, const QString &path) const;
    void add(const QString &key, const QString &path);
    void remove(const QString &key, const QString &path);

    // Fuegt hinzu/entfernt; gibt den neuen Zustand zurueck (true = vorhanden).
    bool toggle(const QString &key, const QString &path);

    // --- Sync (Export/Import) ----------------------------------------------
    void exportTo(const QString &path) const;

    // Mischt Lesezeichen aus einer Datei hinzu; liefert die Anzahl neuer Pfade.
    int importFrom(const QString &path);

private:
    QJsonObject toJson() const;

    QHash<QString, QStringList> m_data;
};

} // namespace ncssh::core
