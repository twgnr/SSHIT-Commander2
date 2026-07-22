// Im-/Export der Konfiguration als eine JSON-Datei.
//
// Buendelt die persistenten Dateien (Einstellungen inkl. Panes/Themes/Tools/
// GitHub, Server, Lesezeichen, Tab-Favoriten, Befehlsverlauf). Geheimnisse
// (Passwoerter, GitHub-Token) liegen im OS-Schluesselbund und werden bewusst
// NICHT exportiert. Host-Keys (TOFU) sind rechnerspezifisch und ebenfalls
// ausgenommen.  (Port von core/configio.py)
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace ncssh::core {

// Reihenfolge bestimmt die Anzeige; Schluessel -> Datei.
QStringList sectionOrder();

// Liest die vorhandenen Konfigurationsdateien in ein Buendel.
QJsonObject buildBundle();

void writeExport(const QString &path);

// Wirft std::runtime_error bei ungueltigem Format.
QJsonObject readBundle(const QString &path);

// Vorhandene (nicht-leere) Bereiche eines Buendels — in fester Reihenfolge.
QStringList availableSections(const QJsonObject &bundle);

// Schreibt die gewaehlten Bereiche zurueck; gibt die uebernommenen zurueck.
QStringList applyBundle(const QJsonObject &bundle, const QStringList &sections);

} // namespace ncssh::core
