// Persistente Befehlshistorie + Favoriten (JSON).
//
// Jede Konsole und der Verlaufsdialog haben eine eigene Instanz. Damit keine
// die Aenderungen der anderen mit einem veralteten Stand ueberschreibt, liest
// jede aendernde Operation die Datei unmittelbar vorher neu ein und speichert
// danach selbst (read-modify-write) — ein zusaetzliches save() ist unnoetig.
#pragma once

#include <QString>
#include <QStringList>

namespace ncssh::core {

class HistoryStore {
public:
    static constexpr int MAX = 1000;

    HistoryStore();

    // --- Persistenz --------------------------------------------------------
    void load();
    void save() const;

    // --- Historie ----------------------------------------------------------
    QStringList history() const { return m_history; }
    void add(const QString &command);
    void clearHistory();

    // --- Favoriten ---------------------------------------------------------
    QStringList favorites() const { return m_favorites; }
    void addFavorite(const QString &command);
    void removeFavorite(const QString &command);
    bool isFavorite(const QString &command) const;

private:
    // Aktuellen Dateistand vor einer Aenderung holen (Lesefehler -> Speicherstand).
    void refresh();

    QStringList m_history;
    QStringList m_favorites;
};

} // namespace ncssh::core
