// Import gespeicherter Sitzungen aus PuTTY und WinSCP (Windows) sowie
// ~/.ssh/config. Passwoerter werden nicht uebernommen; Key-Pfade und
// Verbindungsdaten werden importiert.  (Port von core/importers.py)
#pragma once

#include "ncssh/core/models.hpp"

#include <QString>
#include <vector>

namespace ncssh::core {

std::vector<ServerProfile> importPutty();
std::vector<ServerProfile> importWinscp();
std::vector<ServerProfile> importSshConfig();
std::vector<ServerProfile> importAll();

// Importiert Sitzungen aus einer exportierten Datei (.reg von PuTTY/WinSCP oder
// exportierte WinSCP.ini). Das Format wird am Inhalt erkannt.
std::vector<ServerProfile> importFromFile(const QString &path);

} // namespace ncssh::core
