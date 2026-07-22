// SSH-Tunnel / Port-Forwarding: lokal (-L), remote (-R) und dynamisch/SOCKS (-D)
// oeffnen und stoppen.  (Port von gui/tunnel_dialog.py + tunnel_manager.py)
#pragma once

#include "ncssh/core/models.hpp"
#include "ncssh/net/ssh.hpp"
#include "ncssh/net/tunnels.hpp"

#include <QDialog>
#include <memory>
#include <vector>

class QTableWidget;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QLabel;

namespace ncssh::gui {

// Haelt die offenen Tunnel einer Sitzung am Leben.
class TunnelManager {
public:
    void add(std::unique_ptr<net::Tunnel> tunnel);
    void stopAt(int index);
    void stopAll();
    const std::vector<std::unique_ptr<net::Tunnel>> &tunnels() const { return m_tunnels; }

private:
    std::vector<std::unique_ptr<net::Tunnel>> m_tunnels;
};

class TunnelDialog : public QDialog {
    Q_OBJECT
public:
    TunnelDialog(net::SSHSessionPtr session, TunnelManager *manager,
                 QWidget *parent = nullptr);

private:
    void reload();
    void openTunnel();
    void stopSelected();

    net::SSHSessionPtr m_session;
    TunnelManager *m_manager;

    QTableWidget *m_table = nullptr;
    QComboBox *m_kind = nullptr;
    QLineEdit *m_listenHost = nullptr;
    QSpinBox *m_listenPort = nullptr;
    QLineEdit *m_destHost = nullptr;
    QSpinBox *m_destPort = nullptr;
    QLabel *m_status = nullptr;
};

} // namespace ncssh::gui
