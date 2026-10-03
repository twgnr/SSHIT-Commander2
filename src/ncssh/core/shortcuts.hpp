// Konfigurierbare Befehls-Tastenkuerzel.
//
// Jede befehlbare Aktion hat eine stabile ID, ein Anzeige-Label und ein
// Standard-Kuerzel. Abweichungen vom Standard werden als "shortcuts"-Objekt in
// settings.json abgelegt (nur die geaenderten Eintraege), sodass neue/
// angepasste Defaults bei einem Update automatisch greifen.
//
// Reine Navigations-/Editor-Tasten (Tab, Backspace, Alt+Pfeile, Space, F2 ...)
// sind bewusst NICHT hier — sie bleiben fest verdrahtet.
#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <utility>
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

// Vergleichsform eines Kuerzels: "ctrl+p", "Ctrl+P" und " CTRL + p " ergeben
// dasselbe (QKeySequence-PortableText). Leer bleibt leer; Unparsebares wird
// klein geschrieben ohne Leerzeichen zurueckgegeben.
QString normalizeShortcut(const QString &key);

// Fest verdrahtete, nicht konfigurierbare Kuerzel des Hauptfensters
// (Kuerzel, Bezeichnung) — fuer die Dublettenpruefung der Einstellungen.
// Muss zu den setShortcut-Aufrufen in main_window.cpp passen.
std::vector<std::pair<QString, QString>> fixedShortcuts();

} // namespace ncssh::core
