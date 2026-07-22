// Hauptfenster: Tabs (Arbeitsbereiche), Menues, Toolbar, Statusleiste.
// (Port von gui/main_window.py, funktional zusammengefasst)
#pragma once

#include "ncssh/gui/bridge.hpp"
#include "ncssh/net/session.hpp"

#include <QMainWindow>
#include <memory>

class QTabWidget;

namespace ncssh::gui {

class Workspace;
class TransferManager;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(AsyncBridge *bridge, QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    void buildMenus();
    Workspace *currentWorkspace() const;
    Workspace *addTab();
    void openServerManager();
    void openTransfers();
    void openCommandPalette();
    void openHistory();
    void openSearch(const QString &mode);
    void openBulkRename();
    void openFileDiff();
    void openSettings();
    void openKnownHosts();
    void openTunnels();
    void applyThemeByName(const QString &name);

    AsyncBridge *m_bridge;
    std::unique_ptr<net::SessionManager> m_sessions;
    TransferManager *m_transfers = nullptr;
    QTabWidget *m_tabs = nullptr;
};

} // namespace ncssh::gui
