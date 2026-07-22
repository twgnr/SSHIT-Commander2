#include "ncssh/gui/tunnel_dialog.hpp"

#include "ncssh/core/i18n.hpp"
#include "ncssh/core/profiles.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;
using core::TunnelSpec;

void TunnelManager::add(std::unique_ptr<net::Tunnel> tunnel)
{
    m_tunnels.push_back(std::move(tunnel));
}

void TunnelManager::stopAt(int index)
{
    if (index < 0 || index >= int(m_tunnels.size()))
        return;
    m_tunnels[index]->stop();
    m_tunnels.erase(m_tunnels.begin() + index);
}

void TunnelManager::stopAll()
{
    for (auto &tunnel : m_tunnels)
        tunnel->stop();
    m_tunnels.clear();
}

TunnelDialog::TunnelDialog(net::SSHSessionPtr session, TunnelManager *manager, QWidget *parent)
    : QDialog(parent), m_session(std::move(session)), m_manager(manager)
{
    setWindowTitle(_t("SSH-Tunnel"));
    resize(720, 480);

    auto *layout = new QVBoxLayout(this);

    // Ohne Verbindung laesst sich kein Tunnel oeffnen — das gleich sagen.
    if (!m_session) {
        auto *warning = new QLabel(
            _t("⚠ Keine aktive SSH-Verbindung — bitte zuerst per F9 verbinden."), this);
        warning->setWordWrap(true);
        layout->addWidget(warning);
    }

    layout->addWidget(new QLabel(_t("Aktive Weiterleitungen"), this));
    m_table = new QTableWidget(0, 2, this);
    m_table->setHorizontalHeaderLabels({_t("Art"), _t("Listen/Ziel")});
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_table, 1);

    auto *box = new QGroupBox(
        m_session ? _t("Neue Weiterleitung über: %1").arg(m_session->label())
                  : _t("Neue Weiterleitung"),
        this);
    auto *form = new QFormLayout(box);
    m_kind = new QComboBox(box);
    m_kind->addItem(_t("Lokal (-L)"), QStringLiteral("local"));
    m_kind->addItem(_t("Remote (-R)"), QStringLiteral("remote"));
    m_kind->addItem(_t("Dynamisch / SOCKS (-D)"), QStringLiteral("dynamic"));
    connect(m_kind, &QComboBox::currentIndexChanged, this, [this] {
        const bool dynamic = m_kind->currentData().toString() == QLatin1String("dynamic");
        m_destHost->setEnabled(!dynamic);
        m_destPort->setEnabled(!dynamic);
    });
    m_listenHost = new QLineEdit(QStringLiteral("127.0.0.1"), box);
    m_listenPort = new QSpinBox(box);
    m_listenPort->setRange(1, 65535);
    m_listenPort->setValue(8080);
    m_destHost = new QLineEdit(QStringLiteral("localhost"), box);
    m_destHost->setPlaceholderText(_t("z.B. localhost oder DB-Host"));
    m_destPort = new QSpinBox(box);
    m_destPort->setRange(1, 65535);
    m_destPort->setValue(80);
    m_saveToProfile = new QCheckBox(
        _t("Im Server-Profil speichern (Auto-Start beim Verbinden)"), box);
    form->addRow(_t("Art"), m_kind);
    form->addRow(_t("Listen-Host"), m_listenHost);
    form->addRow(_t("Listen-Port"), m_listenPort);
    form->addRow(_t("Ziel-Host"), m_destHost);
    form->addRow(_t("Ziel-Port"), m_destPort);
    form->addRow(QString(), m_saveToProfile);
    layout->addWidget(box);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("Muted"));
    layout->addWidget(m_status);

    auto *buttons = new QHBoxLayout();
    auto *openBtn = new QPushButton(_t("Öffnen"), this);
    openBtn->setDefault(true);
    auto *stopBtn = new QPushButton(_t("Stoppen"), this);
    auto *closeBtn = new QPushButton(_t("Schließen"), this);
    connect(openBtn, &QPushButton::clicked, this, &TunnelDialog::openTunnel);
    connect(stopBtn, &QPushButton::clicked, this, &TunnelDialog::stopSelected);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(openBtn);
    buttons->addWidget(stopBtn);
    buttons->addStretch(1);
    buttons->addWidget(closeBtn);
    layout->addLayout(buttons);

    reload();
}

void TunnelDialog::reload()
{
    m_table->setRowCount(0);
    int row = 0;
    for (const auto &tunnel : m_manager->tunnels()) {
        m_table->insertRow(row);
        m_table->setItem(row, 0, new QTableWidgetItem(tunnel->spec().kind));
        m_table->setItem(row, 1, new QTableWidgetItem(tunnel->spec().label()));
        ++row;
    }
}

void TunnelDialog::openTunnel()
{
    if (!m_session) {
        m_status->setText(
            _t("⚠ Keine aktive SSH-Verbindung — bitte zuerst per F9 verbinden."));
        return;
    }
    TunnelSpec spec;
    spec.kind = m_kind->currentData().toString();
    spec.listenHost = m_listenHost->text().trimmed();
    spec.listenPort = m_listenPort->value();
    spec.destHost = m_destHost->text().trimmed();
    spec.destPort = m_destPort->value();

    m_status->setText(_t("startet…"));
    try {
        m_manager->add(net::openTunnel(m_session, spec));
        m_status->setText(QStringLiteral("✓ %1").arg(spec.label()));
        if (m_saveToProfile->isChecked())
            saveToProfile(spec);
        reload();
    } catch (const std::exception &exc) {
        QMessageBox::warning(this, _t("Tunnel-Fehler"), QString::fromUtf8(exc.what()));
        m_status->setText(QString::fromUtf8(exc.what()));
    }
}

// Legt die Weiterleitung im Server-Profil ab, damit sie beim naechsten
// Verbinden automatisch startet.
void TunnelDialog::saveToProfile(const TunnelSpec &spec)
{
    core::ProfileStore store;
    store.load();
    const QString label = m_session->label();
    for (const auto &profile : store.profiles()) {
        // Profil ueber Host/Port zuordnen — der Anzeigename kann abweichen.
        if (!label.contains(profile.host))
            continue;
        core::ServerProfile updated = profile;
        for (const auto &existing : updated.tunnels) {
            if (existing.kind == spec.kind && existing.listenPort == spec.listenPort)
                return;   // schon hinterlegt
        }
        updated.tunnels.push_back(spec);
        store.upsert(updated);
        store.save();
        m_status->setText(_t("✓ %1").arg(spec.label()) + QStringLiteral(" · ")
                          + _t("im Profil „%1“ gespeichert").arg(updated.name));
        return;
    }
    m_status->setText(_t("Kein passendes Server-Profil gefunden."));
}

void TunnelDialog::stopSelected()
{
    const int row = m_table->currentRow();
    if (row < 0)
        return;
    m_manager->stopAt(row);
    reload();
    m_status->setText(_t("Tunnel gestoppt."));
}

} // namespace ncssh::gui
