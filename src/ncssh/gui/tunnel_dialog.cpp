#include "ncssh/gui/tunnel_dialog.hpp"

#include "ncssh/core/i18n.hpp"

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

    m_table = new QTableWidget(0, 2, this);
    m_table->setHorizontalHeaderLabels({_t("Art"), _t("Weiterleitung")});
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_table, 1);

    auto *box = new QGroupBox(_t("Neuen Tunnel öffnen"), this);
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
    m_destPort = new QSpinBox(box);
    m_destPort->setRange(1, 65535);
    m_destPort->setValue(80);
    form->addRow(_t("Art"), m_kind);
    form->addRow(_t("Lauscht auf"), m_listenHost);
    form->addRow(_t("Lokaler Port"), m_listenPort);
    form->addRow(_t("Ziel-Host"), m_destHost);
    form->addRow(_t("Ziel-Port"), m_destPort);
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
    TunnelSpec spec;
    spec.kind = m_kind->currentData().toString();
    spec.listenHost = m_listenHost->text().trimmed();
    spec.listenPort = m_listenPort->value();
    spec.destHost = m_destHost->text().trimmed();
    spec.destPort = m_destPort->value();

    try {
        m_manager->add(net::openTunnel(m_session, spec));
        m_status->setText(QStringLiteral("✓ %1").arg(spec.label()));
        reload();
    } catch (const std::exception &exc) {
        QMessageBox::warning(this, _t("Tunnel-Fehler"), QString::fromUtf8(exc.what()));
        m_status->setText(QString::fromUtf8(exc.what()));
    }
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
