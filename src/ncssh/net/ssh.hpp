// libssh2-Anbindung: Verbindung, SFTP-Dateisystem, Remote-Runner, PTY-Shell.
// Hier — und NUR hier — wird libssh2 eingebunden. Die obere Schicht sieht
// ausschliesslich die Interfaces FileSystemProvider und CommandRunner.
#pragma once

#include "ncssh/core/filesystem.hpp"
#include "ncssh/core/hostkeys.hpp"
#include "ncssh/core/models.hpp"
#include "ncssh/core/runner.hpp"

#include <QByteArray>
#include <QString>
#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>

typedef struct _LIBSSH2_SESSION LIBSSH2_SESSION;
typedef struct _LIBSSH2_SFTP LIBSSH2_SFTP;
typedef struct _LIBSSH2_CHANNEL LIBSSH2_CHANNEL;

namespace ncssh::net {

using ncssh::core::CommandRunner;
using ncssh::core::FileEntry;
using ncssh::core::FileSystemProvider;
using ncssh::core::HostKeyStore;
using ncssh::core::LineCallback;
using ncssh::core::ServerProfile;
using ncssh::gui::CancelTokenPtr;

// Host-Key unbekannt (strict) oder geaendert (moeglicher MITM).
class HostKeyError : public std::runtime_error {
public:
    explicit HostKeyError(const QString &msg) : std::runtime_error(msg.toStdString()) {}
};

// Der gepinnte Fingerprint passt nicht zum gelieferten. Traegt beide mit, damit
// die Oberflaeche sie gegenueberstellen kann; die Verbindung ist zu diesem
// Zeitpunkt bereits abgebrochen (vor der Authentifizierung).
class HostKeyChangedError : public HostKeyError {
public:
    HostKeyChangedError(const QString &msg, QString expectedFp, QString receivedFp,
                        QString keyAlgorithm)
        : HostKeyError(msg), expected(std::move(expectedFp)),
          received(std::move(receivedFp)), algorithm(std::move(keyAlgorithm)) {}

    QString expected;
    QString received;
    QString algorithm;
};

class SFTPFileSystem;
class RemoteCommandRunner;
class RemoteShell;
struct ConnectControl;

struct ExecResult {
    int exitStatus = -1;
    QByteArray out;
    QByteArray err;
};

// Aktive SSH-Verbindung samt SFTP-Client. Eine pro verbundenem Server.
// libssh2-Sessions sind NICHT thread-safe: jede Operation laeuft unter m_mutex.
class SSHSession : public std::enable_shared_from_this<SSHSession> {
public:
    ~SSHSession();

    ServerProfile profile;
    QString osType = QStringLiteral("posix");
    QString hostFingerprint;
    QString hostKeyAlgo;
    QString hostKeyStatus = QStringLiteral("ignored");  // ignored | known | unknown
    bool closing = false;
    std::optional<QString> sudoPassword;  // nur im RAM; leer = NOPASSWD/unbekannt
    // su-Passwoerter je Ziel-Benutzer (Pane als anderer Benutzer) — nur im RAM.
    std::map<QString, QString> userPasswords;
    // Vom Server abgelehnte Umgebungsvariablen (nicht in AcceptEnv). Werden
    // auf weiteren Kanaelen nicht erneut angefragt — jede Anfrage kostet eine
    // Rundreise. Nur unter mutex() lesen/schreiben.
    QStringList rejectedEnv;

    // Setzt profile.environment auf einem frisch geoeffneten Kanal (vor
    // shell/exec; Aufrufer haelt mutex(), Session blockierend). Liefert die
    // Namen, die der Server bei DIESEM Aufruf abgelehnt hat.
    QStringList applyEnvironment(LIBSSH2_CHANNEL *channel);

    QString label() const { return profile.display(); }

    // Sendet ein Keepalive-Paket. false = die Verbindung ist weg.
    // Guenstig genug, um sie regelmaessig zu pruefen.
    bool sendKeepalive();

    std::unique_ptr<SFTPFileSystem> filesystem();
    std::unique_ptr<RemoteCommandRunner> runner();

    // Fuehrt einen Befehl aus und sammelt stdout/stderr/exit (blockierend).
    ExecResult exec(const QString &command, const QByteArray &stdinData = {});

    // Fuehrt einen Befehl in einem Pseudo-Terminal aus (ohne Echo) — fuer
    // Programme, die nur von einem Terminal lesen (su fragt das Passwort ueber
    // das TTY ab). step bekommt nach jeder neuen Ausgabe alles bisher Gelesene
    // und liefert Bytes, die ins Terminal geschrieben werden (leer = nichts).
    // stdout und stderr sind im PTY vereint (ExecResult::err bleibt leer).
    // Bricht ab, wenn idleTimeoutMs lang weder gelesen noch geschrieben wurde.
    using PtyStep = std::function<QByteArray(const QByteArray &output)>;
    ExecResult execPty(const QString &command, const PtyStep &step, int idleTimeoutMs = 30000);

    void close();

    // Interne Nutzung durch Provider/Runner/Shell (Lock + rohe libssh2-Session).
    std::recursive_mutex &mutex() { return m_mutex; }
    LIBSSH2_SESSION *raw() const { return m_session; }
    LIBSSH2_SFTP *sftp();
    int socket() const { return m_socket; }

private:
    friend std::shared_ptr<SSHSession> connectSession(const ServerProfile &, HostKeyStore *,
                                                      const ConnectControl &);
    friend int openViaProxyJump(const std::shared_ptr<SSHSession> &, const ServerProfile &,
                                HostKeyStore *, const ConnectControl &);

    std::recursive_mutex m_mutex;
    LIBSSH2_SESSION *m_session = nullptr;
    LIBSSH2_SFTP *m_sftp = nullptr;
    int m_socket = -1;
    bool m_closed = false;

    // ProxyJump: Transport laeuft ueber einen direct-tcpip-Kanal des Sprung-Hosts.
    // Die Sprung-Session wird hier am Leben gehalten; ein Pump-Thread schaufelt
    // Bytes zwischen unserem lokalen Socket-Paar und diesem Kanal.
    std::shared_ptr<SSHSession> m_jump;
    std::thread m_pumpThread;
    std::atomic<bool> m_pumpStop{false};
};
using SSHSessionPtr = std::shared_ptr<SSHSession>;

// Normalisiert einen Key-Pfad (expandiert ~, dekodiert Prozent-Kodierung).
QString resolveKeyPath(const QString &keyPath);

// Baut eine SSH-Verbindung gemaess Profil auf (Key/Passwort/Agent). Host-Key-
// Pruefung erfolgt NACH dem Handshake, aber VOR jeder Authentifizierung.
SSHSessionPtr connectSession(const ServerProfile &profile, HostKeyStore *hostkeys);

// Steuerung eines Verbindungsaufbaus aus der Oberflaeche.
struct ConnectControl {
    // Meldet den aktuellen Schritt ("Verbinde TCP …", "Authentifiziere …").
    // Wird im Worker-Thread aufgerufen.
    std::function<void(const QString &)> progress;
    // Gesamtfrist fuer den Aufbau; Standardwert = keine Frist (nur die
    // Einzel-Timeouts des Profils gelten).
    std::chrono::steady_clock::time_point deadline{};
    // Abbruch durch den Nutzer. Wird zwischen den Schritten und waehrend des
    // TCP-Aufbaus geprueft; ein laufender libssh2-Aufruf endet erst mit seinem
    // Timeout (die Oberflaeche verwirft das Ergebnis dann).
    std::shared_ptr<std::atomic_bool> cancelled;
};
SSHSessionPtr connectSession(const ServerProfile &profile, HostKeyStore *hostkeys,
                             const ConnectControl &control);

// Zweite, unabhaengige Verbindung zum Server einer bestehenden Session — als
// eigene Datenleitung fuer Transfers, damit Listings/Konsole nicht am Session-
// Mutex der Hauptverbindung warten. Der Host-Key MUSS exakt dem der bereits
// verifizierten Hauptverbindung entsprechen. Liefert nullptr, wenn keine
// zweite Verbindung moeglich ist (Server-Limit, 2FA, anderer Key, ...).
SSHSessionPtr openSiblingSession(const SSHSessionPtr &base);

// Interop: haengt den Host-Key der Session an OpenSSHs ~/.ssh/known_hosts an
// (wird nach Nutzer-Bestaetigung eines neuen Keys aufgerufen).
void addToOpenSshKnownHosts(const SSHSessionPtr &session, const QString &host, int port);

// Remote-Dateisystem ueber SFTP. Erfuellt denselben Vertrag wie lokal.
class SFTPFileSystem : public FileSystemProvider {
public:
    explicit SFTPFileSystem(SSHSessionPtr session);

    std::vector<FileEntry> listDir(const QString &path) override;
    bool isDir(const QString &path) override;
    void mkdir(const QString &path) override;
    void remove(const QString &path, bool recursive = false) override;
    QString readText(const QString &path, qint64 maxBytes = 200'000) override;
    void writeText(const QString &path, const QString &content) override;
    void writeBytes(const QString &path, const QByteArray &data) override;
    QByteArray readBytes(const QString &path, qint64 maxBytes = 25'000'000) override;
    void rename(const QString &oldPath, const QString &newPath) override;
    void chmod(const QString &path, quint32 mode) override;
    QString join(const QString &path, const QString &name) const override;
    QString parent(const QString &path) const override;
    QString basename(const QString &path) const override;
    QString home() override;
    QString resolve(const QString &path) override;
    void symlink(const QString &target, const QString &linkPath) override;

    // Groesse einer Remote-Datei (fuer Transfer/Resume).
    qint64 size(const QString &path) override;
    SSHSessionPtr session() const { return m_session; }

private:
    SSHSessionPtr m_session;
};

// Fuehrt Befehle ueber die bestehende SSH-Verbindung aus.
class RemoteCommandRunner : public CommandRunner {
public:
    explicit RemoteCommandRunner(SSHSessionPtr session);

    void stream(const QString &command, const QString &cwd,
                const LineCallback &onLine, const CancelTokenPtr &cancel) override;
    std::optional<QString> resolveDir(const QString &cwd, const QString &target) override;
    void runTerminal(const QString &command, const QString &cwd,
                     const LineCallback &onChunk, const CancelTokenPtr &cancel,
                     int cols = 120, int rows = 40) override;

private:
    QString wrap(const QString &command, const QString &cwd) const;
    SSHSessionPtr m_session;
};

// Interaktiver PTY-Shell-Kanal fuer das Terminal-Widget.
class RemoteShell {
public:
    static std::unique_ptr<RemoteShell> open(SSHSessionPtr session, int cols, int rows);
    ~RemoteShell();

    void write(const QByteArray &data);
    // Umgebungsvariablen, die der Server beim Oeffnen abgelehnt hat.
    const QStringList &rejectedEnvironment() const { return m_rejectedEnv; }
    // Liest bis maxBytes (stdout, sonst stderr); wartet hoechstens timeoutMs
    // (leer bei Timeout). Kehrt nie ohne Wartezeit leer zurueck. Wirft bei
    // echten Kanal-/Transportfehlern und wenn die Session schliesst. Nach dem
    // Kanalende (Gegenseite hat z. B. "exit" ausgefuehrt) ist atEof() true.
    QByteArray read(int maxBytes = 8192, int timeoutMs = 100);
    // Kanal zu Ende (EOF der Gegenseite oder per close() geschlossen). Nur
    // aus dem Lesethread abfragen (wird von read() gesetzt).
    bool atEof() const { return m_eof; }
    void resize(int cols, int rows);
    void close();

private:
    explicit RemoteShell(SSHSessionPtr session) : m_session(std::move(session)) {}
    SSHSessionPtr m_session;
    void *m_channel = nullptr;  // LIBSSH2_CHANNEL*; nur unter dem Session-Mutex aendern
    bool m_closed = false;      // nur unter dem Session-Mutex
    bool m_eof = false;         // nur im Lesethread (read/atEof)
    QStringList m_rejectedEnv;  // nur in open() gesetzt
};

} // namespace ncssh::net
