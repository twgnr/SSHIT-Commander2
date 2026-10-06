// Erweiterter Pane-Filter und mehrstufige Sortierung (Filter-Dialog der Pane).
//
// Bewusst ohne GUI-Abhaengigkeit, damit Filter- und Sortierregeln direkt
// testbar sind. Die Pane kompiliert den Filter einmal je Anzeige
// (CompiledPaneFilter) und prueft dann jeden Eintrag.
#pragma once

#include "ncssh/core/models.hpp"

#include <QDateTime>
#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <vector>

namespace ncssh::core {

struct PaneFilter {
    enum class Kind { All, FilesOnly, DirsOnly };
    enum class DateMode { Off, Between, OlderThan, NewerThan };

    Kind kind = Kind::All;
    // Namensregeln; mehrere Eintraege jeweils mit ";" getrennt (ODER).
    QString names;        // Platzhalter * ?; ohne Platzhalter = "enthaelt"
    QString startsWith;
    QString endsWith;
    QString regex;
    bool caseSensitive = false;
    QString extensions;   // "txt, log; cpp" — ohne Punkt, nur Dateien

    DateMode dateMode = DateMode::Off;
    QString dateField = QStringLiteral("modified");   // modified | created
    QDateTime from;       // Between: beide Grenzen einschliesslich
    QDateTime to;
    int amount = 7;       // OlderThan/NewerThan: Menge …
    QString unit = QStringLiteral("days");   // … minutes|hours|days|weeks|months|years

    qint64 minSize = -1;  // Bytes, -1 = ohne Grenze (gilt nur fuer Dateien)
    qint64 maxSize = -1;

    // Namens- und Datumsregeln auch auf Ordner anwenden. Aus: Ordner bleiben
    // sichtbar, damit man trotz Filter weiter navigieren kann.
    bool applyToDirs = false;

    bool isActive() const;
    // Fehlertext (z. B. ungueltiger regulaerer Ausdruck) oder leer.
    QString validate() const;
    // Kurzbeschreibung der aktiven Regeln (Tooltip/Statuszeile).
    QString summary() const;

    bool operator==(const PaneFilter &other) const = default;
};

class CompiledPaneFilter {
public:
    explicit CompiledPaneFilter(const PaneFilter &filter,
                                const QDateTime &now = QDateTime::currentDateTime());
    // ".." passt immer.
    bool matches(const FileEntry &entry) const;

private:
    bool matchesName(const QString &name) const;
    bool matchesDate(const FileEntry &entry) const;

    PaneFilter m_filter;
    bool m_active = false;
    std::vector<QRegularExpression> m_namePatterns;
    QStringList m_starts;
    QStringList m_ends;
    QRegularExpression m_regex;
    QStringList m_extensions;   // klein geschrieben, ohne Punkt
    QDateTime m_from;           // effektiver Zeitraum
    QDateTime m_to;
};

// Eine Sortierstufe: Spalten-Kennung (name, size, modified, created, accessed,
// type, ext, perm, owner) und Richtung.
struct SortKey {
    QString column = QStringLiteral("name");
    bool ascending = true;
    bool operator==(const SortKey &other) const = default;
};

// Sortiert nach den Stufen der Reihe nach (bei Gleichstand entscheidet die
// naechste). ".." steht immer oben; dirsFirst stellt Ordner vor Dateien.
void sortEntries(std::vector<FileEntry> &entries, const QList<SortKey> &keys,
                 bool dirsFirst = true, bool natural = true);

} // namespace ncssh::core
