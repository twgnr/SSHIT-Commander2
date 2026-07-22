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
    void applyThemeByName(const QString &name);

    AsyncBridge *m_bridge;
    std::unique_ptr<net::SessionManager> m_sessions;
    QTabWidget *m_tabs = nullptr;
};

} // namespace ncssh::gui
