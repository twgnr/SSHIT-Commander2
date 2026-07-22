// Externe Plugins (eigenstaendige Programme) verwalten und starten.
// (Port von core/plugins.py)
//
// Programme werden im Ordner "plugins/" im Hauptverzeichnis der Anwendung
// abgelegt (eigene Unterordner je Programm). In der Verwaltung wird die
// jeweilige ausfuehrbare Datei verlinkt, ein Anzeigename vergeben und optional:
//
// * Parameter (Vorlage mit Platzhalter "{path}" fuer das gewaehlte Element),
// * Arbeitsverzeichnis,
// * Kontextmenue (Datei/Ordner) — dann wird der Pfad des Elements uebergeben.
//
// Die Metadaten liegen in <config>/plugins.json. Gestartet wird — wie im
// Original via subprocess — als externer Prozess ueber QProcess.
//
// Namespace-Hinweis: Die Funktionsnamen des Python-Moduls (load, save, launch,
// ...) sind bewusst in einem eigenen Namespace ncssh::core::plugins gekapselt,
// damit die generischen Namen den flachen core-Namespace nicht verschmutzen —
// Aufrufe lesen sich wie im Original: plugins::load().
#pragma once

#include <QJsonObject>
#include <QString>
#include <utility>
#include <vector>

namespace ncssh::core::plugins {

// (Anzeigetext, Wert) fuer die Zielauswahl im Kontextmenue.
const std::vector<std::pair<QString, QString>> &targetLabels();

// Hauptverzeichnis der Anwendung (neben der EXE).
// Das Original unterscheidet PyInstaller/Nuitka/Quellcode-Lauf; im C++-Build
// ist das schlicht das Verzeichnis der laufenden EXE.
QString appBaseDir();

// "plugins/"-Ordner; wird bei Bedarf angelegt.
QString pluginsDir();

struct Plugin {
    int id = 0;
    QString name;
    QString exe;
    QString args;
    QString workingDir;
    bool context = false;                     // im Kontextmenue anzeigen?
    QString targets = QStringLiteral("both"); // both | file | dir
    bool bundled = false;                     // zentral ueber plugins/plugins.json bereitgestellt

    QJsonObject toJson() const;
    static Plugin fromJson(const QJsonObject &data);

    // Soll dieses Plugin im Kontextmenue fuer das Element erscheinen?
    bool matches(bool isDir) const;
};

// Zentral bereitgestellte Plugins (fuer Firmen-Rollout). Liegt im Plugin-
// Ordner und wird nicht ins Repository eingecheckt.
QString bundledFile();

// Nur die eigenen Plugins (aus den Benutzereinstellungen).
std::vector<Plugin> load();

// Nur die zentral bereitgestellten Plugins (plugins/plugins.json).
std::vector<Plugin> loadBundled();

// Relative Pfade werden gegen den Plugin-Ordner aufgeloest, absolute bleiben.
QString resolvePath(const QString &path);

// Eigene + zentrale Plugins. Zentrale Eintraege mit bereits vorhandenem
// Programmpfad werden uebersprungen (eigene Einstellung hat Vorrang).
std::vector<Plugin> loadAll();

void save(const std::vector<Plugin> &plugins);

int nextId(const std::vector<Plugin> &plugins);

// Zeiger in die uebergebene Liste; nullptr, wenn nicht gefunden.
const Plugin *byId(const std::vector<Plugin> &plugins, int pid);

// Startet das Plugin (QProcess::startDetached); uebergibt optional den Pfad
// des gewaehlten Elements.
//
// Die Parameter-Vorlage darf "{path}" enthalten (wird ersetzt). Enthaelt sie
// keinen Platzhalter und ist ein Pfad gegeben, wird er angehaengt.
// Fehler (kein Programm angegeben / nicht gefunden) -> std::runtime_error.
void launch(const Plugin &plugin, const QString &path = {});

} // namespace ncssh::core::plugins
