// Benutzerfreundliches Datumsformat (Token wie DD.MM.YYYY HH24:MI) -> strftime.
// So muss der Nutzer keine %-Codes kennen. Standard ist das deutsche Format.
// (Port von core/dateformat.py)
#pragma once

#include <QDateTime>
#include <QString>

namespace ncssh::core {

inline constexpr const char *DEFAULT_DATE_FORMAT = "DD.MM.YYYY HH24:MI";

// Wandelt ein Token-Format in einen strftime-String um.
// Leeres Format -> Standardformat.
QString toStrftime(const QString &fmt);

// Formatiert dt gemaess Token-Format; ungueltiges dt -> leerer String.
QString formatDt(const QDateTime &dt, const QString &fmt);

// Wendet einen (per toStrftime erzeugten) strftime-String auf dt an.
// Eigener Interpreter statt std::strftime, damit unbekannte %-Codes gefahrlos
// unveraendert durchgereicht werden (MSVC wuerde sonst abbrechen).
QString applyStrftime(const QDateTime &dt, const QString &strftimeFmt);

} // namespace ncssh::core
