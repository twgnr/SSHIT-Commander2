#include "ncssh/gui/workspace.hpp"

#include "ncssh/core/i18n.hpp"
#include "ncssh/gui/console_panel.hpp"
#include "ncssh/gui/file_panel.hpp"
#include "ncssh/gui/transfer_manager.hpp"
#include "ncssh/net/transfer.hpp"

#include <algorithm>
#include <QDir>
#include <QMessageBox>
#include <QSplitter>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;

Workspace::Workspace(AsyncBridge *bridge, net::SessionManager *sessions,
                     TransferManager *transfers, QWidget *parent)
    : QWidget(parent), m_bridge(bridge), m_sessions(sessions), m_transfers(transfers)
{
    m_localFs = std::make_unique<core::LocalFileSystem>();
    m_localRunner = std::make_unique<core::LocalCommandRunner>();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    auto *columns = new QSplitter(Qt::Horizontal, this);

    // Linke Spalte: lokale Pane + Konsole
    auto *leftCol = new QSplitter(Qt::Vertical, columns);
    m_leftPanel = new FilePanel(bridge, _t("Lokal"), leftCol);
    m_leftConsole = new ConsolePanel(bridge, _t("Konsole (lokal)"), leftCol);
    leftCol->addWidget(m_leftPanel);
    leftCol->addWidget(m_leftConsole);
    leftCol->setStretchFactor(0, 3);
    leftCol->setStretchFactor(1, 2);

    // Rechte Spalte: remote Pane + Konsole (bis Connect ebenfalls lokal)
    auto *rightCol = new QSplitter(Qt::Vertical, columns);
    m_rightPanel = new FilePanel(bridge, _t("Remote (nicht verbunden)"), rightCol);
    m_rightConsole = new ConsolePanel(bridge, _t("Konsole (remote)"), rightCol);
    rightCol->addWidget(m_rightPanel);
    rightCol->addWidget(m_rightConsole);
    rightCol->setStretchFactor(0, 3);
    rightCol->setStretchFactor(1, 2);

    columns->addWidget(leftCol);
    columns->addWidget(rightCol);
    columns->setSizes({500, 500});
    layout->addWidget(columns);

    // Lokale Provider zuweisen
    m_leftPanel->setProvider(m_localFs.get());
    m_leftConsole->setRunner(m_localRunner.get(), QDir::homePath());
    m_rightPanel->setProvider(m_localFs.get());
    m_rightConsole->setRunner(m_localRunner.get(), QDir::homePath());

    // CWD-Sync: Pane -> Konsole
    connect(m_leftPanel, &FilePanel::pathChanged, m_leftConsole, &ConsolePanel::setCwd);
    connect(m_rightPanel, &FilePanel::pathChanged, m_rightConsole, &ConsolePanel::setCwd);
    // Konsole -> Pane
    connect(m_leftConsole, &ConsolePanel::cwdChanged, m_leftPanel, &FilePanel::navigateTo);
    connect(m_rightConsole, &ConsolePanel::cwdChanged, m_rightPanel, &FilePanel::navigateTo);

    // Statusmeldungen weiterreichen
    for (FilePanel *p : {m_leftPanel, m_rightPanel})
        connect(p, &FilePanel::statusMessage, this, &Workspace::statusMessage);
    for (ConsolePanel *c : {m_leftConsole, m_rightConsole})
        connect(c, &ConsolePanel::statusMessage, this, &Workspace::statusMessage);

    // Aktive Seite merken (bestimmt Ziel von Befehlspalette/Werkzeugen).
    connect(m_leftPanel, &FilePanel::activated, this, [this] { m_rightActive = false; });
    connect(m_rightPanel, &FilePanel::activated, this, [this] { m_rightActive = true; });
    connect(m_leftConsole, &ConsolePanel::activated, this, [this] { m_rightActive = false; });
    connect(m_rightConsole, &ConsolePanel::activated, this, [this] { m_rightActive = true; });

    // Transfer: F5 aus einer Pane -> in das Verzeichnis der anderen Pane.
    connect(m_leftPanel, &FilePanel::transferRequested, this, [this](const QString &src) {
        startTransfer(m_leftPanel->provider(), src, m_rightPanel->provider(),
                      m_rightPanel->currentPath());
    });
    connect(m_rightPanel, &FilePanel::transferRequested, this, [this](const QString &src) {
        startTransfer(m_rightPanel->provider(), src, m_leftPanel->provider(),
                      m_leftPanel->currentPath());
    });
}

Workspace::~Workspace()
{
    m_tunnels.stopAll();  // Weiterleitungen vor dem Sessionende schliessen
    if (m_session)
        m_sessions->close(m_session);
}

QString Workspace::connectionLabel() const
{
    return m_session ? m_session->label() : _t("Lokal");
}

QString Workspace::activeOsType() const
{
    if (m_rightActive && m_session)
        return m_session->osType;
#ifdef Q_OS_WIN
    return QStringLiteral("windows");
#else
    return QStringLiteral("posix");
#endif
}

void Workspace::sendToActiveConsole(const QString &command, bool execute)
{
    ConsolePanel *console = m_rightActive ? m_rightConsole : m_leftConsole;
    console->runCommand(command, execute);
}

FilePanel *Workspace::activePanel() const
{
    return m_rightActive ? m_rightPanel : m_leftPanel;
}

void Workspace::connectTo(const core::ServerProfile &profile)
{
    net::SessionManager *sessions = m_sessions;
    m_bridge->run<net::SSHSessionPtr>(
        [sessions, profile] { return sessions->open(profile); },
        [this, profile](const net::SSHSessionPtr &session) {
            m_session = session;
            m_remoteFs = session->filesystem();
            m_remoteRunner = session->runner();
            m_rightPanel->setHeaderTitle(session->label());
            // Lesezeichen getrennt je Verbindung (Profilname).
            m_rightPanel->setBookmarkKey(profile.name.isEmpty() ? session->label()
                                                                : profile.name);
            m_rightPanel->setProvider(m_remoteFs.get());
            // Konsole-CWD folgt spaeter dem Pane-Home.
            m_rightConsole->setRunner(m_remoteRunner.get(), QStringLiteral("."));
            m_rightConsole->setSession(session);  // Terminal-Modus nutzt die SSH-Shell
            emit statusMessage(QStringLiteral("Verbunden: %1 (%2)")
                                   .arg(session->label(), session->osType));
            if (session->hostKeyStatus == QLatin1String("unknown")) {
                const auto answer = QMessageBox::question(
                    this, _t("Unbekannter Host-Key"),
                    QStringLiteral("Fingerprint:\n%1\n\nVertrauen und speichern?")
                        .arg(session->hostFingerprint));
                if (answer == QMessageBox::Yes) {
                    m_sessions->hostkeys.add(profile.host, profile.port,
                                             session->hostFingerprint, session->hostKeyAlgo);
                    m_sessions->hostkeys.save();
                }
            }
            emit connectionChanged();
        },
        [this](const QString &err) {
            QMessageBox::critical(this, _t("Verbindungsfehler"), err);
            emit statusMessage(err);
        });
}

void Workspace::startTransfer(core::FileSystemProvider *src, const QString &srcPath,
                              core::FileSystemProvider *dst, const QString &dstDir)
{
    if (!src || !dst || srcPath.isEmpty() || dstDir.isEmpty())
        return;
    const QString name = src->basename(srcPath);
    const QString dstPath = dst->join(dstDir, name);
    // Ueber die Transfer-Queue: Fortschritt/Abbruch/Wiederholen im Dialog.
    const int jobId = m_transfers->enqueue(name, src, srcPath, dst, dstPath);
    emit statusMessage(QStringLiteral("Übertrage %1 …").arg(name));
    // Ziel-Pane nach Abschluss aktualisieren.
    connect(m_transfers, &TransferManager::jobUpdated, this,
            [this, jobId, dst](int id) {
                if (id != jobId)
                    return;
                const auto &jobs = m_transfers->jobs();
                auto it = std::find_if(jobs.begin(), jobs.end(),
                                       [jobId](const net::TransferJob &j) { return j.id == jobId; });
                if (it == jobs.end() || it->status == QLatin1String("running"))
                    return;
                if (it->status == QLatin1String("done")) {
                    emit statusMessage(QStringLiteral("Übertragen: %1").arg(it->name));
                    if (dst == m_leftPanel->provider())
                        m_leftPanel->refresh();
                    if (dst == m_rightPanel->provider())
                        m_rightPanel->refresh();
                } else if (!it->error.isEmpty()) {
                    emit statusMessage(it->error);
                }
            });
}

} // namespace ncssh::gui
