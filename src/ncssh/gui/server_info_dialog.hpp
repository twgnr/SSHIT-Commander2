// Server-Info: Eckdaten des verbundenen Servers (System, Benutzer,
// Konfigurationsdateien, Ports, Dienste, Speicher). Konfigurationsdateien
// lassen sich per Doppelklick direkt im Editor oeffnen.
#pragma once

#include "ncssh/core/serverinfo.hpp"
#include "ncssh/gui/bridge.hpp"
#include "ncssh/net/ssh.hpp"

#include <QDialog>
#include <functional>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

namespace ncssh::gui {

class ServerInfoDialog : public QDialog {
    Q_OBJECT
public:
    // openFile: oeffnet einen Pfad des Servers im Editor (der Workspace waehlt
    // dafuer das passende Dateisystem, bei aktivem sudo das sudo-Dateisystem).
    ServerInfoDialog(AsyncBridge *bridge, net::SSHSessionPtr session,
                     std::function<void(const QString &path)> openFile,
                     QWidget *parent = nullptr);

    // Ergebnis direkt anzeigen (auch fuer Tests ohne Server).
    void showInfo(const core::ServerInfo &info);

private:
    void reload();
    void fillUsers();
    void applyFileFilter();
    void openSelectedFile();

    AsyncBridge *m_bridge;
    net::SSHSessionPtr m_session;
    std::function<void(const QString &)> m_openFile;
    core::ServerInfo m_info;

    QLabel *m_status = nullptr;
    QLabel *m_hostname = nullptr;
    QLabel *m_os = nullptr;
    QLabel *m_kernel = nullptr;
    QLabel *m_uptime = nullptr;
    QLabel *m_identity = nullptr;
    QLineEdit *m_fileFilter = nullptr;
    QTableWidget *m_files = nullptr;
    QTableWidget *m_users = nullptr;
    QCheckBox *m_systemUsers = nullptr;
    QPlainTextEdit *m_ports = nullptr;
    QPlainTextEdit *m_services = nullptr;
    QPlainTextEdit *m_storage = nullptr;
    QPushButton *m_reloadBtn = nullptr;
    QPushButton *m_openBtn = nullptr;
};

} // namespace ncssh::gui
