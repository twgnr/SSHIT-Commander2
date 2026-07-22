// Sicherheits-Audit (CVE): liest OS/Kernel und Pakete eines verbundenen Linux-
// Servers aus, ermittelt offene Sicherheitsupdates (apt/dnf), prueft sshd/ufw/
// Konten und gleicht Kernkomponenten online gegen OSV.dev ab.
// (Port von gui/security_dialog.py)
#pragma once

#include "ncssh/gui/bridge.hpp"
#include "ncssh/net/ssh.hpp"

#include <QDialog>

class QTreeWidget;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;

namespace ncssh::gui {

class SecurityDialog : public QDialog {
    Q_OBJECT
public:
    SecurityDialog(AsyncBridge *bridge, net::SSHSessionPtr session,
                   QWidget *parent = nullptr);

private:
    void runAudit();

    AsyncBridge *m_bridge;
    net::SSHSessionPtr m_session;
    BridgeTask *m_task = nullptr;

    QTreeWidget *m_tree = nullptr;
    QPlainTextEdit *m_details = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_runBtn = nullptr;
};

} // namespace ncssh::gui
