// Ein Arbeitsbereich (Tab): zwei Panes (lokal + remote) mit je eigener Konsole,
// gekoppelt an eine SSH-Verbindung.  (Port von gui/workspace.py, zusammengefasst)
#pragma once

#include "ncssh/core/filesystem.hpp"
#include "ncssh/core/runner.hpp"
#include "ncssh/gui/bridge.hpp"
#include "ncssh/gui/tunnel_dialog.hpp"
#include "ncssh/net/session.hpp"
#include "ncssh/net/ssh.hpp"
#include "ncssh/net/sudofs.hpp"

#include <QJsonObject>
#include <QWidget>
#include <memory>

namespace ncssh::gui {

class FilePanel;
class ConsolePanel;
class TransferManager;

class Workspace : public QWidget {
    Q_OBJECT
public:
    Workspace(AsyncBridge *bridge, net::SessionManager *sessions,
              TransferManager *transfers, QWidget *parent = nullptr);
    ~Workspace() override;

    // Verbindet die rechte Pane mit einem Server (asynchron).
    void connectTo(const core::ServerProfile &profile);
    bool isConnected() const { return static_cast<bool>(m_session); }
    QString connectionLabel() const;

    // OS der aktiven Seite ("posix"/"windows") — fuer die Befehlspalette.
    QString activeOsType() const;
    // Befehl in die aktive Konsole einfuegen bzw. ausfuehren.
    void sendToActiveConsole(const QString &command, bool execute);
    // Aktive Pane (fuer Werkzeuge wie Massen-Umbenennen).
    FilePanel *activePanel() const;
    FilePanel *leftPanel() const { return m_leftPanel; }
    FilePanel *rightPanel() const { return m_rightPanel; }

    // Aktive SSH-Sitzung (leer wenn nicht verbunden) — fuer Tunnel/sudo.
    net::SSHSessionPtr session() const { return m_session; }
    TunnelManager *tunnels() { return &m_tunnels; }

    // Zustand des Tabs fuer Tab-Favoriten / Sitzungswiederherstellung.
    QJsonObject toJson() const;
    void restoreFrom(const QJsonObject &state);

signals:
    void statusMessage(const QString &msg);
    void connectionChanged();

private:
    void startTransfer(core::FileSystemProvider *src, const QString &srcPath,
                       core::FileSystemProvider *dst, const QString &dstDir);
    void setSudoMode(bool on);

    AsyncBridge *m_bridge;
    net::SessionManager *m_sessions;
    TransferManager *m_transfers;

    // Lokale Seite (immer verfuegbar).
    std::unique_ptr<core::LocalFileSystem> m_localFs;
    std::unique_ptr<core::LocalCommandRunner> m_localRunner;

    // Remote-Seite (nach Connect).
    net::SSHSessionPtr m_session;
    std::unique_ptr<net::SFTPFileSystem> m_remoteFs;
    std::unique_ptr<net::SudoFileSystem> m_sudoFs;   // aktiv bei sudo-Chip
    std::unique_ptr<net::RemoteCommandRunner> m_remoteRunner;

    FilePanel *m_leftPanel = nullptr;
    FilePanel *m_rightPanel = nullptr;
    ConsolePanel *m_leftConsole = nullptr;
    ConsolePanel *m_rightConsole = nullptr;
    bool m_rightActive = false;  // zuletzt fokussierte Seite
    TunnelManager m_tunnels;     // offene Port-Weiterleitungen dieser Sitzung
};

} // namespace ncssh::gui
