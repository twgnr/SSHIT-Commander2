// Server-Profile verwalten und eine Verbindung auswaehlen.
#pragma once

#include "ncssh/core/models.hpp"
#include "ncssh/core/profiles.hpp"
#include "ncssh/gui/bridge.hpp"

#include <QDialog>
#include <optional>

class QListWidget;
class QLineEdit;
class QComboBox;
class QSpinBox;
class QCheckBox;
class QLabel;
class QPushButton;

namespace ncssh::gui {

class ServerManagerDialog : public QDialog {
    Q_OBJECT
public:
    explicit ServerManagerDialog(AsyncBridge *bridge, QWidget *parent = nullptr);

    // Das zum Verbinden gewaehlte Profil (nach Accepted).
    std::optional<core::ServerProfile> chosen() const { return m_chosen; }

private:
    void reload();
    void loadIntoForm(const core::ServerProfile &p);
    core::ServerProfile formToProfile() const;
    void onSave();
    void onDelete();
    void onConnect();
    void onImport();
    // Reiner TCP-Test auf Host/Port — ohne SSH-Handshake.
    void testReachability();
    void pickTabColor();
    void updateColorButton();

    AsyncBridge *m_bridge;
    core::ProfileStore m_store;
    std::optional<core::ServerProfile> m_chosen;

    QLineEdit *m_filter = nullptr;
    QListWidget *m_list = nullptr;
    QLineEdit *m_name = nullptr;
    QLineEdit *m_host = nullptr;
    QSpinBox *m_port = nullptr;
    QLineEdit *m_user = nullptr;
    QComboBox *m_auth = nullptr;
    QLineEdit *m_keyPath = nullptr;
    QLineEdit *m_password = nullptr;
    QCheckBox *m_savePassword = nullptr;
    QComboBox *m_policy = nullptr;
    QLineEdit *m_proxyJump = nullptr;
    QLineEdit *m_startPath = nullptr;
    // Verbindungs-Feinsteuerung (#6) + Agent-Forwarding (#4)
    QSpinBox *m_keepalive = nullptr;
    QSpinBox *m_timeout = nullptr;
    QCheckBox *m_compression = nullptr;
    QCheckBox *m_agentFwd = nullptr;
    QLineEdit *m_ciphers = nullptr;
    QLineEdit *m_kex = nullptr;
    QPushButton *m_colorButton = nullptr;
    QString m_tabColor;
    QLabel *m_lastConnected = nullptr;
    QLabel *m_reachability = nullptr;
};

} // namespace ncssh::gui
