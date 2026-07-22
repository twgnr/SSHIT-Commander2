#include "ncssh/gui/netscan_dialog.hpp"

#include "ncssh/core/i18n.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <memory>
#include <mutex>

namespace ncssh::gui {

using core::_t;
using core::HostResult;

NetscanDialog::NetscanDialog(AsyncBridge *bridge, QWidget *parent)
    : QDialog(parent), m_bridge(bridge)
{
    setWindowTitle(_t("Netzwerk-Scanner"));
    resize(1000, 620);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout();

    m_targets = new QLineEdit(core::defaultRange(), this);
    m_targets->setPlaceholderText(QStringLiteral("192.168.1.0/24, 10.0.0.1-50, host.local"));
    connect(m_targets, &QLineEdit::returnPressed, this, &NetscanDialog::startScan);
    form->addRow(_t("Ziele"), m_targets);

    auto *portRow = new QHBoxLayout();
    m_portPreset = new QComboBox(this);
    m_portPreset->addItem(_t("Gängige"), QStringLiteral("common"));
    m_portPreset->addItem(_t("SMB"), QStringLiteral("smb"));
    m_portPreset->addItem(_t("Web"), QStringLiteral("web"));
    m_portPreset->addItem(_t("Fernzugriff"), QStringLiteral("remote"));
    m_portPreset->addItem(_t("Alle bekannten"), QStringLiteral("all"));
    m_portPreset->addItem(_t("Eigene …"), QStringLiteral("custom"));
    m_customPorts = new QLineEdit(this);
    m_customPorts->setPlaceholderText(QStringLiteral("22,80,8000-8100"));
    m_customPorts->setEnabled(false);
    connect(m_portPreset, &QComboBox::currentIndexChanged, this, [this] {
        m_customPorts->setEnabled(m_portPreset->currentData().toString()
                                  == QLatin1String("custom"));
    });
    portRow->addWidget(m_portPreset);
    portRow->addWidget(m_customPorts, 1);
    form->addRow(_t("Ports"), portRow);

    auto *optRow = new QHBoxLayout();
    m_ping = new QCheckBox(QStringLiteral("Ping"), this);
    m_ping->setChecked(true);
    m_onlyAlive = new QCheckBox(_t("nur erreichbare"), this);
    m_onlyAlive->setChecked(true);
    m_resolve = new QCheckBox(_t("Namen auflösen"), this);
    m_resolve->setChecked(true);
    m_shares = new QCheckBox(_t("Freigaben"), this);
    m_shares->setChecked(true);
    m_identify = new QCheckBox(_t("Dienste erkennen"), this);
    m_identify->setChecked(true);
    for (QCheckBox *c : {m_ping, m_onlyAlive, m_resolve, m_shares, m_identify})
        optRow->addWidget(c);
    optRow->addStretch(1);
    form->addRow(_t("Optionen"), optRow);
    layout->addLayout(form);

    m_table = new QTableWidget(0, 7, this);
    m_table->setHorizontalHeaderLabels({_t("IP"), _t("Name"), _t("MAC"), _t("Hersteller"),
                                        _t("OS"), _t("Offene Ports"), _t("Web / Freigaben")});
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setSortingEnabled(true);
    // Doppelklick auf eine Weboberflaeche oeffnet sie im Browser.
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        const QString url = m_table->item(row, 6)->data(Qt::UserRole).toString();
        if (!url.isEmpty())
            QDesktopServices::openUrl(QUrl(url));
    });
    layout->addWidget(m_table, 1);

    m_progress = new QProgressBar(this);
    m_progress->setTextVisible(true);
    layout->addWidget(m_progress);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("Muted"));
    layout->addWidget(m_status);

    auto *buttons = new QHBoxLayout();
    m_startBtn = new QPushButton(_t("Scannen"), this);
    m_startBtn->setDefault(true);
    auto *stopBtn = new QPushButton(_t("Stopp"), this);
    auto *closeBtn = new QPushButton(_t("Schließen"), this);
    connect(m_startBtn, &QPushButton::clicked, this, &NetscanDialog::startScan);
    connect(stopBtn, &QPushButton::clicked, this, &NetscanDialog::stopScan);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(m_startBtn);
    buttons->addWidget(stopBtn);
    buttons->addStretch(1);
    buttons->addWidget(closeBtn);
    layout->addLayout(buttons);
}

void NetscanDialog::addHostRow(const HostResult &host)
{
    m_table->setSortingEnabled(false);
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    m_table->setItem(row, 0, new QTableWidgetItem(host.ip));
    m_table->setItem(row, 1, new QTableWidgetItem(
                                 host.hostname.isEmpty() ? host.netbios : host.hostname));
    m_table->setItem(row, 2, new QTableWidgetItem(host.mac));
    m_table->setItem(row, 3, new QTableWidgetItem(host.vendor));
    m_table->setItem(row, 4, new QTableWidgetItem(host.osGuess));

    QStringList ports;
    for (int p : host.openPorts)
        ports << QStringLiteral("%1 (%2)").arg(p).arg(core::serviceName(p));
    m_table->setItem(row, 5, new QTableWidgetItem(ports.join(QStringLiteral(", "))));

    QStringList extra;
    if (!host.web.isEmpty())
        extra << host.web.first() + (host.webTitle.isEmpty()
                                         ? QString()
                                         : QStringLiteral(" — ") + host.webTitle);
    if (host.hasShares())
        extra << QStringLiteral("\\\\%1: %2").arg(host.ip, host.shares.join(QStringLiteral(", ")));
    auto *extraItem = new QTableWidgetItem(extra.join(QStringLiteral(" · ")));
    if (!host.web.isEmpty())
        extraItem->setData(Qt::UserRole, host.web.first());
    m_table->setItem(row, 6, extraItem);
    m_table->setSortingEnabled(true);
}

void NetscanDialog::startScan()
{
    if (m_task) {
        m_bridge->cancel(m_task);
        m_task = nullptr;
    }
    m_hosts.clear();
    m_table->setRowCount(0);

    core::ScanOptions opts;
    opts.targets = core::parseTargets(m_targets->text());
    if (opts.targets.isEmpty()) {
        m_status->setText(_t("Keine gültigen Ziele."));
        return;
    }
    const QString preset = m_portPreset->currentData().toString();
    opts.ports = (preset == QLatin1String("custom"))
                     ? core::parsePorts(m_customPorts->text())
                     : core::portPresets().value(preset);
    opts.ping = m_ping->isChecked();
    opts.onlyAlive = m_onlyAlive->isChecked();
    opts.resolveNames = m_resolve->isChecked();
    opts.detectShares = m_shares->isChecked();
    opts.identify = m_identify->isChecked();

    m_progress->setRange(0, opts.targets.size());
    m_progress->setValue(0);
    m_status->setText(QStringLiteral("Scanne %1 Ziele …").arg(opts.targets.size()));
    m_startBtn->setEnabled(false);

    // Treffer landen in einem geteilten Puffer; ueber den Line-Kanal kommt nur
    // der Index bzw. der Fortschritt (die HostResults selbst bleiben komplett).
    auto buffer = std::make_shared<std::vector<core::HostResult>>();
    auto bufferMutex = std::make_shared<std::mutex>();

    m_task = m_bridge->stream(
        [opts, buffer, bufferMutex](const AsyncBridge::EmitLine &emitLine,
                                    const CancelTokenPtr &cancel) {
            core::scanEvents(
                opts,
                [&emitLine](int done, int total) {
                    emitLine(QStringLiteral("P/%1/%2").arg(done).arg(total));
                },
                [&emitLine, buffer, bufferMutex](const core::HostResult &host) {
                    int index;
                    {
                        std::lock_guard<std::mutex> lock(*bufferMutex);
                        buffer->push_back(host);
                        index = int(buffer->size()) - 1;
                    }
                    emitLine(QStringLiteral("H/%1").arg(index));
                },
                cancel);
        },
        [this, buffer, bufferMutex](const QString &line) {
            if (line.startsWith(QLatin1String("P/"))) {
                const QStringList parts = line.split(QLatin1Char('/'));
                if (parts.size() == 3)
                    m_progress->setValue(parts[1].toInt());
            } else if (line.startsWith(QLatin1String("H/"))) {
                const int index = line.mid(2).toInt();
                core::HostResult host;
                {
                    std::lock_guard<std::mutex> lock(*bufferMutex);
                    if (index < 0 || index >= int(buffer->size()))
                        return;
                    host = (*buffer)[index];
                }
                m_hosts.push_back(host);
                addHostRow(host);
            }
        },
        [this] {
            m_status->setText(QStringLiteral("Fertig — %1 Host(s)").arg(m_table->rowCount()));
            m_startBtn->setEnabled(true);
            m_task = nullptr;
        },
        [this](const QString &err) {
            m_status->setText(err);
            m_startBtn->setEnabled(true);
            m_task = nullptr;
        });
}

void NetscanDialog::stopScan()
{
    if (m_task) {
        m_bridge->cancel(m_task);
        m_status->setText(_t("Abgebrochen."));
        m_startBtn->setEnabled(true);
        m_task = nullptr;
    }
}

} // namespace ncssh::gui
