#include "ncssh/gui/main_window.hpp"

#include "ncssh/core/assets.hpp"
#include "ncssh/core/i18n.hpp"
#include "ncssh/core/settings.hpp"
#include "ncssh/gui/server_manager.hpp"
#include "ncssh/gui/style.hpp"
#include "ncssh/gui/workspace.hpp"

#include <QApplication>
#include <QIcon>
#include <QMenuBar>
#include <QMessageBox>
#include <QPixmap>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>

namespace ncssh::gui {

using core::_t;

MainWindow::MainWindow(AsyncBridge *bridge, QWidget *parent)
    : QMainWindow(parent), m_bridge(bridge)
{
    m_sessions = std::make_unique<net::SessionManager>();
    m_sessions->hostkeys.load();

    setWindowTitle(QStringLiteral("SSHIT-Commander"));
    const QString iconPath = core::assetPath(QStringLiteral("sshit.png"));
    if (!iconPath.isEmpty())
        setWindowIcon(QIcon(QPixmap(iconPath)));
    resize(1180, 760);

    m_tabs = new QTabWidget(this);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
        if (m_tabs->count() > 1) {
            QWidget *w = m_tabs->widget(index);
            m_tabs->removeTab(index);
            w->deleteLater();
        }
    });
    setCentralWidget(m_tabs);

    buildMenus();
    statusBar()->showMessage(_t("Bereit."));
    addTab();
}

MainWindow::~MainWindow()
{
    if (m_sessions)
        m_sessions->closeAll();
}

void MainWindow::buildMenus()
{
    // --- Aktionen ---
    QMenu *actions = menuBar()->addMenu(_t("Aktionen"));
    QAction *connectAct = actions->addAction(_t("SSH verbinden"), this,
                                             &MainWindow::openServerManager);
    connectAct->setShortcut(QKeySequence(Qt::Key_F9));
    QAction *newTabAct = actions->addAction(_t("Neuer Tab"), this, [this] { addTab(); });
    newTabAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+N")));
    actions->addSeparator();
    QAction *quitAct = actions->addAction(_t("Beenden"), this, &QWidget::close);
    quitAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q")));

    // --- Ansicht: Theme ---
    QMenu *view = menuBar()->addMenu(_t("Ansicht"));
    QMenu *themeMenu = view->addMenu(_t("Theme"));
    for (const QString &name : themeNames()) {
        themeMenu->addAction(name, this, [this, name] { applyThemeByName(name); });
    }

    // --- Hilfe ---
    QMenu *help = menuBar()->addMenu(_t("Hilfe"));
    help->addAction(_t("Über"), this, [this] {
        QMessageBox::about(this, QStringLiteral("SSHIT-Commander"),
                           QStringLiteral("<b>SSHIT-Commander</b> (C++/Qt6-Port)<br>"
                                          "Dual-Pane-Dateimanager mit SSH/SFTP-Terminal.<br>"
                                          "Backend: libssh2."));
    });

    // --- Toolbar ---
    auto *toolbar = addToolBar(_t("Haupt"));
    toolbar->setMovable(false);
    toolbar->addAction(_t("★ Verbinden"), this, &MainWindow::openServerManager);
    toolbar->addAction(_t("➕ Tab"), this, [this] { addTab(); });
}

Workspace *MainWindow::addTab()
{
    auto *ws = new Workspace(m_bridge, m_sessions.get(), this);
    connect(ws, &Workspace::statusMessage, this,
            [this](const QString &msg) { statusBar()->showMessage(msg, 8000); });
    const int index = m_tabs->addTab(ws, _t("Sitzung"));
    connect(ws, &Workspace::connectionChanged, this, [this, ws] {
        const int i = m_tabs->indexOf(ws);
        if (i >= 0)
            m_tabs->setTabText(i, ws->connectionLabel());
    });
    m_tabs->setCurrentIndex(index);
    return ws;
}

Workspace *MainWindow::currentWorkspace() const
{
    return qobject_cast<Workspace *>(m_tabs->currentWidget());
}

void MainWindow::openServerManager()
{
    ServerManagerDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted && dlg.chosen()) {
        Workspace *ws = currentWorkspace();
        if (ws)
            ws->connectTo(*dlg.chosen());
    }
}

void MainWindow::applyThemeByName(const QString &name)
{
    applyTheme(qApp, name);
    core::setSetting(QStringLiteral("theme"), name);
}

} // namespace ncssh::gui
