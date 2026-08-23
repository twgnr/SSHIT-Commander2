// Datei-Alarm: Verzeichnisse auf Aenderungen ueberwachen (Polling per
// Schnappschuss-Vergleich) und Ereignisse melden.
#pragma once

#include "ncssh/core/filealarm.hpp"
#include "ncssh/gui/bridge.hpp"
#include "ncssh/net/ssh.hpp"

#include <QDialog>
#include <QHash>
#include <QObject>
#include <functional>
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
    // Liefert die SSH-Sitzung zum gebundenen Profil (leerer Name = irgendeine
    // aktive Verbindung, fuer Alarme aus aelteren Fassungen). Wird beim Poll im
    // GUI-Thread abgefragt; leer = nicht verbunden -> Alarm pausiert.
    void setSessionProvider(std::function<net::SSHSessionPtr(const QString &)> provider)
    { m_sessionProvider = std::move(provider); }

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
    QHash<int, core::Snapshot> m_snapshots;    // Alarm-ID -> letzter Stand
    QHash<int, QString> m_snapshotOrigin;      // Alarm-ID -> Herkunft ("local"/Session-Label)
    std::function<net::SSHSessionPtr(const QString &)> m_sessionProvider;
    bool m_busy = false;
};

class FileAlarmDialog : public QDialog {
    Q_OBJECT
public:
    // presetPath (optional): oeffnet direkt den Anlege-Dialog fuer dieses
    // Verzeichnis — aus dem Pane-Kontextmenue "Alarm Trigger setzen".
    // currentProfile: Server, an den neue Remote-Alarme gebunden werden.
    FileAlarmDialog(FileAlarmManager *manager, QWidget *parent = nullptr,
                    const QString &presetPath = {}, bool presetRemote = false,
                    const QString &currentProfile = {});

private:
    void reload();
    void addAlarm(const QString &presetPath = {}, bool presetRemote = false);
    void editAlarm();
    void removeAlarm();
    void toggleAlarm();

    FileAlarmManager *m_manager;
    std::vector<core::AlarmSpec> m_alarms;
    QTableWidget *m_table = nullptr;
    QListWidget *m_events = nullptr;
    QLabel *m_status = nullptr;
    QString m_currentProfile;   // Bindung fuer neue Remote-Alarme
};

} // namespace ncssh::gui
