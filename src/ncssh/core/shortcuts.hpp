// Konfigurierbare Befehls-Tastenkuerzel.
//
// Jede befehlbare Aktion hat eine stabile ID, ein Anzeige-Label und ein
// Standard-Kuerzel. Abweichungen vom Standard werden als "shortcuts"-Objekt in
// settings.json abgelegt (nur die geaenderten Eintraege), sodass neue/
// angepasste Defaults bei einem Update automatisch greifen.
//
// Reine Navigations-/Editor-Tasten (Tab, Backspace, Alt+Pfeile, Space, F2 ...)
// sind bewusst NICHT hier — sie bleiben fest verdrahtet.
// (Port von core/shortcuts.py)
#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <vector>

namespace ncssh::core {

struct ShortcutDef {
    QString id;
    QString group;   // uebersetzt (via _t)
    QString label;   // uebersetzt (via _t)
    QString key;     // Standard-Kuerzel
};

// Alle Definitionen (Reihenfolge = Anzeige).
const std::vector<ShortcutDef> &shortcutDefs();

// Reihenfolge der Gruppen fuer die Anzeige.
QStringList groupOrder();

// Die Standard-Kuerzel als id -> Kuerzel.
QHash<QString, QString> defaultShortcuts();

// Effektive Kuerzel: Defaults mit den gespeicherten Overrides ueberlagert.
QHash<QString, QString> getShortcuts();

// Speichert nur die vom Standard abweichenden Kuerzel.
void saveShortcuts(const QHash<QString, QString> &mapping);

QString labelFor(const QString &sid);

} // namespace ncssh::core
