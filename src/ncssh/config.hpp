// Plattformabhaengige Pfade fuer persistente App-Daten.  (Port von config.py)
#pragma once

#include <QString>

namespace ncssh {

// Verzeichnis fuer Konfiguration/Profile, plattformkonform.
//   Windows : %APPDATA%\ncssh
//   macOS   : ~/Library/Application Support/ncssh
//   Linux   : $XDG_CONFIG_HOME/ncssh  (Fallback ~/.config/ncssh)
QString configDir();

// Schreibt atomar: erst in eine Temp-Datei, dann ersetzen. Ein Absturz mitten
// im Schreiben kann die Zieldatei so nicht mehr zerstoeren.
void atomicWriteText(const QString &path, const QString &text);

QString profilesFile();
QString historyFile();
QString bookmarksFile();
QString hostKeysFile();
QString tabFavoritesFile();

} // namespace ncssh
