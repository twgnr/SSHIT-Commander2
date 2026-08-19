#include "ncssh/gui/workspace.hpp"

#include "ncssh/core/i18n.hpp"
#include "ncssh/core/profiles.hpp"
#include "ncssh/core/settings.hpp"
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
#include <QFileInfo>
#include <QJsonArray>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QAbstractButton>
#include <QPushButton>
#include <QSplitter>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;

// Lesezeichen-Gruppe einer Verbindung (Profilname, sonst user@host).
static QString bookmarkKeyFor(const net::SSHSessionPtr &session)
{
    if (!session)
        return QStringLiteral("local");
    return session->profile.name.isEmpty() ? session->label() : session->profile.name;
}

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
    m_leftConsole = new ConsolePanel(bridge, _t("Konsole (links)"), leftCol);
    leftCol->addWidget(m_leftPanel);
    leftCol->addWidget(m_leftPreview);
    leftCol->addWidget(m_leftConsole);
    leftCol->setStretchFactor(0, 3);
    leftCol->setStretchFactor(2, 2);

    // Rechte Spalte: zweite Pane + Konsole. Beide Seiten starten lokal; die
    // Verbindung landet spaeter in der Pane, die beim Verbinden aktiv ist —
    // die Beschriftung darf deshalb keine Seite als "remote" festschreiben.
    auto *rightCol = new QSplitter(Qt::Vertical, columns);
    m_rightColumn = rightCol;
    m_rightPanel = new FilePanel(bridge, _t("Lokal"), rightCol);
    m_rightPreview = new PreviewPanel(bridge, rightCol);
    m_rightPreview->setVisible(false);
    m_rightConsole = new ConsolePanel(bridge, _t("Konsole (rechts)"), rightCol);
    rightCol->addWidget(m_rightPanel);
    rightCol->addWidget(m_rightPreview);
    rightCol->addWidget(m_rightConsole);
    rightCol->setStretchFactor(0, 3);
    rightCol->setStretchFactor(2, 2);

    columns->addWidget(leftCol);
    columns->addWidget(rightCol);
    columns->setSizes({500, 500});
    m_columns = columns;
    // Gemerkte Pane-Ausrichtung uebernehmen (gilt auch fuer neue Tabs).
    if (core::getSettingString(QStringLiteral("pane_orientation"),
                               QStringLiteral("horizontal"))
        == QLatin1String("vertical"))
        columns->setOrientation(Qt::Vertical);
    layout->addWidget(columns);

    // Gemerkte Hoehenaufteilung (Pane/Vorschau/Konsole) je Spalte wieder-
    // herstellen und kuenftige Aenderungen speichern.
    const QVariantList savedSplits = core::getSetting(QStringLiteral("console_splits")).toList();
    const auto restoreColumn = [](QSplitter *col, const QVariantList &saved, int idx) {
        if (idx >= saved.size())
            return;
        const QVariantList entry = saved.at(idx).toList();
        if (entry.size() != col->count())   // Struktur (Anzahl Sektionen) muss passen
            return;
        QList<int> sizes;
        int sum = 0;
        for (const QVariant &v : entry) {
            sizes << v.toInt();
            sum += v.toInt();
        }
        if (sum > 0)
            col->setSizes(sizes);
    };
    restoreColumn(leftCol, savedSplits, 0);
    restoreColumn(rightCol, savedSplits, 1);
    for (QSplitter *col : {leftCol, rightCol})
        connect(col, &QSplitter::splitterMoved, this, [this] { saveConsoleSplits(); });

    // Lokale Provider zuweisen. Der konfigurierte Standard-Startpfad gewinnt
    // gegen das Home-Verzeichnis, sofern er (noch) existiert.
    const QString configuredStart = core::getSettingString(QStringLiteral("start_path"));
    const QString localStart =
        (!configuredStart.isEmpty() && QFileInfo(configuredStart).isDir()) ? configuredStart
                                                                           : QString();
    m_leftPanel->setProvider(m_localFs.get(), localStart);
    m_leftConsole->setRunner(m_localRunner.get(), QDir::homePath());
    m_leftConsole->setCompletionProvider(m_localFs.get());
    m_rightPanel->setProvider(m_localFs.get(), localStart);
    m_rightConsole->setRunner(m_localRunner.get(), QDir::homePath());
    m_rightConsole->setCompletionProvider(m_localFs.get());

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

    // Aktive Seite merken (bestimmt Ziel von Befehlspalette/Werkzeugen) und die
    // Pane/Konsole der aktiven Seite blau umranden.
    connect(m_leftPanel, &FilePanel::activated, this, [this] { m_rightActive = false; highlightActive(); });
    connect(m_rightPanel, &FilePanel::activated, this, [this] { m_rightActive = true; highlightActive(); });
    connect(m_leftConsole, &ConsolePanel::activated, this, [this] { m_rightActive = false; highlightActive(); });
    connect(m_rightConsole, &ConsolePanel::activated, this, [this] { m_rightActive = true; highlightActive(); });

    // Keine Verbindung -> keine verbundene Pane. connectTo() traegt sie ein;
    // eine Vorbelegung auf "rechts" waere eine Annahme, die nicht mehr gilt.
    m_connectedPanel = nullptr;
    m_connectedConsole = nullptr;

    // sudo-/Trennen-Chip beider Panes: wirkt auf die Pane mit der Verbindung.
    for (FilePanel *p : {m_leftPanel, m_rightPanel}) {
        connect(p, &FilePanel::sudoToggled, this, &Workspace::setSudoMode);
        connect(p, &FilePanel::disconnectRequested, this, [this] {
            if (m_session)
                disconnectSession();
        });
    }

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
            // Zurueck auf das Dateisystem, das diese Seite vor dem Netzwerk-
            // Modus hatte: die verbundene Pane bekommt ihr Remote-/sudo-
            // Dateisystem zurueck, jede andere das lokale. (Welche Seite
            // verbunden ist, entscheidet m_connectedPanel — nicht die Lage.)
            const bool connected = (p == m_connectedPanel && m_session && m_remoteFs);
            p->setHeaderTitle(connected ? m_session->label() : _t("Lokal"));
            p->setBookmarkKey(connected ? bookmarkKeyFor(m_session)
                                        : QStringLiteral("local"));
            // Chips wieder herstellen (showNetworkHosts hat sie abgeschaltet).
            p->setConnected(connected);
            p->setSudoAvailable(connected && m_session->osType == QLatin1String("posix"));
            core::FileSystemProvider *back = m_localFs.get();
            if (connected)
                back = m_sudoFs ? static_cast<core::FileSystemProvider *>(m_sudoFs.get())
                                : static_cast<core::FileSystemProvider *>(m_remoteFs.get());
            p->setProvider(back, back == m_localFs.get() ? QDir::homePath() : QString());
        });
    }

    // Startzustand: linke Pane ist aktiv -> gleich blau umranden.
    highlightActive();
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
    // Geordneter Abbau: erst die Terminal-Lesethreads stoppen und die
    // Weiterleitungen schliessen, DANN die Session — sonst arbeiten die
    // Threads auf freigegebenen libssh2-Objekten. Das Hauptfenster loescht
    // die Workspaces explizit, solange der SessionManager noch lebt.
    m_leftConsole->shutdownShell();
    m_rightConsole->shutdownShell();
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
    // Remote-OS nur melden, wenn die AKTIVE Seite auch die verbundene ist —
    // die Verbindung kann in jeder der beiden Panes liegen.
    if (m_session && activePanel() == m_connectedPanel)
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

// Prueft die Verbindung regelmaessig per Keepalive. Faellt sie weg, meldet das
// die Oberflaeche und bietet das Wiederverbinden an — sonst merkt man den
// Abbruch erst beim naechsten Klick, mit einer kryptischen Fehlermeldung.
void Workspace::startHealthCheck()
{
    if (!m_healthTimer) {
        m_healthTimer = new QTimer(this);
        m_healthTimer->setInterval(20000);
        connect(m_healthTimer, &QTimer::timeout, this, [this] {
            if (!m_session || m_session->closing || m_healthPending)
                return;
            m_healthPending = true;
            net::SSHSessionPtr session = m_session;
            m_bridge->run<bool>(
                [session] { return session->sendKeepalive(); },
                [this, session](bool alive) {
                    m_healthPending = false;
                    // Zwischenzeitlich getrennt oder ersetzt -> nichts tun.
                    if (alive || m_session != session || session->closing)
                        return;
                    const core::ServerProfile profile = session->profile;
                    // Wiederverbinden in DERSELBEN Pane — nicht in der gerade
                    // aktiven, der Fokus kann inzwischen woanders liegen.
                    FilePanel *panel = m_connectedPanel;
                    m_healthTimer->stop();
                    emit statusMessage(
                        _t("Verbindung verloren — verbinde neu: %1").arg(profile.display()));
                    disconnectSession();
                    connectTo(profile, panel);
                },
                [this](const QString &) { m_healthPending = false; });
        });
    }
    m_healthTimer->start();
}

void Workspace::disconnectSession()
{
    if (!m_session)
        return;
    if (m_healthTimer)
        m_healthTimer->stop();
    m_tunnels.stopAll();          // Weiterleitungen zuerst schliessen
    // Reihenfolge zaehlt: erst die Verbraucher abhaengen — insbesondere das
    // Terminal, dessen Lesethread sonst waehrend des Schliessens auf die
    // freigegebene libssh2-Session zugreift —, DANN die Session schliessen
    // und zuletzt die Provider-Objekte freigeben.
    // KEINE Seite raten: gibt es (wider Erwarten) keine verbundene Pane, wird
    // nur die Sitzung abgebaut. Ein Fallback auf "rechts" wuerde sonst die
    // falsche Seite zuruecksetzen und die echte auf einem toten Provider
    // stehen lassen.
    FilePanel *panel = m_connectedPanel;
    ConsolePanel *console = m_connectedConsole;
    if (panel) {
        panel->setSudoAvailable(false);
        panel->setConnected(false);
        // Die Pane ist wieder lokal — samt lokaler Lesezeichen-Gruppe.
        panel->setHeaderTitle(_t("Lokal"));
        panel->setBookmarkKey(QStringLiteral("local"));
        panel->setProvider(m_localFs.get(), QDir::homePath());
    }
    if (console) {
        console->setSession({});      // stoppt ein laufendes Remote-Terminal
        console->setRunner(m_localRunner.get(), QDir::homePath());
        console->setCompletionProvider(m_localFs.get());
    }
    m_sessions->close(m_session);
    m_session.reset();
    m_connectedPanel = nullptr;
    m_connectedConsole = nullptr;
    retireRemoteObjects();
    emit connectionChanged();
}

void Workspace::retireRemoteObjects()
{
    // Nicht zerstoeren, nur stilllegen — siehe m_retired in der Kopfdatei.
    RetiredRemote old;
    old.sudoFs = std::move(m_sudoFs);
    old.remoteFs = std::move(m_remoteFs);
    old.runner = std::move(m_remoteRunner);
    if (old.sudoFs || old.remoteFs || old.runner)
        m_retired.push_back(std::move(old));
    // Nicht unbegrenzt wachsen lassen: nach so vielen Verbindungswechseln ist
    // garantiert kein Dialog von damals mehr offen.
    constexpr size_t kKeep = 8;
    if (m_retired.size() > kKeep)
        m_retired.erase(m_retired.begin(), m_retired.end() - kKeep);
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
    // Die Verbindung haengt an der Pane — ihr Zubehoer (Titel, Chips,
    // Lesezeichen-Gruppe, Verbindungs-Zeiger) muss mitwandern, sonst wirken
    // Trennen/sudo anschliessend auf die falsche Seite.
    if (m_session && m_connectedPanel) {
        FilePanel *from = m_connectedPanel;
        FilePanel *to = (from == m_leftPanel) ? m_rightPanel : m_leftPanel;
        to->setHeaderTitle(m_session->label());
        to->setConnected(true);
        to->setSudoAvailable(m_session->osType == QLatin1String("posix"));
        to->setSudoActive(from->sudoActive());
        to->setBookmarkKey(bookmarkKeyFor(m_session));
        from->setHeaderTitle(_t("Lokal"));
        from->setConnected(false);
        from->setSudoAvailable(false);
        from->setSudoActive(false);
        from->setBookmarkKey(QStringLiteral("local"));
        m_connectedPanel = to;
        // Auch die Konsole der Verbindung wandert mit, sonst laeuft der
        // Remote-Runner weiter auf der Seite, die jetzt lokal ist. Die Seite
        // wird aus der verbundenen PANE abgeleitet (die den Zweig bewacht),
        // nicht aus m_connectedConsole — sonst waere "rechts" wieder geraten.
        ConsolePanel *fromConsole = (from == m_leftPanel) ? m_leftConsole : m_rightConsole;
        ConsolePanel *toConsole =
            (fromConsole == m_leftConsole) ? m_rightConsole : m_leftConsole;
        if (m_remoteRunner) {
            toConsole->setRunner(m_remoteRunner.get(), QStringLiteral("."));
            toConsole->setCompletionProvider(m_remoteFs.get());
            toConsole->setSession(m_session);
            fromConsole->setSession({});
            fromConsole->setRunner(m_localRunner.get(), QDir::homePath());
            fromConsole->setCompletionProvider(m_localFs.get());
            m_connectedConsole = toConsole;
        }
    }
}

void Workspace::syncPanes()
{
    // Inaktive Seite auf das Verzeichnis der aktiven bringen.
    FilePanel *from = activePanel();
    FilePanel *to = (from == m_leftPanel) ? m_rightPanel : m_leftPanel;
    if (from->currentPath().isEmpty())
        return;
    // Nur sinnvoll, wenn beide Seiten dasselbe Dateisystem zeigen: ein
    // Remote-Pfad im lokalen Dateisystem (oder umgekehrt) erzeugt sonst nur
    // eine Fehlermeldung.
    if (from->provider() != to->provider()) {
        emit statusMessage(
            _t("Angleichen geht nur, wenn beide Seiten dasselbe Dateisystem zeigen."));
        return;
    }
    to->navigateTo(from->currentPath());
}

FilePanel *Workspace::activePanel() const
{
    return m_rightActive ? m_rightPanel : m_leftPanel;
}

void Workspace::saveConsoleSplits()
{
    QJsonArray cols;
    for (QSplitter *col : {m_leftColumn, m_rightColumn}) {
        QJsonArray sizes;
        for (int s : col->sizes())
            sizes.append(s);
        cols.append(sizes);
    }
    core::setSetting(QStringLiteral("console_splits"), cols);
}

void Workspace::highlightActive()
{
    // Aktive Seite (Pane + Konsole) markieren, andere zuruecksetzen.
    for (auto *panel : {m_leftPanel, m_rightPanel}) {
        const bool active = (panel == m_rightPanel) == m_rightActive;
        if (panel->property("active").toBool() != active) {
            panel->setProperty("active", active);
            panel->style()->unpolish(panel);
            panel->style()->polish(panel);
        }
    }
    m_leftConsole->setActive(!m_rightActive);
    m_rightConsole->setActive(m_rightActive);
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
    // Trifft es die verbundene Pane, ist deren Remote-Ansicht weg: Chips und
    // Verbindungs-Zeiger duerfen dann nicht stehen bleiben, sonst wirkt der
    // sudo-Chip auf eine Pane, die gerade die Hostliste zeigt.
    if (panel == m_connectedPanel) {
        panel->setConnected(false);
        panel->setSudoAvailable(false);
        panel->setSudoActive(false);
    }
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
    window->setWindowTitle(console == m_leftConsole ? _t("Konsole (links)")
                                                    : _t("Konsole (rechts)"));
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
    // Ohne Verbindung gibt es keine verbundene Pane (m_connectedPanel ist dann
    // nullptr) — die Rueckmeldungen unten laufen asynchron und koennen eine
    // zwischenzeitliche Trennung erleben.
    if (!m_session || !m_remoteFs || !m_connectedPanel)
        return;
    FilePanel *const panel = m_connectedPanel;  // Pane mit der Verbindung
    const QString keepPath = panel->currentPath();
    if (!on) {
        panel->setProvider(m_remoteFs.get(), keepPath);
        m_sudoFs.reset();
        emit statusMessage(_t("sudo-Modus aus."));
        return;
    }

    // NOPASSWD pruefen; sonst das Passwort einmal erfragen (nur im RAM halten).
    net::SSHSessionPtr session = m_session;
    const QString host = session->label();
    m_bridge->run<bool>(
        [session] { return net::sudoNeedsPassword(session); },
        [this, session, keepPath, host](bool needsPassword) {
            // Zwischenzeitlich getrennt oder anderer Server -> nichts tun.
            if (m_session != session)
                return;
            if (!needsPassword || session->sudoPassword) {
                enableSudoFilesystem(keepPath);
                return;
            }
            bool ok = false;
            const QString password = QInputDialog::getText(
                this, _t("sudo-Passwort"), _t("sudo-Passwort für %1:").arg(host),
                QLineEdit::Password, QString(), &ok);
            if (!ok || password.isEmpty()) {
                if (m_connectedPanel) m_connectedPanel->setSudoActive(false);   // Chip zurueckstellen
                return;
            }
            // Pruefung geht ueber SSH — nicht im GUI-Thread, sonst haengt das
            // Fenster fuer die Dauer der Runde.
            m_bridge->run<bool>(
                [session, password] { return net::verifySudoPassword(session, password); },
                [this, session, password, keepPath](bool accepted) {
                    if (m_session != session)
                        return;
                    if (!accepted) {
                        QMessageBox::warning(this, _t("sudo"),
                                             _t("sudo-Authentifizierung fehlgeschlagen."));
                        if (m_connectedPanel) m_connectedPanel->setSudoActive(false);
                        return;
                    }
                    session->sudoPassword = password;
                    enableSudoFilesystem(keepPath);
                },
                [this](const QString &err) {
                    QMessageBox::warning(this, _t("sudo"), err);
                    if (m_connectedPanel) m_connectedPanel->setSudoActive(false);
                });
        },
        [this](const QString &err) {
            QMessageBox::warning(this, _t("sudo"), err);
            if (m_connectedPanel) m_connectedPanel->setSudoActive(false);
        });
}

void Workspace::enableSudoFilesystem(const QString &keepPath)
{
    // Der Weg hierher fuehrt ueber mehrere asynchrone Schritte (NOPASSWD-Test,
    // Passwortabfrage, Verifikation). In dieser Zeit kann der Nutzer getrennt
    // oder den Server gewechselt haben — dann gibt es keine verbundene Pane
    // und kein Remote-Dateisystem mehr.
    if (!m_session || !m_remoteFs || !m_connectedPanel)
        return;
    m_sudoFs = std::make_unique<net::SudoFileSystem>(m_remoteFs.get(), m_session);
    m_connectedPanel->setProvider(m_sudoFs.get(), keepPath);
    emit statusMessage(_t("sudo-Modus aktiv — Operationen laufen als root."));
}

QJsonObject Workspace::toJson() const
{
    // Nur der Profilname wird gesichert — Geheimnisse bleiben im Keyring.
    // connected_side merkt sich, welche Pane die Verbindung trug: nur deren
    // Pfad ist ein Remote-Pfad und darf beim Wiederherstellen nicht dem
    // lokalen Dateisystem vorgesetzt werden.
    return QJsonObject{
        {QStringLiteral("profile"), m_session ? m_session->profile.name : QString()},
        {QStringLiteral("connected_side"),
         m_session ? QString(m_connectedPanel == m_leftPanel ? QStringLiteral("left")
                                                             : QStringLiteral("right"))
                   : QString()},
        // Aktive Seite mitsichern: sonst steht der blaue Rahmen nach dem
        // Neustart links, waehrend die Verbindung rechts wiederhergestellt
        // wird — und der naechste Verbinden-Klick zielt auf die falsche Pane.
        {QStringLiteral("active_side"),
         m_rightActive ? QStringLiteral("right") : QStringLiteral("left")},
        {QStringLiteral("left_path"), m_leftPanel->currentPath()},
        {QStringLiteral("right_path"), m_rightPanel->currentPath()},
        {QStringLiteral("left_console_detached"), m_floatingConsoles.contains(m_leftConsole)},
        {QStringLiteral("right_console_detached"), m_floatingConsoles.contains(m_rightConsole)},
    };
}

// Liefert einen nutzbaren lokalen Startpfad fuer die Wiederherstellung: den
// gespeicherten, falls er existiert — sonst den konfigurierten
// Standard-Startpfad, sonst C:\. Faengt insbesondere gespeicherte Remote-
// Pfade (/home/…) und net://-Pfade ab, die nach einem Neustart sonst dem
// LOKALEN Dateisystem vorgesetzt wuerden und eine Fehlermeldung ausloesen.
static QString safeLocalPath(const QString &saved)
{
    if (!saved.isEmpty() && !saved.startsWith(QLatin1String("net://"))
        && QFileInfo(saved).isDir())
        return saved;
    const QString fallback = core::getSettingString(QStringLiteral("start_path"));
    if (!fallback.isEmpty() && QFileInfo(fallback).isDir())
        return fallback;
    return QStringLiteral("C:\\");
}

void Workspace::restoreFrom(const QJsonObject &state)
{
    // Zuvor abgedockte Konsolen wieder abdocken (nach dem Aufbau der Spalten).
    if (state.value(QStringLiteral("left_console_detached")).toBool())
        QTimer::singleShot(0, this, [this] { undockConsole(m_leftConsole); });
    if (state.value(QStringLiteral("right_console_detached")).toBool())
        QTimer::singleShot(0, this, [this] { undockConsole(m_rightConsole); });

    const QString leftPath = state.value(QStringLiteral("left_path")).toString();
    const QString rightPath = state.value(QStringLiteral("right_path")).toString();
    const QString profileName = state.value(QStringLiteral("profile")).toString();
    // Aeltere Sitzungsdaten kannten connected_side noch nicht; damals lag die
    // Verbindung immer rechts.
    const QString side = state.value(QStringLiteral("connected_side")).toString();
    FilePanel *connPanel = (side == QLatin1String("left")) ? m_leftPanel : m_rightPanel;
    FilePanel *otherPanel = (connPanel == m_leftPanel) ? m_rightPanel : m_leftPanel;
    const QString connPath = (connPanel == m_leftPanel) ? leftPath : rightPath;
    const QString otherPath = (otherPanel == m_leftPanel) ? leftPath : rightPath;

    // Aktive Seite wiederherstellen (aeltere Sitzungen kannten das Feld nicht:
    // dann die verbundene Seite markieren, damit Markierung und Verbindung
    // zusammenpassen).
    const QString activeSide = state.value(QStringLiteral("active_side")).toString();
    if (!activeSide.isEmpty())
        m_rightActive = (activeSide == QLatin1String("right"));
    else if (!profileName.isEmpty())
        m_rightActive = (connPanel == m_rightPanel);
    highlightActive();

    // Die nicht verbundene Seite ist immer lokal.
    otherPanel->navigateTo(safeLocalPath(otherPath));

    const bool reconnect = !profileName.isEmpty()
                           && core::getSettingBool(QStringLiteral("auto_connect_last"), true);
    if (reconnect) {
        core::ProfileStore store;
        store.load();
        if (const auto profile = store.get(profileName)) {
            core::ServerProfile p = *profile;
            store.hydrate(p);  // Secrets aus dem Keyring nachladen
            if (!connPath.isEmpty())
                p.startPath = connPath;
            connectTo(p, connPanel, /*quiet=*/true);
            return;
        }
    }
    // Keine Verbindung (abgeschaltet/Profil weg): der gespeicherte Remote-Pfad
    // ist ohne Verbindung wertlos -> lokaler Ersatzpfad (C:\).
    connPanel->navigateTo(safeLocalPath(connPath));
}

void Workspace::connectTo(const core::ServerProfile &profile, FilePanel *target, bool quiet)
{
    // Zielpane JETZT festhalten — bis das Ergebnis eintrifft, kann sich der
    // Fokus aendern. Ohne explizites Ziel gilt ausnahmslos: die Verbindung
    // landet in der AKTIVEN Pane, also der blau umrandeten. Jede Sonderregel
    // ("beim ersten Mal rechts") widerspricht der Markierung und ueberrascht.
    FilePanel *panel = target ? target : activePanel();
    if (m_connecting) {
        emit statusMessage(_t("Es läuft bereits ein Verbindungsversuch."));
        return;
    }
    // Ein Tab traegt genau eine Verbindung. Zielt der Nutzer auf die andere
    // Pane, waehrend schon eine besteht, entscheidet er: neuer Tab (empfohlen,
    // die bestehende Verbindung bleibt) oder Verbindung ersetzen.
    if (m_session && panel != m_connectedPanel && !quiet) {
        QMessageBox box(QMessageBox::Question, _t("Bereits verbunden"),
                        _t("Dieser Tab ist bereits mit %1 verbunden. "
                           "Pro Tab ist eine SSH-Verbindung möglich.")
                            .arg(m_session->label()),
                        QMessageBox::NoButton, this);
        auto *newTab = box.addButton(_t("In neuem Tab verbinden"), QMessageBox::AcceptRole);
        auto *replace = box.addButton(_t("Verbindung ersetzen"), QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() == newTab) {
            emit connectInNewTabRequested(profile);
            return;
        }
        if (box.clickedButton() != replace)
            return;
    }
    ConsolePanel *console = (panel == m_rightPanel) ? m_rightConsole : m_leftConsole;
    m_connecting = true;

    net::SessionManager *sessions = m_sessions;
    m_bridge->run<net::SSHSessionPtr>(
        [sessions, profile] { return sessions->open(profile); },
        [this, profile, panel, console](const net::SSHSessionPtr &session) {
            m_connecting = false;
            // Erst NACH erfolgreichem Aufbau die alte Verbindung abbauen:
            // schlaegt der Aufbau fehl, bleibt die bestehende Sitzung nutzbar.
            // disconnectSession() haengt zudem die alte Pane vom Provider ab,
            // BEVOR m_remoteFs unten ersetzt (und damit zerstoert) wird —
            // sonst zeigte sie auf ein freigegebenes Dateisystem.
            if (m_session)
                disconnectSession();
            m_connectedPanel = panel;
            m_connectedConsole = console;
            m_session = session;
            m_remoteFs = session->filesystem();
            m_remoteRunner = session->runner();
            panel->setHeaderTitle(session->label());
            // Lesezeichen getrennt je Verbindung (Profilname).
            panel->setBookmarkKey(bookmarkKeyFor(session));
            // Startpfad des Profils respektieren (auch von der Sitzungs-
            // wiederherstellung gesetzt); leer -> Home des Servers.
            panel->setProvider(m_remoteFs.get(), profile.startPath);
            // Konsole-CWD folgt spaeter dem Pane-Home.
            console->setRunner(m_remoteRunner.get(), QStringLiteral("."));
            console->setCompletionProvider(m_remoteFs.get());  // Tab-Pfade vom Server
            console->setSession(session);  // Terminal-Modus nutzt die SSH-Shell
            // sudo-Chip nur bei POSIX-Servern anbieten; Trennen-Chip bei jeder
            // Verbindung.
            panel->setSudoAvailable(session->osType == QLatin1String("posix"));
            panel->setConnected(true);
            emit statusMessage(QStringLiteral("Verbunden: %1 (%2)")
                                   .arg(session->label(), session->osType));
            if (session->hostKeyStatus == QLatin1String("unknown")) {
                // TOFU: Fingerprint zeigen und auf Wunsch dauerhaft merken.
                if (HostKeyDialog::ask(profile.host, profile.port, session->hostKeyAlgo,
                                       session->hostFingerprint, this)) {
                    m_sessions->hostkeys.add(profile.host, profile.port,
                                             session->hostFingerprint, session->hostKeyAlgo);
                    m_sessions->hostkeys.save();
                    // Interop: den bestaetigten Key auch in OpenSSHs
                    // ~/.ssh/known_hosts eintragen, damit das System-ssh ihn kennt.
                    if (core::getSettingBool(QStringLiteral("openssh_known_hosts"), true))
                        net::addToOpenSshKnownHosts(session, profile.host, profile.port);
                }
            }
            startHealthCheck();
            emit connectionChanged();

            // Auto-Start: im Profil gespeicherte Tunnel-Presets automatisch
            // oeffnen. openTunnel() richtet die Weiterleitung ein (bindet den
            // lokalen Port bzw. fordert forward-listen an) und wirft bei Fehler
            // (Port belegt o.ae.) — je Preset einzeln abfangen, damit ein
            // Fehlschlag die uebrigen nicht verhindert.
            if (!profile.tunnels.empty()) {
                int opened = 0;
                QStringList tunnelErrors;
                for (const auto &spec : profile.tunnels) {
                    try {
                        m_tunnels.add(net::openTunnel(session, spec));
                        ++opened;
                    } catch (const std::exception &exc) {
                        tunnelErrors << QStringLiteral("%1 (%2)").arg(
                            spec.label(), QString::fromUtf8(exc.what()));
                    }
                }
                if (opened > 0)
                    emit statusMessage(_t("%1 Tunnel automatisch geöffnet").arg(opened));
                if (!tunnelErrors.isEmpty())
                    emit statusMessage(
                        _t("Tunnel nicht geöffnet: %1").arg(tunnelErrors.join(QStringLiteral("; "))));
            }
        },
        [this, profile, panel, quiet](const QString &err) {
            m_connecting = false;
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
                    connectTo(profile, panel, quiet);   // jetzt passt der Pin
                } else {
                    emit statusMessage(_t("Abgebrochen — Host-Key nicht bestätigt."));
                }
                return;
            }
            if (quiet) {
                // Sitzungswiederherstellung: kein modaler Dialog beim Start.
                // Die Pane bleibt lokal nutzbar (C:\ bzw. Standard-Startpfad).
                emit statusMessage(_t("Automatisches Verbinden fehlgeschlagen: %1").arg(err));
                panel->navigateTo(safeLocalPath(QString()));
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
    // Ziel-Pane nach Abschluss aktualisieren. Die Verbindung wird beim ersten
    // Endzustand wieder getrennt — sonst sammeln sich ueber die Laufzeit
    // beliebig viele Handler an, die bei JEDEM Job-Update mitlaufen.
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(m_transfers, &TransferManager::jobUpdated, this,
            [this, jobId, src, srcPath, dst, moveSource, conn](int id) {
                if (id != jobId)
                    return;
                const auto &jobs = m_transfers->jobs();
                auto it = std::find_if(jobs.begin(), jobs.end(),
                                       [jobId](const net::TransferJob &j) { return j.id == jobId; });
                if (it == jobs.end() || it->status == QLatin1String("running"))
                    return;
                // Endzustand erreicht (fertig/Fehler/abgebrochen/pausiert):
                // Handler abmelden. Ein spaeterer Wiederholungslauf meldet
                // sich ueber startTransfer neu an.
                if (it->status != QLatin1String("paused"))
                    disconnect(*conn);
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
    // Wie beim Kopieren erst die Namenskonflikte klaeren. Ohne diese Pruefung
    // wuerde das Verschieben Zieldateien wortlos ersetzen — und anschliessend
    // auch noch die Quelle loeschen.
    withConflictCheck(dst, dstDir, dlg.results(),
                      [this, src, dst](const std::vector<std::pair<QString, QString>> &todo) {
                          for (const auto &[from, to] : todo)
                              startTransfer(src, from, dst, dst->parent(to), dst->basename(to),
                                            /*moveSource=*/true);
                      });
}

} // namespace ncssh::gui
