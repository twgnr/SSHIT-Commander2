#include "ncssh/gui/sftp_batch_dialog.hpp"

#include "ncssh/core/filesystem.hpp"
#include "ncssh/core/i18n.hpp"
#include "ncssh/core/settings.hpp"
#include "ncssh/net/sftp_batch.hpp"

#include <QCheckBox>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;

SftpBatchDialog::SftpBatchDialog(AsyncBridge *bridge, net::SSHSessionPtr session, QWidget *parent)
    : QDialog(parent), m_bridge(bridge), m_session(std::move(session))
{
    setWindowTitle(_t("SFTP-Batch / geplante Aufgaben"));
    resize(760, 640);

    auto *layout = new QVBoxLayout(this);

    auto *help = new QLabel(
        _t("Ein Befehl pro Zeile. Verfügbar: cd, lcd, pwd, lpwd, mkdir, rm, rmdir, "
           "rename, chmod, ln, put, get, echo. Relative Pfade: entfernt ab „cd“, "
           "lokal ab „lcd“. Pfade mit Leerzeichen in \"Anführungszeichen\". # = Kommentar."),
        this);
    help->setWordWrap(true);
    help->setStyleSheet(QStringLiteral("color: palette(mid);"));
    layout->addWidget(help);

    m_editor = new QPlainTextEdit(this);
    m_editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_editor->setPlaceholderText(QStringLiteral(
        "# Beispiel:\n"
        "cd /var/www\n"
        "put report.csv daten/report.csv\n"
        "chmod 640 daten/report.csv\n"
        "get logs/app.log app-$heute.log"));
    // Zuletzt verwendetes Skript wiederherstellen.
    m_editor->setPlainText(core::getSettingString(QStringLiteral("sftp_batch_script")));
    layout->addWidget(m_editor, 3);

    // Optionen-Zeile: Fehlerverhalten + Zeitplanung.
    auto *opts = new QHBoxLayout();
    m_stopOnError = new QCheckBox(_t("Bei Fehler abbrechen"), this);
    opts->addWidget(m_stopOnError);
    opts->addSpacing(16);
    m_schedule = new QCheckBox(_t("Wiederholen alle"), this);
    m_interval = new QSpinBox(this);
    m_interval->setRange(1, 1440);
    m_interval->setValue(core::getSettingInt(QStringLiteral("sftp_batch_interval_min"), 15));
    m_interval->setSuffix(_t(" Min."));
    opts->addWidget(m_schedule);
    opts->addWidget(m_interval);
    opts->addStretch(1);
    layout->addLayout(opts);

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(m_log, 2);

    m_status = new QLabel(this);
    layout->addWidget(m_status);

    auto *buttons = new QHBoxLayout();
    m_runBtn = new QPushButton(_t("Ausführen"), this);
    m_stopBtn = new QPushButton(_t("Stopp"), this);
    auto *loadBtn = new QPushButton(_t("Laden …"), this);
    auto *saveBtn = new QPushButton(_t("Speichern …"), this);
    auto *closeBtn = new QPushButton(_t("Schließen"), this);
    buttons->addWidget(m_runBtn);
    buttons->addWidget(m_stopBtn);
    buttons->addStretch(1);
    buttons->addWidget(loadBtn);
    buttons->addWidget(saveBtn);
    buttons->addWidget(closeBtn);
    layout->addLayout(buttons);

    connect(m_runBtn, &QPushButton::clicked, this, &SftpBatchDialog::runBatch);
    connect(m_stopBtn, &QPushButton::clicked, this, &SftpBatchDialog::stopBatch);
    connect(loadBtn, &QPushButton::clicked, this, &SftpBatchDialog::loadScript);
    connect(saveBtn, &QPushButton::clicked, this, &SftpBatchDialog::saveScript);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_schedule, &QCheckBox::toggled, this, &SftpBatchDialog::onScheduleToggled);

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, [this] {
        if (!m_running)  // laufenden Durchlauf nicht ueberholen
            runBatch();
    });

    updateButtons();
}

void SftpBatchDialog::updateButtons()
{
    m_runBtn->setEnabled(!m_running);
    m_stopBtn->setEnabled(m_running);
}

void SftpBatchDialog::appendLog(const QString &line)
{
    m_log->appendPlainText(line);
}

void SftpBatchDialog::runBatch()
{
    if (m_running)
        return;
    if (!m_session) {
        QMessageBox::information(this, windowTitle(),
                                 _t("Dafür muss der Tab mit einem Server verbunden sein."));
        return;
    }
    const QString script = m_editor->toPlainText();
    // Skript + Intervall fuer die naechste Sitzung merken.
    core::setSetting(QStringLiteral("sftp_batch_script"), script);
    core::setSetting(QStringLiteral("sftp_batch_interval_min"), m_interval->value());

    const bool stopOnError = m_stopOnError->isChecked();
    net::SSHSessionPtr session = m_session;

    m_log->clear();
    appendLog(_t("▶ Start …"));
    m_running = true;
    updateButtons();

    m_task = m_bridge->stream(
        [script, session, stopOnError](const AsyncBridge::EmitLine &emitLine,
                                       const CancelTokenPtr &cancel) {
            // Provider je Lauf frisch aufbauen; der gehaltene Session-Zeiger
            // haelt die Verbindung am Leben (auch wenn der Tab getrennt wird).
            core::LocalFileSystem local;
            net::SFTPFileSystem remote(session);
            const net::BatchResult r = net::runSftpBatch(
                script, &local, &remote, QString(), QString(),
                [&emitLine](const QString &l) { emitLine(l); }, stopOnError, cancel);
            emitLine(QStringLiteral("__DONE__ %1 %2 %3")
                         .arg(r.ok).arg(r.failed).arg(r.aborted ? 1 : 0));
        },
        // onLine
        [this](const QString &line) {
            if (line.startsWith(QStringLiteral("__DONE__"))) {
                const QStringList p = line.split(QLatin1Char(' '));
                if (p.size() >= 3)
                    m_status->setText(_t("Fertig: %1 ok, %2 Fehler").arg(p[1], p[2]));
                return;
            }
            appendLog(line);
        },
        // onFinished
        [this] {
            m_running = false;
            m_task = nullptr;
            updateButtons();
        },
        // onError
        [this](const QString &err) {
            appendLog(_t("Abbruch: %1").arg(err));
            m_status->setText(_t("Abgebrochen"));
            m_running = false;
            m_task = nullptr;
            updateButtons();
        });
}

void SftpBatchDialog::stopBatch()
{
    if (m_task)
        m_bridge->cancel(m_task);
}

void SftpBatchDialog::onScheduleToggled(bool on)
{
    if (on) {
        m_timer->start(m_interval->value() * 60 * 1000);
        m_status->setText(_t("Geplant: alle %1 Min. (solange dieser Dialog offen ist)")
                              .arg(m_interval->value()));
    } else {
        m_timer->stop();
        m_status->clear();
    }
}

void SftpBatchDialog::loadScript()
{
    const QString path = QFileDialog::getOpenFileName(
        this, _t("Batch-Skript laden"), QString(),
        _t("Batch-Skripte (*.txt *.sftp *.batch);;Alle Dateien (*)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, _t("Fehler"), _t("Datei nicht lesbar."));
        return;
    }
    m_editor->setPlainText(QString::fromUtf8(f.readAll()));
}

void SftpBatchDialog::saveScript()
{
    const QString path = QFileDialog::getSaveFileName(
        this, _t("Batch-Skript speichern"), QString(),
        _t("Batch-Skripte (*.txt *.sftp *.batch);;Alle Dateien (*)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, _t("Fehler"), _t("Datei nicht schreibbar."));
        return;
    }
    f.write(m_editor->toPlainText().toUtf8());
}

} // namespace ncssh::gui
