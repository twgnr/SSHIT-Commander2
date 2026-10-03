// Datei-Alarm: ueberwacht Verzeichnisse auf Aenderungen (Bordmittel).
//
// Die Ueberwachung erfolgt per Schnappschuss-Vergleich (Polling): scanDir
// erzeugt eine Momentaufnahme {pfad: (mtime, groesse, ist_ordner)}, diffSnapshots
// ermittelt daraus neue / geaenderte / geloeschte Eintraege. Beide Funktionen
// sind rein und testbar; das Polling-Intervall liegt im GUI-Manager.
#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <tuple>
#include <vector>

namespace ncssh::core {

struct AlarmSpec {
    int id = 0;
    QString name;
    QString path;
    bool onCreated = true;
    bool onModified = true;
    bool onDeleted = true;
    bool recursive = false;
    bool includeDirs = true;
    bool enabled = true;
    bool remote = false;   // ueber eine SSH-Verbindung statt lokal ueberwachen
    // Profil/Server, zu dem ein Remote-Alarm gehoert. Leer = irgendeine aktive
    // Verbindung (Verhalten vor 1.0.0-beta.2). Ohne diese Bindung wechselte ein
    // Alarm den ueberwachten Server, sobald der Nutzer den Tab wechselte.
    QString profile;
    // Namensfilter (jeweils ';'-getrennte Wildcard-Muster; leer = ohne Wirkung).
    QString includeGlob;   // nur passende Namen beruecksichtigen (z.B. "*.log;*.csv")
    QString excludeGlob;   // passende Namen ignorieren (z.B. "*.tmp;*~")
    // Optionaler lokaler Befehl, der bei Ausloesung EINMAL pro Poll-Zyklus laeuft.
    // Platzhalter: {path} {kind} {name} {count}. Leer = keine Aktion.
    QString actionCmd;

    QJsonObject toJson() const;
    static AlarmSpec fromJson(const QJsonObject &d);
    QString eventsLabel() const;
};

// True, wenn `name` den Filtern genuegt: passt zu mind. einem Include-Muster
// (falls Includes gesetzt) UND zu keinem Exclude-Muster. Muster sind
// ';'-getrennte Wildcards (*, ?). Oeffentlich fuer Tests.
bool matchesGlobFilter(const QString &name, const QString &includeGlob,
                       const QString &excludeGlob);

// (mtime als Unix-Sekunden, groesse, ist_ordner)
struct SnapshotEntry {
    qint64 mtime = 0;
    qint64 size = 0;
    bool isDir = false;
    bool operator==(const SnapshotEntry &o) const
    { return mtime == o.mtime && size == o.size && isDir == o.isDir; }
};
using Snapshot = QHash<QString, SnapshotEntry>;

// Momentaufnahme {vollpfad: (mtime, groesse, ist_ordner)} eines Verzeichnisses.
// includeGlob/excludeGlob filtern die erfassten Namen (Traversierung bleibt
// vollstaendig, damit Unterordner trotz Filter durchsucht werden).
Snapshot scanDir(const QString &path, bool recursive = false, bool includeDirs = true,
                 const QString &includeGlob = {}, const QString &excludeGlob = {},
                 int limit = 50'000);

// Vergleicht zwei Schnappschuesse -> Liste (art, pfad, ist_ordner).
// art ist "created" | "modified" | "deleted". Fuer Verzeichnisse wird
// "modified" bewusst ausgelassen (Ordner-mtime ist zu "laut").
std::vector<std::tuple<QString, QString, bool>> diffSnapshots(
    const Snapshot &oldSnap, const Snapshot &newSnap,
    bool onCreated = true, bool onModified = true, bool onDeleted = true);

// Baut aus der Befehlsvorlage einer Alarm-Aktion die Shell-Kommandozeile.
// Die Platzhalter {path} {kind} {name} werden NICHT durch ihre Werte ersetzt,
// sondern durch Verweise auf die Umgebungsvariablen ALARM_PATH/ALARM_KIND/
// ALARM_NAME (Windows: !ALARM_PATH! — nur mit cmd /v:on gueltig; sonst
// "${ALARM_PATH}"). Grund: Dateinamen kommen u.U. von einem fremden Server;
// ein Name wie `x & calc.exe` wuerde woertlich eingesetzt als Befehl laufen.
// Verzoegerte Expansion setzt den Wert erst NACH dem Parsen von & | < > ^ ( )
// ein — er bleibt reiner Text. {count} ist eine Zahl und wird direkt ersetzt.
QString alarmShellCommand(const QString &actionCmd, int count);

// Die Umgebungsvariablen fuer alarmShellCommand (Name -> Wert).
QHash<QString, QString> alarmEnvironment(const QString &kind, const QString &path,
                                         const QString &name, int count);

std::vector<AlarmSpec> loadAlarms();
void saveAlarms(const std::vector<AlarmSpec> &alarms);

} // namespace ncssh::core
