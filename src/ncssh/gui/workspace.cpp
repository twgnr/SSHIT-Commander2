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
#include <QAbstractButton>
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
    m_columns = columns;
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

    // Verschieben in die andere Pane (Kontextmenue).
    connect(m_leftPanel, &FilePanel::moveRequested, this, [this](const QString &) {
        confirmAndMove(m_leftPanel->provider(), m_leftPanel->selectedPaths(),
                       m_rightPanel->provider(), m_rightPanel->currentPath());
    });
    connect(m_rightPanel, &FilePanel::moveRequested, this, [this](const QString &) {
        confirmAndMove(m_rightPanel->provider(), m_rightPanel->selectedPaths(),
                       m_leftPanel->provider(), m_leftPanel->currentPath());
    });

    // Einfuegen aus der internen Zwischenablage (Strg+V) — Ziel ist diese Pane.
    connect(m_leftPanel, &FilePanel::pasteRequested, this,
            [this](bool move) { pasteInto(m_leftPanel, move); });
    connect(m_rightPanel, &FilePanel::pasteRequested, this,
            [this](bool move) { pasteInto(m_rightPanel, move); });

    // Verzeichnis-Vergleich beider Panes.
    for (FilePanel *p : {m_leftPanel, m_rightPanel})
        connect(p, &FilePanel::dirDiffRequested, this, &Workspace::dirDiffRequested);

    // Netzwerk-Modus: Aktionen aus dem Host-Kontextmenue.
    for (FilePanel *p : {m_leftPanel, m_rightPanel}) {
        connect(p, &FilePanel::rescanRequested, this, &Workspace::rescanRequested);
        connect(p, &FilePanel::connectToHostRequested, this,
                [this](const QString &host) {
                    // Profil aus dem Host bauen; Zugangsdaten erfragt das
                    // Hauptfenster ueber connectHostRequested.
                    emit connectHostRequested(host);
                });
        connect(p, &FilePanel::exitNetworkModeRequested, this, [this, p] {
            // Zurueck auf das lokale Dateisystem der jeweiligen Seite.
            p->setHeaderTitle(p == m_leftPanel ? _t("Lokal")
                                               : (m_session ? m_session->label()
                                                            : _t("Remote (nicht verbunden)")));
            p->setBookmarkKey(p == m_leftPanel ? QStringLiteral("local")
                                               : QStringLiteral("remote"));
            core::FileSystemProvider *back =
                (p == m_rightPanel && m_remoteFs) ? static_cast<core::FileSystemProvider *>(
                                                        m_remoteFs.get())
                                                  : m_localFs.get();
            p->setProvider(back, back == m_localFs.get() ? QDir::homePath() : QString());
        });
    }
}

void Workspace::pasteInto(FilePanel *target, bool move)
{
    core::FileSystemProvider *src = FilePanel::clipboardProvider();
    const QStringList paths = FilePanel::clipboardPaths();
    if (!src || paths.isEmpty() || !target || !target->provider())
        return;
    std::vector<QString> list(paths.begin(), paths.end());
    if (move)
        confirmAndMove(src, list, target->provider(), target->currentPath());
    else
        confirmAndTransfer(src, list, target->provider(), target->currentPath());
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

void Workspace::disconnectSession()
{
    if (!m_session)
        return;
    m_tunnels.stopAll();          // Weiterleitungen zuerst schliessen
    m_sessions->close(m_session);
    m_session.reset();
    // Reihenfolge zaehlt: erst die Verbraucher abhaengen, dann die Objekte
    // freigeben — sonst zeigt eine Pane noch auf ein totes Dateisystem.
    m_rightPanel->setSudoAvailable(false);
    m_rightConsole->setSession({});
    m_rightConsole->setRunner(m_localRunner.get(), QDir::homePath());
    m_rightPanel->setHeaderTitle(_t("Remote (nicht verbunden)"));
    m_rightPanel->setProvider(m_localFs.get(), QDir::homePath());
    m_sudoFs.reset();
    m_remoteRunner.reset();
    m_remoteFs.reset();
    emit connectionChanged();
}

void Workspace::explainActiveConsoleWithAi()
{
    ConsolePanel *console = m_rightActive ? m_rightConsole : m_leftConsole;
    console->explainWithAi();
}

void Workspace::broadcastToConsoles(const QString &command, bool execute)
{
    m_leftConsole->runCommand(command, execute);
    m_rightConsole->runCommand(command, execute);
}

// --- Ansichts-Modi ----------------------------------------------------------

void Workspace::setOnlyFilesystem(bool on)
{
    m_onlyFilesystem = on;
    if (on)
        m_onlyTerminal = false;
    // Konsolen aus-/einblenden; abgedockte Konsolen bleiben unberuehrt.
    for (ConsolePanel *console : {m_leftConsole, m_rightConsole}) {
        if (console->parentWidget() == m_leftColumn || console->parentWidget() == m_rightColumn)
            console->setVisible(!on);
    }
    for (FilePanel *panel : {m_leftPanel, m_rightPanel})
        panel->setVisible(true);
}

void Workspace::setOnlyTerminal(bool on)
{
    m_onlyTerminal = on;
    if (on)
        m_onlyFilesystem = false;
    for (FilePanel *panel : {m_leftPanel, m_rightPanel})
        panel->setVisible(!on);
    for (ConsolePanel *console : {m_leftConsole, m_rightConsole}) {
        if (console->parentWidget() == m_leftColumn || console->parentWidget() == m_rightColumn)
            console->setVisible(true);
    }
    if (on)
        setPreviewVisible(false);
}

void Workspace::setPanesVertical(bool vertical)
{
    if (m_columns)
        m_columns->setOrientation(vertical ? Qt::Vertical : Qt::Horizontal);
}

bool Workspace::panesVertical() const
{
    return m_columns && m_columns->orientation() == Qt::Vertical;
}

void Workspace::swapPanes()
{
    // Nur die Inhalte tauschen — die Widgets bleiben, wo sie sind, damit
    // Provider-Zuordnung und Konsolen-Kopplung stimmig bleiben.
    core::FileSystemProvider *leftProvider = m_leftPanel->provider();
    core::FileSystemProvider *rightProvider = m_rightPanel->provider();
    const QString leftPath = m_leftPanel->currentPath();
    const QString rightPath = m_rightPanel->currentPath();
    if (!leftProvider || !rightProvider)
        return;
    m_leftPanel->setProvider(rightProvider, rightPath);
    m_rightPanel->setProvider(leftProvider, leftPath);
}

void Workspace::syncPanes()
{
    // Inaktive Seite auf das Verzeichnis der aktiven bringen.
    FilePanel *from = activePanel();
    FilePanel *to = (from == m_leftPanel) ? m_rightPanel : m_leftPanel;
    if (from->currentPath().isEmpty())
        return;
    to->navigateTo(from->currentPath());
}

FilePanel *Workspace::activePanel() const
{
    return m_rightActive ? m_rightPanel : m_leftPanel;
}

void Workspace::showNetworkHosts(const std::vector<core::HostResult> &hosts,
                                 const QString &side)
{
    // Netzwerk-Provider anlegen bzw. aktualisieren und in der gewuenschten Pane
    // zeigen (leer = aktive Seite).
    if (!m_netFs)
        m_netFs = std::make_unique<core::NetworkScanProvider>(hosts);
    else
        m_netFs->setHosts(hosts);
    FilePanel *panel = side == QLatin1String("left")    ? m_leftPanel
                       : side == QLatin1String("right") ? m_rightPanel
                                                        : activePanel();
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
        [this, profile](const QString &err) {
            // Geaenderter Host-Key: der Versuch wurde vor der Authentifizierung
            // abgebrochen. Erst nach ausdruecklicher Zustimmung erneut versuchen.
            const net::HostKeyMismatch mismatch = m_sessions->lastMismatch();
            if (mismatch.valid) {
                m_sessions->clearMismatch();
                if (HostKeyDialog::askChanged(mismatch.host, mismatch.port,
                                              mismatch.algorithm, mismatch.expected,
                                              mismatch.received, this)) {
                    m_sessions->hostkeys.add(mismatch.host, mismatch.port,
                                             mismatch.received, mismatch.algorithm);
                    m_sessions->hostkeys.save();
                    connectTo(profile);   // jetzt passt der Pin
                } else {
                    emit statusMessage(_t("Abgebrochen — Host-Key nicht bestätigt."));
                }
                return;
            }
            QMessageBox::critical(this, _t("Verbindung fehlgeschlagen"), err);
            emit statusMessage(_t("Verbindung fehlgeschlagen"));
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
    withConflictCheck(dst, dstDir, dlg.results(),
                      [this, src, dst](const std::vector<std::pair<QString, QString>> &todo) {
                          for (const auto &[from, to] : todo)
                              startTransfer(src, from, dst, dst->parent(to), dst->basename(to));
                      });
}

// Listet das Zielverzeichnis, fragt fuer vorhandene Namen das Ueberschreiben ab
// und ruft dann fortfahren() mit den freigegebenen Eintraegen. Ohne diese
// Pruefung wuerde eine Uebertragung Zieldateien wortlos ersetzen.
void Workspace::withConflictCheck(
    core::FileSystemProvider *dst, const QString &targetDir,
    const std::vector<std::pair<QString, QString>> &results,
    const std::function<void(const std::vector<std::pair<QString, QString>> &)> &then)
{
    m_bridge->run<std::vector<core::FileEntry>>(
        [dst, targetDir] { return dst->listDir(targetDir); },
        [this, dst, targetDir, results, then](const std::vector<core::FileEntry> &entries) {
            QSet<QString> existing;
            for (const core::FileEntry &e : entries)
                existing.insert(e.name);
            bool cancelled = false;
            const auto todo = resolveOverwrites(dst->label, targetDir, results, existing,
                                                cancelled);
            if (cancelled)
                return;
            if (todo.empty()) {
                emit statusMessage(_t("Nichts zu übertragen (alle übersprungen)."));
                return;
            }
            then(todo);
        },
        [this](const QString &err) {
            QMessageBox::critical(this, _t("Fehler"), err);
        });
}

std::vector<std::pair<QString, QString>> Workspace::resolveOverwrites(
    const QString &dstLabel, const QString &targetDir,
    const std::vector<std::pair<QString, QString>> &results,
    const QSet<QString> &existing, bool &cancelled)
{
    std::vector<std::pair<QString, QString>> todo;
    bool overwriteAll = false;
    bool skipAll = false;
    cancelled = false;

    for (const auto &[from, to] : results) {
        const QString name = to.mid(to.lastIndexOf(QLatin1Char('/')) + 1);
        if (!existing.contains(name)) {
            todo.emplace_back(from, to);
            continue;
        }
        if (overwriteAll) {
            todo.emplace_back(from, to);
            continue;
        }
        if (skipAll)
            continue;

        QMessageBox box(this);
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(_t("Überschreiben?"));
        box.setText(_t("„%1“ existiert bereits in [%2] %3.\n\nÜberschreiben?")
                        .arg(name, dstLabel, targetDir));
        box.setStandardButtons(QMessageBox::Yes | QMessageBox::YesToAll | QMessageBox::No
                               | QMessageBox::NoToAll | QMessageBox::Cancel);
        box.setDefaultButton(QMessageBox::No);   // im Zweifel nichts ueberschreiben
        box.button(QMessageBox::Yes)->setText(_t("Ja"));
        box.button(QMessageBox::YesToAll)->setText(_t("Ja, alle"));
        box.button(QMessageBox::No)->setText(_t("Nein"));
        box.button(QMessageBox::NoToAll)->setText(_t("Nein, alle"));
        box.button(QMessageBox::Cancel)->setText(_t("Abbrechen"));

        switch (box.exec()) {
        case QMessageBox::Cancel:
            cancelled = true;
            return {};
        case QMessageBox::YesToAll:
            overwriteAll = true;
            todo.emplace_back(from, to);
            break;
        case QMessageBox::Yes:
            todo.emplace_back(from, to);
            break;
        case QMessageBox::NoToAll:
            skipAll = true;
            break;
        default:
            break;   // Nein -> diesen Eintrag ueberspringen
        }
    }
    return todo;
}

void Workspace::startTransfer(core::FileSystemProvider *src, const QString &srcPath,
                              core::FileSystemProvider *dst, const QString &dstDir,
                              const QString &overrideName, bool moveSource)
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
            [this, jobId, src, srcPath, dst, moveSource](int id) {
                if (id != jobId)
                    return;
                const auto &jobs = m_transfers->jobs();
                auto it = std::find_if(jobs.begin(), jobs.end(),
                                       [jobId](const net::TransferJob &j) { return j.id == jobId; });
                if (it == jobs.end() || it->status == QLatin1String("running"))
                    return;
                if (it->status == QLatin1String("done")) {
                    emit statusMessage(QStringLiteral("Übertragen: %1").arg(it->name));
                    // Beim Verschieben die Quelle erst nach erfolgreicher
                    // Uebertragung entfernen — nie vorher.
                    if (moveSource) {
                        m_bridge->run(
                            [src, srcPath] { src->remove(srcPath, true); },
                            [this, src] {
                                if (src == m_leftPanel->provider()) m_leftPanel->refresh();
                                if (src == m_rightPanel->provider()) m_rightPanel->refresh();
                            },
                            [this](const QString &err) { emit statusMessage(err); });
                    }
                    if (dst == m_leftPanel->provider())
                        m_leftPanel->refresh();
                    if (dst == m_rightPanel->provider())
                        m_rightPanel->refresh();
                } else if (!it->error.isEmpty()) {
                    emit statusMessage(it->error);
                }
            });
}

void Workspace::confirmAndMove(core::FileSystemProvider *src,
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
    TransferConfirmDialog dlg(
        _t("Verschieben"), _t("Verschieben"), names, sources,
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
        startTransfer(src, from, dst, dst->parent(to), dst->basename(to), /*moveSource=*/true);
}

} // namespace ncssh::gui
