// Datei-Alarm: Verzeichnisse auf Aenderungen ueberwachen (Polling per
// Schnappschuss-Vergleich) und Ereignisse melden.
// (Port von gui/filealarm_dialog.py + filealarm_manager.py)
#pragma once

#include "ncssh/core/filealarm.hpp"
#include "ncssh/gui/bridge.hpp"

#include <QDialog>
#include <QHash>
#include <QObject>
#include <vector>

class QTableWidget;
class QListWidget;
class QLabel;
class QTimer;

namespace ncssh::gui {

// Ueberwacht die aktiven Alarme im Hintergrund und meldet Ereignisse.
class FileAlarmManager : public QObject {
    Q_OBJECT
public:
    explicit FileAlarmManager(AsyncBridge *bridge, QObject *parent = nullptr);

    void reload();                 // Alarme aus den Einstellungen neu laden
    void setIntervalSeconds(int seconds);

signals:
    // art ("created"/"modified"/"deleted"), Pfad, Name des Alarms
    void event(const QString &kind, const QString &path, const QString &alarmName);

private:
    void poll();
    // Fuehrt den optionalen Befehl eines ausgeloesten Alarms aus (lokal, einmal
    // pro Poll-Zyklus). Platzhalter {path} {kind} {name} {count} werden ersetzt.
    void runAction(const core::AlarmSpec &spec, const QString &kind, const QString &path,
                   int count);

    AsyncBridge *m_bridge;
    QTimer *m_timer;
    std::vector<core::AlarmSpec> m_alarms;
    QHash<int, core::Snapshot> m_snapshots;   // Alarm-ID -> letzter Stand
    bool m_busy = false;
};

class FileAlarmDialog : public QDialog {
    Q_OBJECT
public:
    FileAlarmDialog(FileAlarmManager *manager, QWidget *parent = nullptr);

private:
    void reload();
    void addAlarm();
    void editAlarm();
    void removeAlarm();
    void toggleAlarm();

    FileAlarmManager *m_manager;
    std::vector<core::AlarmSpec> m_alarms;
    QTableWidget *m_table = nullptr;
    QListWidget *m_events = nullptr;
    QLabel *m_status = nullptr;
};

} // namespace ncssh::gui
