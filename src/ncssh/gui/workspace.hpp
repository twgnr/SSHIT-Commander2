// Ein Arbeitsbereich (Tab): zwei Panes (lokal + remote) mit je eigener Konsole,
// gekoppelt an eine SSH-Verbindung.  (Port von gui/workspace.py, zusammengefasst)
#pragma once

#include "ncssh/core/filesystem.hpp"
#include "ncssh/core/runner.hpp"
#include "ncssh/gui/bridge.hpp"
#include "ncssh/net/session.hpp"
#include "ncssh/net/ssh.hpp"

#include <QWidget>
#include <memory>

namespace ncssh::gui {

class FilePanel;
class ConsolePanel;

class Workspace : public QWidget {
    Q_OBJECT
public:
    Workspace(AsyncBridge *bridge, net::SessionManager *sessions, QWidget *parent = nullptr);
    ~Workspace() override;

    // Verbindet die rechte Pane mit einem Server (asynchron).
    void connectTo(const core::ServerProfile &profile);
    bool isConnected() const { return static_cast<bool>(m_session); }
    QString connectionLabel() const;

signals:
    void statusMessage(const QString &msg);
    void connectionChanged();

private:
    void startTransfer(core::FileSystemProvider *src, const QString &srcPath,
                       core::FileSystemProvider *dst, const QString &dstDir);

    AsyncBridge *m_bridge;
    net::SessionManager *m_sessions;

    // Lokale Seite (immer verfuegbar).
    std::unique_ptr<core::LocalFileSystem> m_localFs;
    std::unique_ptr<core::LocalCommandRunner> m_localRunner;

    // Remote-Seite (nach Connect).
    net::SSHSessionPtr m_session;
    std::unique_ptr<net::SFTPFileSystem> m_remoteFs;
    std::unique_ptr<net::RemoteCommandRunner> m_remoteRunner;

    FilePanel *m_leftPanel = nullptr;
    FilePanel *m_rightPanel = nullptr;
    ConsolePanel *m_leftConsole = nullptr;
    ConsolePanel *m_rightConsole = nullptr;
};

} // namespace ncssh::gui
