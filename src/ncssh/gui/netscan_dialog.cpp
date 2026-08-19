#include "ncssh/gui/netscan_dialog.hpp"

#include "ncssh/core/i18n.hpp"
#include "ncssh/core/settings.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariantMap>
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
    m_targets->setPlaceholderText(_t("CIDR, Bereich oder Komma-Liste von IPs/Hostnamen"));
    connect(m_targets, &QLineEdit::returnPressed, this, &NetscanDialog::startScan);
    form->addRow(_t("IP-Range"), m_targets);

    auto *portRow = new QHBoxLayout();
    m_portPreset = new QComboBox(this);
    m_portPreset->addItem(_t("Häufige Ports"), QStringLiteral("common"));
    m_portPreset->addItem(_t("Nur SMB (139,445)"), QStringLiteral("smb"));
    m_portPreset->addItem(_t("Web (80,443,…)"), QStringLiteral("web"));
    m_portPreset->addItem(_t("Fernzugriff (22,3389,…)"), QStringLiteral("remote"));
    m_portPreset->addItem(_t("Alle wichtigen"), QStringLiteral("all"));
    m_portPreset->addItem(_t("Eigene"), QStringLiteral("custom"));
    m_customPorts = new QLineEdit(this);
    m_customPorts->setPlaceholderText(_t("Ports und Bereiche, z. B. 22,80,443,1-1024"));
    m_customPorts->setEnabled(false);
    connect(m_portPreset, &QComboBox::currentIndexChanged, this, [this] {
        m_customPorts->setEnabled(m_portPreset->currentData().toString()
                                  == QLatin1String("custom"));
    });
    portRow->addWidget(m_portPreset);
    portRow->addWidget(m_customPorts, 1);
    form->addRow(_t("Port-Vorauswahl"), portRow);
    form->addRow(_t("Ports / Bereich"), new QLabel(QString(), this));

    auto *optRow = new QHBoxLayout();
    m_ping = new QCheckBox(_t("Ping (ICMP) zusätzlich"), this);
    m_ping->setChecked(true);
    m_onlyAlive = new QCheckBox(_t("Nur antwortende Hosts anzeigen"), this);
    m_onlyAlive->setChecked(true);
    m_resolve = new QCheckBox(_t("Hostnamen auflösen (Reverse-DNS)"), this);
    m_resolve->setChecked(true);
    m_shares = new QCheckBox(_t("Freigaben erkennen (SMB)"), this);
    m_shares->setChecked(true);
    for (QCheckBox *c : {m_ping, m_onlyAlive, m_resolve, m_shares})
        optRow->addWidget(c);
    optRow->addStretch(1);
    form->addRow(_t("Optionen"), optRow);

    auto *opt2Row = new QHBoxLayout();
    m_identify = new QCheckBox(_t("Geräte identifizieren (Banner, Web-Titel, OS, NetBIOS)"),
                               this);
    m_identify->setChecked(true);
    m_detectMac = new QCheckBox(_t("MAC-Adresse + Hersteller (ARP/OUI)"), this);
    m_detectMac->setChecked(true);
    opt2Row->addWidget(m_identify);
    opt2Row->addWidget(m_detectMac);
    opt2Row->addStretch(1);
    form->addRow(QString(), opt2Row);

    // Leistung: gleichzeitige Verbindungen und Wartezeit je Port.
    auto *perfRow = new QHBoxLayout();
    m_concurrency = new QSpinBox(this);
    m_concurrency->setRange(1, 1000);
    m_concurrency->setValue(100);
    m_timeout = new QDoubleSpinBox(this);
    m_timeout->setRange(0.1, 10.0);
    m_timeout->setSingleStep(0.1);
    m_timeout->setValue(0.5);
    m_timeout->setSuffix(QStringLiteral(" s"));
    perfRow->addWidget(new QLabel(_t("Parallel"), this));
    perfRow->addWidget(m_concurrency);
    perfRow->addWidget(new QLabel(_t("Timeout je Port"), this));
    perfRow->addWidget(m_timeout);
    // Wiederholtes Scannen im Hintergrund (0 = aus).
    m_autoRescan = new QSpinBox(this);
    m_autoRescan->setRange(0, 3600);
    m_autoRescan->setSuffix(QStringLiteral(" s"));
    m_autoRescan->setSpecialValueText(_t("aus"));
    m_autoRescan->setToolTip(_t("Host-Liste automatisch in diesem Intervall neu scannen"));
    perfRow->addWidget(new QLabel(_t("Auto-Rescan"), this));
    perfRow->addWidget(m_autoRescan);
    perfRow->addStretch(1);
    form->addRow(QString(), perfRow);

    // Zielpane fuer die Uebernahme der Ergebnisse.
    m_targetPane = new QComboBox(this);
    // Vorgabe ist die aktive (blau umrandete) Pane — dieselbe Regel wie beim
    // Verbinden. Ohne diesen Eintrag landeten die Hosts immer links, egal was
    // markiert war.
    m_targetPane->addItem(_t("Aktive Pane"), QString());
    m_targetPane->addItem(_t("Linke Pane"), QStringLiteral("left"));
    m_targetPane->addItem(_t("Rechte Pane"), QStringLiteral("right"));
    form->addRow(_t("Ergebnisse in"), m_targetPane);
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
    auto *stopBtn = new QPushButton(_t("Stop"), this);
    auto *lastBtn = new QPushButton(_t("Letzten Scan laden"), this);
    auto *closeBtn = new QPushButton(_t("Schließen"), this);
    connect(m_startBtn, &QPushButton::clicked, this, &NetscanDialog::startScan);
    connect(stopBtn, &QPushButton::clicked, this, &NetscanDialog::stopScan);
    connect(lastBtn, &QPushButton::clicked, this, &NetscanDialog::loadLastScan);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(m_startBtn);
    buttons->addWidget(stopBtn);
    buttons->addWidget(lastBtn);
    buttons->addStretch(1);
    buttons->addWidget(closeBtn);
    layout->addLayout(buttons);

    // Auto-Rescan: laeuft nur, wenn ein Intervall gesetzt ist und gerade kein
    // Scan aktiv ist — sonst wuerden sich die Laeufe ueberholen.
    m_rescanTimer = new QTimer(this);
    connect(m_rescanTimer, &QTimer::timeout, this, [this] {
        if (!m_task)
            startScan();
    });
    connect(m_autoRescan, &QSpinBox::valueChanged, this, [this](int seconds) {
        if (seconds > 0)
            m_rescanTimer->start(seconds * 1000);
        else
            m_rescanTimer->stop();
    });
}

QString NetscanDialog::targetPane() const
{
    return m_targetPane->currentData().toString();
}

void NetscanDialog::saveLastScan() const
{
    QJsonArray array;
    for (const HostResult &host : m_hosts) {
        QJsonObject entry;
        entry.insert(QStringLiteral("ip"), host.ip);
        entry.insert(QStringLiteral("hostname"), host.hostname);
        entry.insert(QStringLiteral("netbios"), host.netbios);
        entry.insert(QStringLiteral("mac"), host.mac);
        entry.insert(QStringLiteral("vendor"), host.vendor);
        entry.insert(QStringLiteral("os"), host.osGuess);
        entry.insert(QStringLiteral("webTitle"), host.webTitle);
        QJsonArray ports;
        for (int p : host.openPorts)
            ports.append(p);
        entry.insert(QStringLiteral("ports"), ports);
        entry.insert(QStringLiteral("shares"), QJsonArray::fromStringList(host.shares));
        entry.insert(QStringLiteral("web"), QJsonArray::fromStringList(host.web));
        array.append(entry);
    }
    core::setSetting(QStringLiteral("netscan_last"), array);
    core::setSetting(QStringLiteral("netscan_last_targets"), m_targets->text());
}

void NetscanDialog::loadLastScan()
{
    const QVariantList saved = core::getSetting(QStringLiteral("netscan_last")).toList();
    if (saved.isEmpty()) {
        m_status->setText(_t("Kein gespeicherter Scan vorhanden."));
        return;
    }
    m_hosts.clear();
    m_table->setRowCount(0);
    for (const QVariant &value : saved) {
        const QVariantMap map = value.toMap();
        HostResult host;
        host.ip = map.value(QStringLiteral("ip")).toString();
        host.hostname = map.value(QStringLiteral("hostname")).toString();
        host.netbios = map.value(QStringLiteral("netbios")).toString();
        host.mac = map.value(QStringLiteral("mac")).toString();
        host.vendor = map.value(QStringLiteral("vendor")).toString();
        host.osGuess = map.value(QStringLiteral("os")).toString();
        host.webTitle = map.value(QStringLiteral("webTitle")).toString();
        for (const QVariant &p : map.value(QStringLiteral("ports")).toList())
            host.openPorts.append(p.toInt());
        host.shares = map.value(QStringLiteral("shares")).toStringList();
        host.web = map.value(QStringLiteral("web")).toStringList();
        host.alive = true;
        m_hosts.push_back(host);
        addHostRow(host);
    }
    const QString targets =
        core::getSettingString(QStringLiteral("netscan_last_targets"));
    if (!targets.isEmpty())
        m_targets->setText(targets);
    m_status->setText(_t("%1 Host(s) gefunden.").arg(m_hosts.size()));
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
        m_status->setText(_t("Bitte eine gültige IP-Range angeben."));
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
    opts.detectMac = m_detectMac->isChecked();
    opts.concurrency = m_concurrency->value();
    opts.timeout = m_timeout->value();
    m_status->setText(_t("Scanne %1 Adressen …").arg(opts.targets.size()));

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
                m_status->setText(_t("%1 Host(s) gefunden …").arg(m_hosts.size()));
            }
        },
        [this] {
            m_status->setText(_t("%1 Host(s) gefunden.").arg(m_hosts.size()));
            m_startBtn->setEnabled(true);
            m_task = nullptr;
            saveLastScan();   // damit "Letzten Scan laden" etwas findet
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
        m_status->setText(_t("%1 Host(s) gefunden.").arg(m_hosts.size())
                          + _t(" (abgebrochen)"));
        m_startBtn->setEnabled(true);
        m_task = nullptr;
    }
}

} // namespace ncssh::gui
