// Ein Arbeitsbereich (Tab): zwei Panes (lokal + remote) mit je eigener Konsole,
// gekoppelt an eine SSH-Verbindung.  (Port von gui/workspace.py, zusammengefasst)
#pragma once

#include "ncssh/core/filesystem.hpp"
#include "ncssh/core/netfs.hpp"
#include "ncssh/core/runner.hpp"
#include "ncssh/gui/bridge.hpp"
#include "ncssh/gui/tunnel_dialog.hpp"
#include "ncssh/net/session.hpp"
#include "ncssh/net/ssh.hpp"
#include "ncssh/net/sudofs.hpp"

#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QWidget>
#include <functional>
#include <memory>
#include <utility>

class QSplitter;
class QTimer;
class QEvent;

namespace ncssh::gui {

class FilePanel;
class ConsolePanel;
class PreviewPanel;
class TransferManager;

class Workspace : public QWidget {
    Q_OBJECT
public:
    Workspace(AsyncBridge *bridge, net::SessionManager *sessions,
              TransferManager *transfers, QWidget *parent = nullptr);
    ~Workspace() override;

    // Verbindet die rechte Pane mit einem Server (asynchron).
    void connectTo(const core::ServerProfile &profile);
    // Trennt die Verbindung und stellt die rechte Seite auf lokal zurueck.
    void disconnectSession();
    bool isConnected() const { return static_cast<bool>(m_session); }
    QString connectionLabel() const;

    // Vorschau-Panels beider Seiten ein-/ausblenden.
    void setPreviewVisible(bool visible);
    bool previewVisible() const;

    // --- Ansichts-Modi ---
    // Nur die Dateilisten bzw. nur die Konsolen zeigen (sonst beides).
    void setOnlyFilesystem(bool on);
    void setOnlyTerminal(bool on);
    bool onlyFilesystem() const { return m_onlyFilesystem; }
    bool onlyTerminal() const { return m_onlyTerminal; }
    // Panes nebeneinander (waagerecht) oder untereinander (senkrecht).
    void setPanesVertical(bool vertical);
    bool panesVertical() const;
    // Linke und rechte Seite tauschen bzw. Verzeichnis angleichen.
    void swapPanes();
    void syncPanes();
    // Befehl an beide Konsolen schicken.
    void broadcastToConsoles(const QString &command, bool execute);
    // Ausgabe der aktiven Konsole von der KI erklaeren lassen.
    void explainActiveConsoleWithAi();

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

    // Dateisystem-Provider dieses Tabs — fuer SFTP-Batch/geplante Aufgaben.
    core::FileSystemProvider *localFs() const { return m_localFs.get(); }
    core::FileSystemProvider *remoteFs() const { return m_remoteFs.get(); }  // null wenn getrennt

    // Zustand des Tabs fuer Tab-Favoriten / Sitzungswiederherstellung.
    QJsonObject toJson() const;
    void restoreFrom(const QJsonObject &state);

    // Netzwerk-Modus: die aktive Pane zeigt die gefundenen Hosts als
    // navigierbaren Baum (net:// -> Host -> Freigabe -> Dateien).
    // side: "left" | "right" | leer = aktive Pane.
    void showNetworkHosts(const std::vector<core::HostResult> &hosts,
                          const QString &side = {});

signals:
    void statusMessage(const QString &msg);
    void connectionChanged();
    // Aus dem Pane-Kontextmenue: Verzeichnis-Vergleich beider Seiten oeffnen.
    void dirDiffRequested();
    // Netzwerk-Modus: Scanner erneut oeffnen bzw. zu einem Host verbinden.
    void rescanRequested();
    void connectHostRequested(const QString &host);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    // Aktive Pane + Konsole blau umranden (Property "active" je Seite setzen).
    void highlightActive();
    // Hoehenaufteilung (Pane/Vorschau/Konsole) je Spalte merken.
    void saveConsoleSplits();
    // moveSource: Quelle nach erfolgreicher Uebertragung entfernen (Verschieben).
    void startTransfer(core::FileSystemProvider *src, const QString &srcPath,
                       core::FileSystemProvider *dst, const QString &dstDir,
                       const QString &overrideName = {}, bool moveSource = false);
    // F5: Bestaetigungsdialog mit waehlbarem Zielordner, dann uebertragen.
    void confirmAndTransfer(core::FileSystemProvider *src,
                            const std::vector<QString> &srcPaths,
                            core::FileSystemProvider *dst, const QString &dstDir);
    // Wie confirmAndTransfer, entfernt die Quelle aber nach Erfolg.
    void confirmAndMove(core::FileSystemProvider *src,
                        const std::vector<QString> &srcPaths,
                        core::FileSystemProvider *dst, const QString &dstDir);
    // Prueft Namenskonflikte im Zielordner und ruft then() mit den
    // freigegebenen (quelle, ziel)-Paaren.
    void withConflictCheck(
        core::FileSystemProvider *dst, const QString &targetDir,
        const std::vector<std::pair<QString, QString>> &results,
        const std::function<void(const std::vector<std::pair<QString, QString>> &)> &then);
    // Rueckfrage je Konflikt: Ja / Ja, alle / Nein / Nein, alle / Abbrechen.
    std::vector<std::pair<QString, QString>> resolveOverwrites(
        const QString &dstLabel, const QString &targetDir,
        const std::vector<std::pair<QString, QString>> &results,
        const QSet<QString> &existing, bool &cancelled);
    // Strg+V: Inhalt der internen Zwischenablage in target einfuegen.
    void pasteInto(FilePanel *target, bool move);
    void setSudoMode(bool on);
    // Haengt das sudo-Dateisystem in die rechte Pane (Passwort bereits geklaert).
    void enableSudoFilesystem(const QString &keepPath);
    // Regelmaessige Keepalive-Pruefung; bei Abbruch wird neu verbunden.
    void startHealthCheck();
    // Konsole in ein eigenes Fenster loesen bzw. zurueckholen.
    void undockConsole(ConsolePanel *console);
    void dockConsole(ConsolePanel *console);

    AsyncBridge *m_bridge;
    net::SessionManager *m_sessions;
    TransferManager *m_transfers;

    // Lokale Seite (immer verfuegbar).
    std::unique_ptr<core::LocalFileSystem> m_localFs;
    std::unique_ptr<core::LocalCommandRunner> m_localRunner;
    // Netzwerk-Modus (virtuelles net://-Dateisystem aus dem Scanner).
    std::unique_ptr<core::NetworkScanProvider> m_netFs;

    // Remote-Seite (nach Connect).
    net::SSHSessionPtr m_session;
    std::unique_ptr<net::SFTPFileSystem> m_remoteFs;
    std::unique_ptr<net::SudoFileSystem> m_sudoFs;   // aktiv bei sudo-Chip
    std::unique_ptr<net::RemoteCommandRunner> m_remoteRunner;

    FilePanel *m_leftPanel = nullptr;
    FilePanel *m_rightPanel = nullptr;
    PreviewPanel *m_leftPreview = nullptr;
    PreviewPanel *m_rightPreview = nullptr;
    ConsolePanel *m_leftConsole = nullptr;
    ConsolePanel *m_rightConsole = nullptr;
    // Pane/Konsole, in der die aktuelle Verbindung liegt. Beim Verbinden auf die
    // aktive Seite gesetzt; solange nichts verbunden ist, zeigt es auf rechts.
    FilePanel *m_connectedPanel = nullptr;
    ConsolePanel *m_connectedConsole = nullptr;
    bool m_rightActive = false;  // zuletzt fokussierte Seite
    TunnelManager m_tunnels;     // offene Port-Weiterleitungen dieser Sitzung
    QTimer *m_healthTimer = nullptr;   // Keepalive-Wecker
    bool m_healthPending = false;      // laeuft gerade eine Pruefung?
    bool m_onlyFilesystem = false;
    bool m_onlyTerminal = false;
    QSplitter *m_columns = nullptr;   // waagerechter Splitter der beiden Seiten
    // Spalten-Splitter je Seite (zum Wiedereinhaengen abgedockter Konsolen)
    QSplitter *m_leftColumn = nullptr;
    QSplitter *m_rightColumn = nullptr;
    QHash<ConsolePanel *, QWidget *> m_floatingConsoles;
};

} // namespace ncssh::gui
