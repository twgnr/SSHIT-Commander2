#include "ncssh/gui/filealarm_dialog.hpp"

#include "ncssh/core/settings.hpp"

#include "ncssh/core/i18n.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include "ncssh/gui/file_dialogs.hpp"
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QDir>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace ncssh::gui {

using core::_t;
using core::AlarmSpec;

// ---------------------------------------------------------------------------
// FileAlarmManager
// ---------------------------------------------------------------------------

FileAlarmManager::FileAlarmManager(AsyncBridge *bridge, QObject *parent)
    : QObject(parent), m_bridge(bridge), m_timer(new QTimer(this))
{
    connect(m_timer, &QTimer::timeout, this, &FileAlarmManager::poll);
    // Poll-Intervall aus den Einstellungen (verstecktes file_alarm_interval,
    // Sekunden; Standard 5, min. 2) — wie im Original.
    m_timer->setInterval(qMax(2, core::getSettingInt(QStringLiteral("file_alarm_interval"), 5))
                         * 1000);
    reload();
}

void FileAlarmManager::setIntervalSeconds(int seconds)
{
    m_timer->setInterval(qMax(2, seconds) * 1000);
}

void FileAlarmManager::reload()
{
    m_alarms = core::loadAlarms();
    // Schnappschuesse (+ Herkunft) verworfener Alarme freigeben.
    QHash<int, core::Snapshot> keptSnap;
    QHash<int, QString> keptOrigin;
    for (const AlarmSpec &a : m_alarms) {
        if (m_snapshots.contains(a.id))
            keptSnap.insert(a.id, m_snapshots.value(a.id));
        if (m_snapshotOrigin.contains(a.id))
            keptOrigin.insert(a.id, m_snapshotOrigin.value(a.id));
    }
    m_snapshots = keptSnap;
    m_snapshotOrigin = keptOrigin;

    const bool anyEnabled = std::any_of(m_alarms.begin(), m_alarms.end(),
                                        [](const AlarmSpec &a) { return a.enabled; });
    if (anyEnabled)
        m_timer->start();
    else
        m_timer->stop();
}

namespace {
// Momentaufnahme eines REMOTE-Verzeichnisses ueber SFTP (mtime/groesse/ordner).
// Traversiert vollstaendig (Filter wirkt nur auf die Aufnahme). Kann werfen —
// der Aufrufer faengt pro Alarm.
core::Snapshot remoteScan(net::SFTPFileSystem &fs, const QString &path, bool recursive,
                          bool includeDirs, const QString &inc, const QString &exc, int limit,
                          int depth)
{
    core::Snapshot out;
    for (const core::FileEntry &e : fs.listDir(path)) {
        if (e.type == core::EntryType::Parent)
            continue;
        // Server-Namen mit Pfad-Trennern/".." ueberspringen (defense in depth).
        if (e.name == QLatin1String("..") || e.name.contains(QLatin1Char('/'))
            || e.name.contains(QLatin1Char('\\')))
            continue;
        const QString full = fs.join(path, e.name);
        const bool isDir = e.isDir();
        if ((includeDirs || !isDir) && core::matchesGlobFilter(e.name, inc, exc)) {
            core::SnapshotEntry se;
            se.isDir = isDir;
            se.mtime = e.modified.isValid() ? e.modified.toSecsSinceEpoch() : 0;
            se.size = isDir ? 0 : e.size;
            out.insert(full, se);
        }
        if (out.size() >= limit)
            break;
        if (recursive && isDir && depth < 32) {
            const core::Snapshot sub =
                remoteScan(fs, full, recursive, includeDirs, inc, exc, limit, depth + 1);
            for (auto it = sub.begin(); it != sub.end() && out.size() < limit; ++it)
                out.insert(it.key(), it.value());
        }
    }
    return out;
}
}  // namespace

void FileAlarmManager::poll()
{
    if (m_busy || m_alarms.empty())
        return;
    m_busy = true;
    const std::vector<AlarmSpec> alarms = m_alarms;
    const QHash<int, core::Snapshot> previous = m_snapshots;
    const QHash<int, QString> previousOrigin = m_snapshotOrigin;
    // Aktive Sitzung im GUI-Thread holen (fuer Remote-Alarme); der shared_ptr
    // haelt sie waehrend des Scans am Leben.
    const net::SSHSessionPtr session = m_sessionProvider ? m_sessionProvider() : nullptr;
    const QString sessLabel = session ? session->label() : QString();

    // Scannen laeuft im Worker; Ergebnis (neue Schnappschuesse + Herkunft +
    // Events + Firings pro Alarm) kommt im GUI-Thread an.
    struct Firing {
        QString kind;
        QString path;
        int count = 0;
    };
    struct Result {
        QHash<int, core::Snapshot> snapshots;
        QHash<int, QString> origins;
        std::vector<std::tuple<QString, QString, QString>> events;
        QHash<int, Firing> firings;  // Alarm-ID -> erstes Ereignis + Anzahl
    };
    m_bridge->run<Result>(
        [alarms, previous, previousOrigin, session, sessLabel]() -> Result {
            Result r;
            for (const AlarmSpec &a : alarms) {
                if (!a.enabled)
                    continue;
                const QString origin = a.remote ? sessLabel : QStringLiteral("local");
                core::Snapshot now;
                if (a.remote) {
                    if (!session)
                        continue;  // nicht verbunden -> diesen Alarm ueberspringen
                    try {
                        net::SFTPFileSystem fs(session);
                        now = remoteScan(fs, a.path, a.recursive, a.includeDirs, a.includeGlob,
                                         a.excludeGlob, 20'000, 0);
                    } catch (...) {
                        continue;  // Remote-Fehler isolieren, andere Alarme laufen weiter
                    }
                } else {
                    now = core::scanDir(a.path, a.recursive, a.includeDirs, a.includeGlob,
                                        a.excludeGlob);
                }
                r.snapshots.insert(a.id, now);
                r.origins.insert(a.id, origin);
                // Nur vergleichen, wenn eine Basis GLEICHER Herkunft existiert —
                // sonst (erster Lauf / Serverwechsel) nur neu einpegeln.
                if (!previous.contains(a.id) || previousOrigin.value(a.id) != origin)
                    continue;
                for (const auto &[kind, path, isDir] :
                     core::diffSnapshots(previous.value(a.id), now, a.onCreated, a.onModified,
                                         a.onDeleted)) {
                    r.events.emplace_back(kind, path, a.name);
                    if (r.firings.contains(a.id))
                        ++r.firings[a.id].count;
                    else
                        r.firings.insert(a.id, Firing{kind, path, 1});
                }
            }
            return r;
        },
        [this](const Result &result) {
            for (auto it = result.snapshots.begin(); it != result.snapshots.end(); ++it)
                m_snapshots.insert(it.key(), it.value());
            for (auto it = result.origins.begin(); it != result.origins.end(); ++it)
                m_snapshotOrigin.insert(it.key(), it.value());
            for (const auto &[kind, path, name] : result.events)
                emit event(kind, path, name);
            // Aktionen EINMAL pro ausgeloestem Alarm (kein Prozess-Sturm).
            for (auto it = result.firings.begin(); it != result.firings.end(); ++it) {
                const auto spec = std::find_if(
                    m_alarms.begin(), m_alarms.end(),
                    [id = it.key()](const AlarmSpec &a) { return a.id == id; });
                if (spec != m_alarms.end() && !spec->actionCmd.trimmed().isEmpty())
                    runAction(*spec, it.value().kind, it.value().path, it.value().count);
            }
            m_busy = false;
        },
        [this](const QString &) { m_busy = false; });
}

void FileAlarmManager::runAction(const core::AlarmSpec &spec, const QString &kind,
                                 const QString &path, int count)
{
    QString cmd = spec.actionCmd.trimmed();
    if (cmd.isEmpty())
        return;
    // Platzhalter ersetzen.
    cmd.replace(QStringLiteral("{path}"), path);
    cmd.replace(QStringLiteral("{kind}"), kind);
    cmd.replace(QStringLiteral("{name}"), spec.name);
    cmd.replace(QStringLiteral("{count}"), QString::number(count));
    // Als losgeloester Prozess starten — die Shell parst Argumente/Quotes. Die
    // Ereignisdaten stehen zusaetzlich als Umgebungsvariablen bereit (robuster
    // als String-Interpolation bei Sonderzeichen im Pfad).
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("ALARM_PATH"), path);
    env.insert(QStringLiteral("ALARM_KIND"), kind);
    env.insert(QStringLiteral("ALARM_NAME"), spec.name);
    env.insert(QStringLiteral("ALARM_COUNT"), QString::number(count));
    QProcess proc;
    proc.setProcessEnvironment(env);
#ifdef Q_OS_WIN
    proc.setProgram(QStringLiteral("cmd"));
    proc.setArguments({QStringLiteral("/c"), cmd});
#else
    proc.setProgram(QStringLiteral("/bin/sh"));
    proc.setArguments({QStringLiteral("-c"), cmd});
#endif
    proc.startDetached();
}

// ---------------------------------------------------------------------------
// Bearbeiten-Dialog fuer einen einzelnen Alarm
// ---------------------------------------------------------------------------

namespace {
bool editAlarmSpec(AlarmSpec &spec, QWidget *parent)
{
    QDialog dlg(parent);
    // Titel sagt, ob angelegt oder bearbeitet wird.
    dlg.setWindowTitle(spec.path.isEmpty() ? _t("Neuer Datei-Alarm")
                                           : _t("Alarm bearbeiten"));
    auto *layout = new QVBoxLayout(&dlg);
    auto *form = new QFormLayout();

    auto *name = new QLineEdit(spec.name, &dlg);
    name->setPlaceholderText(_t("Anzeigename (optional)"));
    auto *pathRow = new QHBoxLayout();
    auto *path = new QLineEdit(spec.path, &dlg);
    auto *browse = new QPushButton(_t("Durchsuchen …"), &dlg);
    QObject::connect(browse, &QPushButton::clicked, &dlg, [&dlg, path] {
        const QString d = getExistingDirectory(&dlg, _t("Ordner wählen"), path->text());
        if (!d.isEmpty())
            path->setText(d);
    });
    pathRow->addWidget(path, 1);
    pathRow->addWidget(browse);

    auto *onCreated = new QCheckBox(_t("Neu erstellt"), &dlg);
    onCreated->setChecked(spec.onCreated);
    auto *onModified = new QCheckBox(_t("Geändert"), &dlg);
    onModified->setChecked(spec.onModified);
    auto *onDeleted = new QCheckBox(_t("Gelöscht"), &dlg);
    onDeleted->setChecked(spec.onDeleted);
    auto *eventRow = new QHBoxLayout();
    eventRow->addWidget(onCreated);
    eventRow->addWidget(onModified);
    eventRow->addWidget(onDeleted);

    auto *remote = new QCheckBox(_t("Remote (aktive SSH-Verbindung)"), &dlg);
    remote->setChecked(spec.remote);
    remote->setToolTip(
        _t("Statt lokal den Pfad auf der gerade aktiven Verbindung per SFTP überwachen."));
    // Bei Remote den lokalen Ordner-Dialog abschalten (Pfad wird getippt).
    QObject::connect(remote, &QCheckBox::toggled, browse, &QPushButton::setDisabled);
    browse->setDisabled(remote->isChecked());

    auto *recursive = new QCheckBox(_t("Unterordner einbeziehen"), &dlg);
    recursive->setChecked(spec.recursive);
    auto *includeDirs = new QCheckBox(_t("Ordner mitüberwachen"), &dlg);
    includeDirs->setChecked(spec.includeDirs);
    auto *enabled = new QCheckBox(_t("aktiv"), &dlg);
    enabled->setChecked(spec.enabled);

    auto *includeGlob = new QLineEdit(spec.includeGlob, &dlg);
    includeGlob->setPlaceholderText(_t("alle — z. B. *.log;*.csv"));
    auto *excludeGlob = new QLineEdit(spec.excludeGlob, &dlg);
    excludeGlob->setPlaceholderText(_t("keine — z. B. *.tmp;*~"));

    auto *actionCmd = new QLineEdit(spec.actionCmd, &dlg);
    actionCmd->setPlaceholderText(_t("optional — z. B. echo {kind} {path} >> alarm.log"));
    actionCmd->setToolTip(
        _t("Lokaler Befehl bei Auslösung (einmal pro Prüfzyklus). Platzhalter: "
           "{path} {kind} {name} {count}; auch als Umgebungsvariablen ALARM_PATH usw."));

    form->addRow(_t("Anzeigename (optional)"), name);
    form->addRow(_t("Zu überwachender Ordner"), pathRow);
    form->addRow(_t("Erkannte Änderungen:"), eventRow);
    form->addRow(_t("Nur diese Muster:"), includeGlob);
    form->addRow(_t("Diese Muster ignorieren:"), excludeGlob);
    form->addRow(_t("Befehl bei Auslösung:"), actionCmd);
    form->addRow(QString(), remote);
    form->addRow(QString(), recursive);
    form->addRow(QString(), includeDirs);
    form->addRow(QString(), enabled);
    layout->addLayout(form);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QObject::connect(box, &QDialogButtonBox::accepted, &dlg, [&] {
        // Ein Alarm ohne Ordner oder ohne Ereignis wuerde nie ausloesen —
        // das lieber hier sagen als still nichts tun. Remote-Pfade lassen sich
        // hier nicht lokal pruefen, daher nur auf "nicht leer" bestehen.
        const QString p = path->text().trimmed();
        if (p.isEmpty() || (!remote->isChecked() && !QDir(p).exists())) {
            QMessageBox::warning(&dlg, _t("Alarm Trigger"),
                                 _t("Bitte einen gültigen Ordner wählen."));
            return;
        }
        if (!onCreated->isChecked() && !onModified->isChecked() && !onDeleted->isChecked()) {
            QMessageBox::warning(&dlg, _t("Alarm Trigger"),
                                 _t("Bitte mindestens ein Ereignis auswählen."));
            return;
        }
        dlg.accept();
    });
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(box);

    if (dlg.exec() != QDialog::Accepted)
        return false;
    spec.name = name->text().trimmed().isEmpty() ? path->text() : name->text().trimmed();
    spec.path = path->text().trimmed();
    spec.onCreated = onCreated->isChecked();
    spec.onModified = onModified->isChecked();
    spec.onDeleted = onDeleted->isChecked();
    spec.remote = remote->isChecked();
    spec.recursive = recursive->isChecked();
    spec.includeDirs = includeDirs->isChecked();
    spec.enabled = enabled->isChecked();
    spec.includeGlob = includeGlob->text().trimmed();
    spec.excludeGlob = excludeGlob->text().trimmed();
    spec.actionCmd = actionCmd->text().trimmed();
    return true;
}
} // namespace

// ---------------------------------------------------------------------------
// FileAlarmDialog
// ---------------------------------------------------------------------------

FileAlarmDialog::FileAlarmDialog(FileAlarmManager *manager, QWidget *parent)
    : QDialog(parent), m_manager(manager)
{
    setWindowTitle(_t("Alarm Trigger"));
    resize(820, 560);

    auto *layout = new QVBoxLayout(this);
    m_table = new QTableWidget(0, 5, this);
    m_table->setHorizontalHeaderLabels({_t("Name"), _t("Ordner"), _t("Ereignisse"),
                                        _t("Rekursiv"), _t("Aktiv")});
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(m_table, &QTableWidget::cellDoubleClicked, this,
            [this](int, int) { editAlarm(); });
    layout->addWidget(m_table, 2);

    layout->addWidget(new QLabel(
        _t("Überwachte Ordner — Häkchen schaltet einen Alarm an/aus:"), this));
    m_events = new QListWidget(this);
    layout->addWidget(m_events, 1);
    connect(manager, &FileAlarmManager::event, this,
            [this](const QString &kind, const QString &path, const QString &alarmName) {
                m_events->insertItem(0, QStringLiteral("[%1] %2 — %3")
                                            .arg(alarmName, kind, path));
                while (m_events->count() > 500)
                    delete m_events->takeItem(m_events->count() - 1);
            });

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("Muted"));
    layout->addWidget(m_status);

    // Benachrichtigungsart (global, gilt fuer alle Alarme).
    auto *notifyRow = new QHBoxLayout();
    auto *trayNotify = new QCheckBox(_t("Desktop-Benachrichtigung"), this);
    trayNotify->setChecked(core::getSettingBool(QStringLiteral("alarm_tray_notify"), true));
    connect(trayNotify, &QCheckBox::toggled, this, [](bool on) {
        core::setSetting(QStringLiteral("alarm_tray_notify"), on);
    });
    auto *sound = new QCheckBox(_t("Signalton"), this);
    sound->setChecked(core::getSettingBool(QStringLiteral("alarm_sound"), false));
    connect(sound, &QCheckBox::toggled, this, [](bool on) {
        core::setSetting(QStringLiteral("alarm_sound"), on);
    });
    notifyRow->addWidget(new QLabel(_t("Bei Auslösung:"), this));
    notifyRow->addWidget(trayNotify);
    notifyRow->addWidget(sound);
    notifyRow->addStretch(1);
    layout->addLayout(notifyRow);

    auto *buttons = new QHBoxLayout();
    auto *addBtn = new QPushButton(_t("Neu …"), this);
    auto *editBtn = new QPushButton(_t("Bearbeiten …"), this);
    auto *toggleBtn = new QPushButton(_t("Aktiv/Inaktiv"), this);
    auto *removeBtn = new QPushButton(_t("Löschen"), this);
    auto *closeBtn = new QPushButton(_t("Schließen"), this);
    closeBtn->setDefault(true);
    connect(addBtn, &QPushButton::clicked, this, &FileAlarmDialog::addAlarm);
    connect(editBtn, &QPushButton::clicked, this, &FileAlarmDialog::editAlarm);
    connect(toggleBtn, &QPushButton::clicked, this, &FileAlarmDialog::toggleAlarm);
    connect(removeBtn, &QPushButton::clicked, this, &FileAlarmDialog::removeAlarm);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(addBtn);
    buttons->addWidget(editBtn);
    buttons->addWidget(toggleBtn);
    buttons->addWidget(removeBtn);
    buttons->addStretch(1);
    buttons->addWidget(closeBtn);
    layout->addLayout(buttons);

    reload();
}

void FileAlarmDialog::reload()
{
    m_alarms = core::loadAlarms();
    m_table->setRowCount(0);
    int row = 0;
    for (const AlarmSpec &a : m_alarms) {
        m_table->insertRow(row);
        m_table->setItem(row, 0, new QTableWidgetItem(a.name));
        m_table->setItem(row, 1,
                         new QTableWidgetItem(a.remote ? _t("🌐 %1 (remote)").arg(a.path)
                                                       : a.path));
        m_table->setItem(row, 2, new QTableWidgetItem(a.eventsLabel()));
        m_table->setItem(row, 3, new QTableWidgetItem(a.recursive ? QStringLiteral("✓")
                                                                  : QString()));
        m_table->setItem(row, 4, new QTableWidgetItem(a.enabled ? QStringLiteral("✓")
                                                                : QString()));
        ++row;
    }
    m_status->setText(QStringLiteral("%1 Alarm(e)").arg(m_alarms.size()));
}

void FileAlarmDialog::addAlarm()
{
    AlarmSpec spec;
    int maxId = 0;
    for (const AlarmSpec &a : m_alarms)
        maxId = qMax(maxId, a.id);
    spec.id = maxId + 1;
    if (!editAlarmSpec(spec, this))
        return;
    m_alarms.push_back(spec);
    core::saveAlarms(m_alarms);
    m_manager->reload();
    reload();
}

void FileAlarmDialog::editAlarm()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= int(m_alarms.size()))
        return;
    AlarmSpec spec = m_alarms[row];
    if (!editAlarmSpec(spec, this))
        return;
    m_alarms[row] = spec;
    core::saveAlarms(m_alarms);
    m_manager->reload();
    reload();
}

void FileAlarmDialog::toggleAlarm()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= int(m_alarms.size()))
        return;
    m_alarms[row].enabled = !m_alarms[row].enabled;
    core::saveAlarms(m_alarms);
    m_manager->reload();
    reload();
}

void FileAlarmDialog::removeAlarm()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= int(m_alarms.size()))
        return;
    if (QMessageBox::question(this, _t("Löschen"),
                              QStringLiteral("Alarm \"%1\" löschen?").arg(m_alarms[row].name))
        != QMessageBox::Yes)
        return;
    m_alarms.erase(m_alarms.begin() + row);
    core::saveAlarms(m_alarms);
    m_manager->reload();
    reload();
}

} // namespace ncssh::gui
