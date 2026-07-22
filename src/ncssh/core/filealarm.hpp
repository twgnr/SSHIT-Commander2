// Datei-Alarm: ueberwacht Verzeichnisse auf Aenderungen (Bordmittel).
//
// Die Ueberwachung erfolgt per Schnappschuss-Vergleich (Polling): scanDir
// erzeugt eine Momentaufnahme {pfad: (mtime, groesse, ist_ordner)}, diffSnapshots
// ermittelt daraus neue / geaenderte / geloeschte Eintraege. Beide Funktionen
// sind rein und testbar; das Polling-Intervall liegt im GUI-Manager.
// (Port von core/filealarm.py)
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

    QJsonObject toJson() const;
    static AlarmSpec fromJson(const QJsonObject &d);
    QString eventsLabel() const;
};

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
Snapshot scanDir(const QString &path, bool recursive = false,
                 bool includeDirs = true, int limit = 50'000);

// Vergleicht zwei Schnappschuesse -> Liste (art, pfad, ist_ordner).
// art ist "created" | "modified" | "deleted". Fuer Verzeichnisse wird
// "modified" bewusst ausgelassen (Ordner-mtime ist zu "laut").
std::vector<std::tuple<QString, QString, bool>> diffSnapshots(
    const Snapshot &oldSnap, const Snapshot &newSnap,
    bool onCreated = true, bool onModified = true, bool onDeleted = true);

std::vector<AlarmSpec> loadAlarms();
void saveAlarms(const std::vector<AlarmSpec> &alarms);

} // namespace ncssh::core
