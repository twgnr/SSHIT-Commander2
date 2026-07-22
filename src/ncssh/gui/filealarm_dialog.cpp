#include "ncssh/gui/filealarm_dialog.hpp"

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
#include <QMessageBox>
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
    m_timer->setInterval(10000);  // 10 s Polling-Intervall
    reload();
}

void FileAlarmManager::setIntervalSeconds(int seconds)
{
    m_timer->setInterval(qMax(2, seconds) * 1000);
}

void FileAlarmManager::reload()
{
    m_alarms = core::loadAlarms();
    // Schnappschuesse verworfener Alarme freigeben.
    QHash<int, core::Snapshot> kept;
    for (const AlarmSpec &a : m_alarms) {
        if (m_snapshots.contains(a.id))
            kept.insert(a.id, m_snapshots.value(a.id));
    }
    m_snapshots = kept;

    const bool anyEnabled = std::any_of(m_alarms.begin(), m_alarms.end(),
                                        [](const AlarmSpec &a) { return a.enabled; });
    if (anyEnabled)
        m_timer->start();
    else
        m_timer->stop();
}

void FileAlarmManager::poll()
{
    if (m_busy || m_alarms.empty())
        return;
    m_busy = true;
    const std::vector<AlarmSpec> alarms = m_alarms;
    const QHash<int, core::Snapshot> previous = m_snapshots;

    // Scannen laeuft im Worker; Ergebnis (neue Schnappschuesse + Events) im GUI.
    using Result = std::pair<QHash<int, core::Snapshot>,
                             std::vector<std::tuple<QString, QString, QString>>>;
    m_bridge->run<Result>(
        [alarms, previous]() -> Result {
            QHash<int, core::Snapshot> snapshots;
            std::vector<std::tuple<QString, QString, QString>> events;
            for (const AlarmSpec &a : alarms) {
                if (!a.enabled)
                    continue;
                const core::Snapshot now = core::scanDir(a.path, a.recursive, a.includeDirs);
                snapshots.insert(a.id, now);
                if (!previous.contains(a.id))
                    continue;  // erster Durchlauf: nur Basis aufnehmen
                for (const auto &[kind, path, isDir] :
                     core::diffSnapshots(previous.value(a.id), now,
                                         a.onCreated, a.onModified, a.onDeleted)) {
                    events.emplace_back(kind, path, a.name);
                }
            }
            return {snapshots, events};
        },
        [this](const Result &result) {
            for (auto it = result.first.begin(); it != result.first.end(); ++it)
                m_snapshots.insert(it.key(), it.value());
            for (const auto &[kind, path, name] : result.second)
                emit event(kind, path, name);
            m_busy = false;
        },
        [this](const QString &) { m_busy = false; });
}

// ---------------------------------------------------------------------------
// Bearbeiten-Dialog fuer einen einzelnen Alarm
// ---------------------------------------------------------------------------

namespace {
bool editAlarmSpec(AlarmSpec &spec, QWidget *parent)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(_t("Datei-Alarm"));
    auto *layout = new QVBoxLayout(&dlg);
    auto *form = new QFormLayout();

    auto *name = new QLineEdit(spec.name, &dlg);
    auto *pathRow = new QHBoxLayout();
    auto *path = new QLineEdit(spec.path, &dlg);
    auto *browse = new QPushButton(QStringLiteral("…"), &dlg);
    browse->setFixedWidth(34);
    QObject::connect(browse, &QPushButton::clicked, &dlg, [&dlg, path] {
        const QString d = getExistingDirectory(&dlg, _t("Ordner überwachen"),
                                                            path->text());
        if (!d.isEmpty())
            path->setText(d);
    });
    pathRow->addWidget(path, 1);
    pathRow->addWidget(browse);

    auto *onCreated = new QCheckBox(_t("Neu"), &dlg);
    onCreated->setChecked(spec.onCreated);
    auto *onModified = new QCheckBox(_t("Geändert"), &dlg);
    onModified->setChecked(spec.onModified);
    auto *onDeleted = new QCheckBox(_t("Gelöscht"), &dlg);
    onDeleted->setChecked(spec.onDeleted);
    auto *eventRow = new QHBoxLayout();
    eventRow->addWidget(onCreated);
    eventRow->addWidget(onModified);
    eventRow->addWidget(onDeleted);

    auto *recursive = new QCheckBox(_t("Unterordner einbeziehen"), &dlg);
    recursive->setChecked(spec.recursive);
    auto *includeDirs = new QCheckBox(_t("Ordner mitzählen"), &dlg);
    includeDirs->setChecked(spec.includeDirs);
    auto *enabled = new QCheckBox(_t("aktiv"), &dlg);
    enabled->setChecked(spec.enabled);

    form->addRow(_t("Name"), name);
    form->addRow(_t("Ordner"), pathRow);
    form->addRow(_t("Ereignisse"), eventRow);
    form->addRow(QString(), recursive);
    form->addRow(QString(), includeDirs);
    form->addRow(QString(), enabled);
    layout->addLayout(form);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QObject::connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(box);

    if (dlg.exec() != QDialog::Accepted)
        return false;
    if (path->text().trimmed().isEmpty())
        return false;
    spec.name = name->text().trimmed().isEmpty() ? path->text() : name->text().trimmed();
    spec.path = path->text().trimmed();
    spec.onCreated = onCreated->isChecked();
    spec.onModified = onModified->isChecked();
    spec.onDeleted = onDeleted->isChecked();
    spec.recursive = recursive->isChecked();
    spec.includeDirs = includeDirs->isChecked();
    spec.enabled = enabled->isChecked();
    return true;
}
} // namespace

// ---------------------------------------------------------------------------
// FileAlarmDialog
// ---------------------------------------------------------------------------

FileAlarmDialog::FileAlarmDialog(FileAlarmManager *manager, QWidget *parent)
    : QDialog(parent), m_manager(manager)
{
    setWindowTitle(_t("Datei-Alarm"));
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

    layout->addWidget(new QLabel(_t("Ereignisse"), this));
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

    auto *buttons = new QHBoxLayout();
    auto *addBtn = new QPushButton(_t("Neu"), this);
    auto *editBtn = new QPushButton(_t("Bearbeiten"), this);
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
        m_table->setItem(row, 1, new QTableWidgetItem(a.path));
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
