// Parser fuer "ls -lnA --time-style=long-iso" (rein, ohne I/O — voll testbar).
//
// Wird vom sudo-Dateisystem (net/sudofs) genutzt, um ein Verzeichnislisting aus
// der Ausgabe von "sudo ls" in FileEntry-Objekte zu uebersetzen — also genau
// das, was sonst SFTP readdir liefert. "-n" gibt numerische UID/GID (stabil
// parsbar), "-A" listet versteckte Dateien ohne "."/"..".
// (Port von core/lsparse.py)
#pragma once

#include <QString>
#include <vector>

#include "ncssh/core/models.hpp"

namespace ncssh::core {

// "ls -lnA --time-style=long-iso"-Ausgabe -> [FileEntry] (ohne "..").
std::vector<FileEntry> parseLsLong(const QString &text);

} // namespace ncssh::core
