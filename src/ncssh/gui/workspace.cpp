#include "ncssh/gui/workspace.hpp"

#include "ncssh/core/i18n.hpp"
#include "ncssh/core/profiles.hpp"
#include "ncssh/gui/confirm_dialog.hpp"
#include "ncssh/gui/console_panel.hpp"
#include "ncssh/gui/dir_chooser.hpp"
#include "ncssh/gui/file_panel.hpp"
#include "ncssh/gui/host_key_dialog.hpp"
#include "ncssh/gui/preview_panel.hpp"
#include "ncssh/gui/transfer_manager.hpp"
#include "ncssh/net/transfer.hpp"

#include <algorithm>
#include <QDir>
#include <QEvent>
#include <QInputDialog>
#include <QLineEdit>
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
    m_leftColumn = leftCol;
    m_leftPanel = new FilePanel(bridge, _t("Lokal"), leftCol);
    m_leftPreview = new PreviewPanel(bridge, leftCol);
    m_leftPreview->setVisible(false);   // ueber Ansicht -> Vorschau einblendbar
    m_leftConsole = new ConsolePanel(bridge, _t("Konsole (lokal)"), leftCol);
    leftCol->addWidget(m_leftPanel);
    leftCol->addWidget(m_leftPreview);
    leftCol->addWidget(m_leftConsole);
    leftCol->setStretchFactor(0, 3);
    leftCol->setStretchFactor(2, 2);

    // Rechte Spalte: remote Pane + Konsole (bis Connect ebenfalls lokal)
    auto *rightCol = new QSplitter(Qt::Vertical, columns);
    m_rightColumn = rightCol;
    m_rightPanel = new FilePanel(bridge, _t("Remote (nicht verbunden)"), rightCol);
    m_rightPreview = new PreviewPanel(bridge, rightCol);
    m_rightPreview->setVisible(false);
    m_rightConsole = new ConsolePanel(bridge, _t("Konsole (remote)"), rightCol);
    rightCol->addWidget(m_rightPanel);
    rightCol->addWidget(m_rightPreview);
    rightCol->addWidget(m_rightConsole);
    rightCol->setStretchFactor(0, 3);
    rightCol->setStretchFactor(2, 2);

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

    // sudo-Modus der rechten Pane: auf das sudo-Dateisystem umschalten.
    connect(m_rightPanel, &FilePanel::sudoToggled, this, &Workspace::setSudoMode);

    // Vorschau-Panel: zeigt die markierte Datei der jeweiligen Pane.
    connect(m_leftPanel, &FilePanel::selectionChanged, this, [this](const QString &path) {
        if (m_leftPreview->isVisible())
            m_leftPreview->preview(m_leftPanel->provider(), path);
    });
    connect(m_rightPanel, &FilePanel::selectionChanged, this, [this](const QString &path) {
        if (m_rightPreview->isVisible())
            m_rightPreview->preview(m_rightPanel->provider(), path);
    });

    // Abdocken/Andocken der Konsolen-Spalte.
    for (ConsolePanel *console : {m_leftConsole, m_rightConsole}) {
        connect(console, &ConsolePanel::undockRequested, this,
                [this, console] { undockConsole(console); });
        connect(console, &ConsolePanel::dockRequested, this,
                [this, console] { dockConsole(console); });
    }

    // Drag & Drop: Quelle ist die jeweils andere Pane (bzw. der Explorer).
    connect(m_leftPanel, &FilePanel::filesDropped, this,
            [this](const QStringList &paths, bool fromExplorer) {
                core::FileSystemProvider *src = fromExplorer ? m_localFs.get()
                                                             : m_rightPanel->provider();
                for (const QString &p : paths)
                    startTransfer(src, p, m_leftPanel->provider(),
                                  m_leftPanel->currentPath());
            });
    connect(m_rightPanel, &FilePanel::filesDropped, this,
            [this](const QStringList &paths, bool fromExplorer) {
                core::FileSystemProvider *src = fromExplorer ? m_localFs.get()
                                                             : m_leftPanel->provider();
                for (const QString &p : paths)
                    startTransfer(src, p, m_rightPanel->provider(),
                                  m_rightPanel->currentPath());
            });

    // Transfer: F5 aus einer Pane -> in das Verzeichnis der anderen Pane.
    connect(m_leftPanel, &FilePanel::transferRequested, this, [this](const QString &) {
        confirmAndTransfer(m_leftPanel->provider(), m_leftPanel->selectedPaths(),
                           m_rightPanel->provider(), m_rightPanel->currentPath());
    });
    connect(m_rightPanel, &FilePanel::transferRequested, this, [this](const QString &) {
        confirmAndTransfer(m_rightPanel->provider(), m_rightPanel->selectedPaths(),
                           m_leftPanel->provider(), m_leftPanel->currentPath());
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

void Workspace::setPreviewVisible(bool visible)
{
    m_leftPreview->setVisible(visible);
    m_rightPreview->setVisible(visible);
    if (visible) {
        // Aktuelle Auswahl gleich anzeigen.
        m_leftPreview->preview(m_leftPanel->provider(), m_leftPanel->selectedPath());
        m_rightPreview->preview(m_rightPanel->provider(), m_rightPanel->selectedPath());
    } else {
        m_leftPreview->clearPreview();
        m_rightPreview->clearPreview();
    }
}

bool Workspace::previewVisible() const
{
    return m_leftPreview->isVisible();
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

void Workspace::showNetworkHosts(const std::vector<core::HostResult> &hosts)
{
    // Netzwerk-Provider anlegen bzw. aktualisieren und in der aktiven Pane zeigen.
    if (!m_netFs)
        m_netFs = std::make_unique<core::NetworkScanProvider>(hosts);
    else
        m_netFs->setHosts(hosts);
    FilePanel *panel = activePanel();
    panel->setHeaderTitle(_t("Netzwerk"));
    panel->setBookmarkKey(QStringLiteral("network"));
    panel->setProvider(m_netFs.get(), QStringLiteral("net://"));
    emit statusMessage(QStringLiteral("%1 Host(s) im Netzwerk-Modus").arg(hosts.size()));
}

void Workspace::undockConsole(ConsolePanel *console)
{
    if (m_floatingConsoles.contains(console))
        return;
    // Eigenes Fenster als Container; die Pane fuellt danach die Spalte.
    auto *window = new QWidget(this, Qt::Window);
    window->setObjectName(QStringLiteral("FloatingConsole"));
    window->setWindowTitle(console == m_leftConsole ? _t("Konsole (lokal)")
                                                    : _t("Konsole (remote)"));
    auto *layout = new QVBoxLayout(window);
    layout->setContentsMargins(4, 4, 4, 4);
    console->setParent(window);
    layout->addWidget(console);
    console->setDocked(false);
    console->show();
    window->resize(760, 420);
    window->show();
    // Fenster schliessen = andocken.
    window->installEventFilter(this);
    m_floatingConsoles.insert(console, window);
}

void Workspace::dockConsole(ConsolePanel *console)
{
    QWidget *window = m_floatingConsoles.value(console, nullptr);
    if (!window)
        return;
    QSplitter *column = (console == m_leftConsole) ? m_leftColumn : m_rightColumn;
    console->setParent(column);
    column->addWidget(console);
    console->setDocked(true);
    console->show();
    m_floatingConsoles.remove(console);
    window->deleteLater();
}

bool Workspace::eventFilter(QObject *obj, QEvent *event)
{
    // Wird ein abgedocktes Fenster geschlossen, die Konsole zurueckholen.
    if (event->type() == QEvent::Close) {
        for (auto it = m_floatingConsoles.begin(); it != m_floatingConsoles.end(); ++it) {
            if (it.value() == obj) {
                ConsolePanel *console = it.key();
                // Direkt andocken; das Fenster wird dabei geloescht.
                QMetaObject::invokeMethod(this, [this, console] { dockConsole(console); },
                                          Qt::QueuedConnection);
                event->ignore();
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

void Workspace::setSudoMode(bool on)
{
    if (!m_session || !m_remoteFs)
        return;
    const QString keepPath = m_rightPanel->currentPath();
    if (!on) {
        m_rightPanel->setProvider(m_remoteFs.get(), keepPath);
        m_sudoFs.reset();
        emit statusMessage(_t("sudo-Modus aus."));
        return;
    }

    // NOPASSWD pruefen; sonst das Passwort einmal erfragen (nur im RAM halten).
    net::SSHSessionPtr session = m_session;
    m_bridge->run<bool>(
        [session] { return net::sudoNeedsPassword(session); },
        [this, session, keepPath](bool needsPassword) {
            if (needsPassword && !session->sudoPassword) {
                bool ok = false;
                const QString password = QInputDialog::getText(
                    this, _t("sudo-Passwort"),
                    _t("Passwort für sudo (wird nur im Speicher gehalten):"),
                    QLineEdit::Password, QString(), &ok);
                if (!ok || password.isEmpty()) {
                    m_rightPanel->setSudoAvailable(true);  // Chip zuruecksetzen
                    return;
                }
                if (!net::verifySudoPassword(session, password)) {
                    QMessageBox::warning(this, _t("sudo"), _t("Passwort abgelehnt."));
                    return;
                }
                session->sudoPassword = password;
            }
            m_sudoFs = std::make_unique<net::SudoFileSystem>(m_remoteFs.get(), session);
            m_rightPanel->setProvider(m_sudoFs.get(), keepPath);
            emit statusMessage(_t("sudo-Modus aktiv — Operationen laufen als root."));
        },
        [this](const QString &err) { QMessageBox::warning(this, _t("sudo"), err); });
}

QJsonObject Workspace::toJson() const
{
    // Nur der Profilname wird gesichert — Geheimnisse bleiben im Keyring.
    return QJsonObject{
        {QStringLiteral("profile"), m_session ? m_session->profile.name : QString()},
        {QStringLiteral("left_path"), m_leftPanel->currentPath()},
        {QStringLiteral("right_path"), m_rightPanel->currentPath()},
    };
}

void Workspace::restoreFrom(const QJsonObject &state)
{
    const QString leftPath = state.value(QStringLiteral("left_path")).toString();
    if (!leftPath.isEmpty())
        m_leftPanel->navigateTo(leftPath);

    const QString profileName = state.value(QStringLiteral("profile")).toString();
    if (profileName.isEmpty()) {
        const QString rightPath = state.value(QStringLiteral("right_path")).toString();
        if (!rightPath.isEmpty())
            m_rightPanel->navigateTo(rightPath);
        return;
    }
    // Verbindung aus dem gespeicherten Profil wiederherstellen.
    core::ProfileStore store;
    store.load();
    if (const auto profile = store.get(profileName)) {
        core::ServerProfile p = *profile;
        store.hydrate(p);  // Secrets aus dem Keyring nachladen
        const QString rightPath = state.value(QStringLiteral("right_path")).toString();
        if (!rightPath.isEmpty())
            p.startPath = rightPath;
        connectTo(p);
    }
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
            // sudo-Chip nur bei POSIX-Servern anbieten.
            m_rightPanel->setSudoAvailable(session->osType == QLatin1String("posix"));
            emit statusMessage(QStringLiteral("Verbunden: %1 (%2)")
                                   .arg(session->label(), session->osType));
            if (session->hostKeyStatus == QLatin1String("unknown")) {
                // TOFU: Fingerprint zeigen und auf Wunsch dauerhaft merken.
                if (HostKeyDialog::ask(profile.host, profile.port, session->hostKeyAlgo,
                                       session->hostFingerprint, this)) {
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

void Workspace::confirmAndTransfer(core::FileSystemProvider *src,
                                   const std::vector<QString> &srcPaths,
                                   core::FileSystemProvider *dst, const QString &dstDir)
{
    if (!src || !dst || srcPaths.empty())
        return;
    QStringList names, sources;
    for (const QString &p : srcPaths) {
        names << src->basename(p);
        sources << p;
    }
    // Zielordner ist vorbelegt, kann aber editiert oder durchsucht werden —
    // der Browser laeuft ueber den Provider, funktioniert also auch remote.
    TransferConfirmDialog dlg(
        _t("Kopieren / Übertragen"), _t("Kopieren"), names, sources,
        [dst](const QString &dir, const QString &name) { return dst->join(dir, name); },
        dstDir,
        [this, dst](const QString &current) -> QString {
            DirChooserDialog chooser(m_bridge, dst, current, {}, this);
            return chooser.exec() == QDialog::Accepted ? chooser.chosen() : QString();
        },
        {}, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    for (const auto &[from, to] : dlg.results())
        startTransfer(src, from, dst, dst->parent(to), dst->basename(to));
}

void Workspace::startTransfer(core::FileSystemProvider *src, const QString &srcPath,
                              core::FileSystemProvider *dst, const QString &dstDir,
                              const QString &overrideName)
{
    if (!src || !dst || srcPath.isEmpty() || dstDir.isEmpty())
        return;
    const QString name = overrideName.isEmpty() ? src->basename(srcPath) : overrideName;
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
