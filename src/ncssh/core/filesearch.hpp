// Lokale Such-Engine in reinem C++ - grep-aehnlich, plattformunabhaengig.
//
// Wird fuer LOKALE Suchen genutzt (insb. Windows, wo findstr schwach ist) und
// liefert dieselben Treffer-Zeilen wie grep/findstr ("pfad:zeile:text" bzw. nur
// "pfad"), damit der bestehende Ergebnis-Dialog unveraendert funktioniert.
//
// Funktionen: Wildcards/Glob & Regex, Gross/klein, ganzes Wort, invertiert,
// Datei-Include/Exclude, Ordner-Ausschluss, Typ-Filter, Binaerdateien ein/aus,
// nur Dateinamen, Kontextzeilen, Max-Tiefe, Groessen- und Datumsfilter, Limit.
// Remote-Suchen laufen weiterhin ueber echte grep/find-Befehle (siehe search).
#pragma once

#include "ncssh/gui/bridge.hpp"  // CancelTokenPtr

#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>

namespace ncssh::core {

using ncssh::gui::CancelTokenPtr;

// Treffer werden zeilenweise via Callback gestreamt.
using HitCallback = std::function<void(const QString &)>;

struct SearchOptions {
    QString pattern;
    QString mode = QStringLiteral("content");   // "name" | "content"
    bool regex = false;
    bool ignoreCase = true;
    bool wholeWord = false;
    bool invert = false;                         // content: Zeilen OHNE Treffer
    QStringList include;                         // Datei-Globs
    QStringList exclude;                         // Datei-Globs (Ausschluss)
    QStringList excludeDir;                      // Ordnernamen (Ausschluss)
    QString kind = QStringLiteral("all");        // name-Modus: all | file | dir
    bool includeBinary = false;
    bool namesOnly = false;
    int context = 0;                             // Zeilen vor/nach Treffer (content)
    std::optional<int> maxDepth;                 // leer = unbegrenzt
    std::optional<qint64> minSize;               // Bytes
    std::optional<qint64> maxSize;               // Bytes
    std::optional<double> newerThanDays;
    int limit = 2000;
};

// Uebersetzt das Muster in eine kompilierte Regex (oder leer bei leerem Muster).
std::optional<QRegularExpression> buildRegex(const SearchOptions &opts);

// Hauptfunktion: liefert Treffer-Zeilen ("pfad" bzw. "pfad:zeile:text") ueber
// onHit. Bricht kooperativ ueber cancel ab.
void iterSearch(const QString &root, const SearchOptions &opts,
                const HitCallback &onHit, const CancelTokenPtr &cancel = {});

} // namespace ncssh::core
