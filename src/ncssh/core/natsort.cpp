// Natuerliche Sortierung.
#include "ncssh/core/natsort.hpp"

#include <algorithm>

namespace ncssh::core {

// Vergleich zweier Abschnitte: erst Typ (Zahl vor Text), dann Wert.
static bool chunkLess(const std::pair<int, QString> &a, const std::pair<int, QString> &b)
{
    if (a.first != b.first)
        return a.first < b.first;
    if (a.first == 0) {
        // Zahl: normalisierte Ziffernfolge — kuerzere Folge = kleinere Zahl.
        if (a.second.size() != b.second.size())
            return a.second.size() < b.second.size();
    }
    return a.second < b.second;
}

bool NaturalKey::operator==(const NaturalKey &other) const
{
    return parts == other.parts;
}

bool NaturalKey::operator<(const NaturalKey &other) const
{
    return std::lexicographical_compare(parts.begin(), parts.end(),
                                        other.parts.begin(), other.parts.end(), chunkLess);
}

NaturalKey naturalKey(const QString &text)
{
    // Entspricht re.split(r"(\d+)", text): Text- und Zahlabschnitte im Wechsel,
    // inklusive leerer Textstuecke am Anfang/Ende — die bleiben erhalten, damit
    // die Vergleichsreihenfolge stabil bleibt.
    NaturalKey key;
    const int n = text.size();
    int i = 0;
    for (;;) {
        // Textabschnitt (ggf. leer), kleingeschrieben.
        int start = i;
        while (i < n && !text.at(i).isDigit())
            ++i;
        key.parts.emplace_back(1, text.mid(start, i - start).toLower());
        if (i >= n)
            break;
        // Zahlabschnitt: Ziffern (auch Unicode-Ziffern wie bei Pythons \d)
        // in ASCII normalisieren und fuehrende Nullen entfernen.
        QString digits;
        while (i < n && text.at(i).isDigit())
            digits += QChar(ushort('0' + text.at(i++).digitValue()));
        int z = 0;
        while (z < digits.size() - 1 && digits.at(z) == QLatin1Char('0'))
            ++z;
        key.parts.emplace_back(0, digits.mid(z));
        if (i >= n) {
            key.parts.emplace_back(1, QString());
            break;
        }
    }
    return key;
}

bool naturalLess(const QString &a, const QString &b)
{
    return naturalKey(a) < naturalKey(b);
}

} // namespace ncssh::core
