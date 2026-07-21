// Natuerliche Sortierung (z.B. "datei2" < "datei10").  (Port von core/natsort.py)
#pragma once

#include <QString>
#include <utility>
#include <vector>

namespace ncssh::core {

// Sortierschluessel, der Zahlen numerisch behandelt.
//
// Zahlen-/Text-Abschnitte werden als (0, Zahl) bzw. (1, Text) getypt, damit
// der Vergleich zwischen Zahl und Text wohldefiniert ist (Zahl vor Text).
// Zahlen werden als normalisierte Ziffernfolge (ohne fuehrende Nullen)
// gespeichert und nach Laenge + lexikografisch verglichen — dadurch gibt es
// wie bei Pythons unbegrenztem int keine Ueberlauf-Grenze.
struct NaturalKey {
    // first: 0 = Zahl (normalisierte Ziffernfolge), 1 = Text (kleingeschrieben)
    std::vector<std::pair<int, QString>> parts;

    bool operator==(const NaturalKey &other) const;
    bool operator<(const NaturalKey &other) const;
};

NaturalKey naturalKey(const QString &text);

// Bequemer Direktvergleich fuer Sortier-Lambdas.
bool naturalLess(const QString &a, const QString &b);

} // namespace ncssh::core
