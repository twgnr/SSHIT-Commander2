// Import gespeicherter Sitzungen aus PuTTY und WinSCP (Windows) sowie
// ~/.ssh/config. Passwoerter werden nicht uebernommen; Key-Pfade und
// Verbindungsdaten werden importiert.
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

// Umgebungsvariablen aus PuTTY ("NAME=wert,NAME2=wert2", \ maskiert) bzw.
// aus einer ssh_config-Zeile "SetEnv NAME=wert NAME2=\"a b\"" (nur die Argumente).
std::vector<EnvVar> parsePuttyEnvironment(const QString &raw);
std::vector<EnvVar> parseSshSetEnv(const QString &args);

} // namespace ncssh::core
