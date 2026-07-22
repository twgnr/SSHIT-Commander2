// Ermittelt unter Windows die fuer eine Dateiendung registrierten Programme
// ("Oeffnen mit") aus der Registry. Auf anderen Plattformen leer.
//
// Liefert [(Anzeigename, exe_pfad)] — das erste Element ist (falls bekannt)
// das Standardprogramm der Endung.  (Port von core/openwith.py)
#pragma once

#include <QString>
#include <utility>
#include <vector>

namespace ncssh::core {

std::vector<std::pair<QString, QString>> programsForExtension(const QString &ext);

} // namespace ncssh::core
