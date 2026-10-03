// Parser fuer "ls -lnA --time-style=long-iso" (rein, ohne I/O — voll testbar).
//
// Wird vom sudo-Dateisystem (net/sudofs) genutzt, um ein Verzeichnislisting aus
// der Ausgabe von "sudo ls" in FileEntry-Objekte zu uebersetzen — also genau
// das, was sonst SFTP readdir liefert. "-n" gibt numerische UID/GID (stabil
// parsbar), "-A" listet versteckte Dateien ohne "."/"..".
#pragma once

#include <QString>
#include <vector>

#include "ncssh/core/models.hpp"

namespace ncssh::core {

// "ls -lnA --time-style=long-iso"-Ausgabe -> [FileEntry] (ohne "..").
// Die Zeiten muessen in UTC vorliegen (ls mit TZ=UTC0 aufrufen); der Name ist
// alles nach dem EINEN Leerzeichen hinter der Uhrzeit (fuehrende Leerzeichen
// bleiben erhalten).
std::vector<FileEntry> parseLsLong(const QString &text);

// Rechte-String ("drwxr-xr-x") -> st_mode inkl. Typ-Bits und setuid/setgid/
// sticky. Oeffentlich, weil der Parser-Test ihn direkt prueft.
quint32 modeFromPerms(const QString &perms);

} // namespace ncssh::core
