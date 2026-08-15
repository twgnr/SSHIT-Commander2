#include "ncssh/net/ssh.hpp"

#include "ncssh/core/lsparse.hpp"
#include "ncssh/core/ppk.hpp"
#include "ncssh/core/settings.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <libssh2.h>
#include <libssh2_sftp.h>
#include <mutex>
#include <vector>

#ifdef Q_OS_WIN
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <cerrno>
#endif

namespace ncssh::net {

// --- WinSock/libssh2 einmalig initialisieren -------------------------------
static std::once_flag g_initFlag;
static void ensureInit()
{
    std::call_once(g_initFlag, [] {
#ifdef Q_OS_WIN
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
        libssh2_init(0);
    });
}

static void closeSocket(int s)
{
    if (s < 0)
        return;
#ifdef Q_OS_WIN
    closesocket(static_cast<SOCKET>(s));
#else
    ::close(s);
#endif
}

static void fail(const QString &msg)
{
    throw std::runtime_error(msg.toStdString());
}

static void setNonBlockingSock(int s, bool nonBlocking)
{
#ifdef Q_OS_WIN
    u_long mode = nonBlocking ? 1 : 0;
    ioctlsocket(static_cast<SOCKET>(s), FIONBIO, &mode);
#else
    const int fl = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, nonBlocking ? (fl | O_NONBLOCK) : (fl & ~O_NONBLOCK));
#endif
}

static bool sockWouldBlock()
{
#ifdef Q_OS_WIN
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EWOULDBLOCK || errno == EAGAIN;
#endif
}

// Erzeugt ein ueber den Loopback verbundenes TCP-Socket-Paar. Windows kennt
// kein socketpair(); wir binden einen Listener auf 127.0.0.1:0, verbinden uns
// selbst und nehmen die Gegenseite an. Danach sind a und b bidirektional
// verbunden (a = unsere Ziel-Seite, b = Pump-Seite).
static bool loopbackSocketPair(int &a, int &b)
{
    a = b = -1;
    const SOCKET listener = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener == INVALID_SOCKET)
        return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    socklen_t len = sizeof(addr);
    if (::bind(listener, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0
        || ::listen(listener, 1) != 0
        || ::getsockname(listener, reinterpret_cast<sockaddr *>(&addr), &len) != 0) {
        closeSocket(static_cast<int>(listener));
        return false;
    }
    const SOCKET client = ::socket(AF_INET, SOCK_STREAM, 0);
    if (client == INVALID_SOCKET) {
        closeSocket(static_cast<int>(listener));
        return false;
    }
    // Blockierendes connect auf einen lauschenden Loopback-Socket schliesst den
    // TCP-Handshake ohne gleichzeitiges accept() ab (SYN landet im Accept-Queue).
    if (::connect(client, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        closeSocket(static_cast<int>(client));
        closeSocket(static_cast<int>(listener));
        return false;
    }
    const SOCKET server = ::accept(listener, nullptr, nullptr);
    closeSocket(static_cast<int>(listener));
    if (server == INVALID_SOCKET) {
        closeSocket(static_cast<int>(client));
        return false;
    }
    int one = 1;
    ::setsockopt(client, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char *>(&one), sizeof(one));
    ::setsockopt(server, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char *>(&one), sizeof(one));
    a = static_cast<int>(client);
    b = static_cast<int>(server);
    return true;
}

// Wartet, bis der Socket in der von libssh2 gewuenschten Richtung bereit ist.
static int waitSocket(int socket, LIBSSH2_SESSION *session, int timeoutMs = 10000)
{
    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    fd_set fdr, fdw;
    FD_ZERO(&fdr);
    FD_ZERO(&fdw);
    FD_SET(static_cast<SOCKET>(socket), &fdr);
    FD_SET(static_cast<SOCKET>(socket), &fdw);
    const int dir = libssh2_session_block_directions(session);
    fd_set *rp = (dir & LIBSSH2_SESSION_BLOCK_INBOUND) ? &fdr : nullptr;
    fd_set *wp = (dir & LIBSSH2_SESSION_BLOCK_OUTBOUND) ? &fdw : nullptr;
    if (!rp && !wp)
        rp = &fdr;
    return select(socket + 1, rp, wp, nullptr, &tv);
}

// TCP-Verbindung zu host:port aufbauen.
static int tcpConnect(const QString &host, int port)
{
    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *res = nullptr;
    const QByteArray hostB = host.toUtf8();
    const QByteArray portB = QByteArray::number(port);
    if (getaddrinfo(hostB.constData(), portB.constData(), &hints, &res) != 0 || !res)
        fail(QStringLiteral("Host nicht auflösbar: %1").arg(host));
    int sock = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        sock = static_cast<int>(::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol));
        if (sock < 0)
            continue;
        if (::connect(sock, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) == 0)
            break;
        closeSocket(sock);
        sock = -1;
    }
    freeaddrinfo(res);
    if (sock < 0)
        fail(QStringLiteral("Verbindung fehlgeschlagen: %1:%2").arg(host).arg(port));
    return sock;
}

static QString lastSshError(LIBSSH2_SESSION *session)
{
    char *msg = nullptr;
    libssh2_session_last_error(session, &msg, nullptr, 0);
    return msg ? QString::fromUtf8(msg) : QStringLiteral("SSH-Fehler");
}

// Fingerprint: "SHA256:" + Base64(SHA256(hostkey)) ohne Padding.
static QString hostFingerprint(LIBSSH2_SESSION *session, QString &algoOut)
{
    int keyType = 0;
    size_t len = 0;
    const char *key = libssh2_session_hostkey(session, &len, &keyType);
    if (!key)
        return {};
    switch (keyType) {
    case LIBSSH2_HOSTKEY_TYPE_RSA: algoOut = QStringLiteral("ssh-rsa"); break;
    case LIBSSH2_HOSTKEY_TYPE_DSS: algoOut = QStringLiteral("ssh-dss"); break;
#ifdef LIBSSH2_HOSTKEY_TYPE_ECDSA_256
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_256: algoOut = QStringLiteral("ecdsa-sha2-nistp256"); break;
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_384: algoOut = QStringLiteral("ecdsa-sha2-nistp384"); break;
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_521: algoOut = QStringLiteral("ecdsa-sha2-nistp521"); break;
    case LIBSSH2_HOSTKEY_TYPE_ED25519: algoOut = QStringLiteral("ssh-ed25519"); break;
#endif
    default: algoOut = QStringLiteral("ssh-unknown"); break;
    }
    const char *hash = libssh2_hostkey_hash(session, LIBSSH2_HOSTKEY_HASH_SHA256);
    if (!hash)
        return {};
    const QByteArray raw(hash, 32);
    QString b64 = QString::fromLatin1(raw.toBase64());
    while (b64.endsWith(QLatin1Char('=')))
        b64.chop(1);
    return QStringLiteral("SHA256:") + b64;
}

// ---------------------------------------------------------------------------
// SSHSession
// ---------------------------------------------------------------------------

SSHSession::~SSHSession()
{
    close();
}

bool SSHSession::sendKeepalive()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_closed || !m_session)
        return false;
    int secondsToNext = 0;
    // Liefert < 0 bei Uebertragungsfehler — dann ist die Verbindung tot.
    return libssh2_keepalive_send(m_session, &secondsToNext) >= 0;
}

void SSHSession::close()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_closed)
        return;
    m_closed = true;
    if (m_sftp) {
        libssh2_sftp_shutdown(m_sftp);
        m_sftp = nullptr;
    }
    if (m_session) {
        libssh2_session_disconnect(m_session, "bye");
        libssh2_session_free(m_session);
        m_session = nullptr;
    }
    // ProxyJump-Transport abbauen: unseren Socket schliessen bricht das lokale
    // Socket-Paar, der Pump-Thread entblockt und beendet sich, dann die
    // Sprung-Session freigeben. Der Pump nutzt NICHT unseren Mutex -> kein
    // Deadlock beim join(). Fuer Direktverbindungen sind diese Schritte No-ops.
    m_pumpStop.store(true);
    closeSocket(m_socket);
    m_socket = -1;
    if (m_pumpThread.joinable())
        m_pumpThread.join();
    m_jump.reset();
}

LIBSSH2_SFTP *SSHSession::sftp()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_sftp && m_session) {
        while (!(m_sftp = libssh2_sftp_init(m_session))) {
            if (libssh2_session_last_errno(m_session) != LIBSSH2_ERROR_EAGAIN)
                fail(QStringLiteral("SFTP-Initialisierung fehlgeschlagen: %1")
                         .arg(lastSshError(m_session)));
            waitSocket(m_socket, m_session);
        }
    }
    return m_sftp;
}

ExecResult SSHSession::exec(const QString &command, const QByteArray &stdinData)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    ExecResult result;
    if (!m_session)
        fail("Sitzung geschlossen.");
    LIBSSH2_CHANNEL *channel = nullptr;
    while (!(channel = libssh2_channel_open_session(m_session))) {
        if (libssh2_session_last_errno(m_session) != LIBSSH2_ERROR_EAGAIN)
            fail(QStringLiteral("Kanal konnte nicht geöffnet werden: %1")
                     .arg(lastSshError(m_session)));
        waitSocket(m_socket, m_session);
    }
    const QByteArray cmd = command.toUtf8();
    int rc;
    while ((rc = libssh2_channel_exec(channel, cmd.constData())) == LIBSSH2_ERROR_EAGAIN)
        waitSocket(m_socket, m_session);
    if (rc != 0) {
        libssh2_channel_free(channel);
        fail(QStringLiteral("Befehl fehlgeschlagen: %1").arg(lastSshError(m_session)));
    }
    if (!stdinData.isEmpty()) {
        qint64 sent = 0;
        while (sent < stdinData.size()) {
            const ssize_t n = libssh2_channel_write(channel, stdinData.constData() + sent,
                                                    stdinData.size() - sent);
            if (n == LIBSSH2_ERROR_EAGAIN) {
                waitSocket(m_socket, m_session);
                continue;
            }
            if (n < 0)
                break;
            sent += n;
        }
        libssh2_channel_send_eof(channel);
    }
    char buf[16384];
    for (;;) {
        ssize_t n = libssh2_channel_read(channel, buf, sizeof(buf));
        if (n == LIBSSH2_ERROR_EAGAIN) {
            // stderr auch bedienen, dann warten
            ssize_t e = libssh2_channel_read_stderr(channel, buf, sizeof(buf));
            if (e > 0) { result.err.append(buf, e); continue; }
            if (libssh2_channel_eof(channel))
                break;
            waitSocket(m_socket, m_session);
            continue;
        }
        if (n <= 0)
            break;
        result.out.append(buf, n);
    }
    for (;;) {
        ssize_t e = libssh2_channel_read_stderr(channel, buf, sizeof(buf));
        if (e == LIBSSH2_ERROR_EAGAIN) {
            if (libssh2_channel_eof(channel))
                break;
            waitSocket(m_socket, m_session);
            continue;
        }
        if (e <= 0)
            break;
        result.err.append(buf, e);
    }
    while (libssh2_channel_close(channel) == LIBSSH2_ERROR_EAGAIN)
        waitSocket(m_socket, m_session);
    result.exitStatus = libssh2_channel_get_exit_status(channel);
    libssh2_channel_free(channel);
    return result;
}

std::unique_ptr<SFTPFileSystem> SSHSession::filesystem()
{
    return std::make_unique<SFTPFileSystem>(shared_from_this());
}

std::unique_ptr<RemoteCommandRunner> SSHSession::runner()
{
    return std::make_unique<RemoteCommandRunner>(shared_from_this());
}

// ---------------------------------------------------------------------------
// Verbindungsaufbau
// ---------------------------------------------------------------------------

QString resolveKeyPath(const QString &keyPath)
{
    QString p = keyPath;
    if (p.startsWith(QLatin1Char('~')))
        p = QDir::homePath() + p.mid(1);
    if (!p.isEmpty() && !QFile::exists(p) && p.contains(QLatin1Char('%'))) {
        const QString decoded = QUrl::fromPercentEncoding(p.toUtf8());
        if (QFile::exists(decoded))
            return decoded;
    }
    return p;
}

static QString detectOs(SSHSession &session)
{
    try {
        const ExecResult r = session.exec(QStringLiteral("uname -s"));
        const QString out = QString::fromUtf8(r.out).trimmed();
        if (r.exitStatus == 0
            && (out.contains(QLatin1String("Linux")) || out.contains(QLatin1String("Darwin"))
                || out.contains(QLatin1String("BSD")) || out.contains(QLatin1String("SunOS"))
                || out.contains(QLatin1String("AIX"))))
            return QStringLiteral("posix");
    } catch (...) {
    }
    return QStringLiteral("windows");
}

// Keyboard-interactive-Callback: beantwortet jede Aufforderung mit dem
// hinterlegten Passwort (Adresse liegt im Session-Abstract). Viele Server
// (PAM) bieten statt "password" nur "keyboard-interactive" an.
static void kbdInteractiveResponse(const char * /*name*/, int /*name_len*/,
                                   const char * /*instruction*/, int /*instruction_len*/,
                                   int num_prompts,
                                   const LIBSSH2_USERAUTH_KBDINT_PROMPT * /*prompts*/,
                                   LIBSSH2_USERAUTH_KBDINT_RESPONSE *responses,
                                   void **abstract)
{
    const auto *pw = abstract ? static_cast<const QByteArray *>(*abstract) : nullptr;
    for (int i = 0; i < num_prompts; ++i) {
        if (pw && !pw->isEmpty()) {
            responses[i].text = static_cast<char *>(malloc(pw->size()));
            if (responses[i].text) {
                memcpy(responses[i].text, pw->constData(), pw->size());
                responses[i].length = static_cast<unsigned int>(pw->size());
            }
        } else {
            responses[i].text = nullptr;
            responses[i].length = 0;
        }
    }
}

// Sensiblen Puffer (Passwort/Passphrase/Schluessel) nach Gebrauch sicher
// ueberschreiben. volatile verhindert, dass der Compiler das Nullen wegoptimiert.
static void secureZero(QByteArray &b)
{
    if (b.isEmpty())
        return;
    volatile char *p = b.data();   // data() loest Copy-on-Write -> eigener Puffer
    for (qsizetype i = 0; i < b.size(); ++i)
        p[i] = 0;
}

// libssh2-Hostkey-Typ -> KNOWNHOST-Key-Bit (0 = unbekannt).
static int knownHostKeyBit(int type)
{
    switch (type) {
    case LIBSSH2_HOSTKEY_TYPE_RSA: return LIBSSH2_KNOWNHOST_KEY_SSHRSA;
    case LIBSSH2_HOSTKEY_TYPE_DSS: return LIBSSH2_KNOWNHOST_KEY_SSHDSS;
#ifdef LIBSSH2_HOSTKEY_TYPE_ED25519
    case LIBSSH2_HOSTKEY_TYPE_ED25519: return LIBSSH2_KNOWNHOST_KEY_ED25519;
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_256: return LIBSSH2_KNOWNHOST_KEY_ECDSA_256;
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_384: return LIBSSH2_KNOWNHOST_KEY_ECDSA_384;
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_521: return LIBSSH2_KNOWNHOST_KEY_ECDSA_521;
#endif
    default: return 0;
    }
}

static QString openSshKnownHostsPath()
{
    return QDir::homePath() + QStringLiteral("/.ssh/known_hosts");
}

// Prueft den aktuellen Host-Key gegen OpenSSHs ~/.ssh/known_hosts (Interop mit
// dem System-ssh). Rueckgabe: 1 = passt/bekannt, 0 = nicht gefunden,
// -1 = fuer diesen Host ist ein ANDERER Key hinterlegt (moeglicher MITM).
static int checkOpenSshKnownHosts(LIBSSH2_SESSION *sess, const QString &host, int port)
{
    const QString khPath = openSshKnownHostsPath();
    if (!QFile::exists(khPath))
        return 0;
    LIBSSH2_KNOWNHOSTS *kh = libssh2_knownhost_init(sess);
    if (!kh)
        return 0;
    libssh2_knownhost_readfile(kh, khPath.toUtf8().constData(),
                               LIBSSH2_KNOWNHOST_FILE_OPENSSH);
    size_t len = 0;
    int type = 0;
    const char *key = libssh2_session_hostkey(sess, &len, &type);
    int result = 0;
    if (key) {
        const int typemask = LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW
                             | knownHostKeyBit(type);
        struct libssh2_knownhost *found = nullptr;
        const int rc = libssh2_knownhost_checkp(kh, host.toUtf8().constData(), port,
                                                key, len, typemask, &found);
        if (rc == LIBSSH2_KNOWNHOST_CHECK_MATCH)
            result = 1;
        else if (rc == LIBSSH2_KNOWNHOST_CHECK_MISMATCH)
            result = -1;
    }
    libssh2_knownhost_free(kh);
    return result;
}

// Haengt den aktuellen Host-Key an OpenSSHs ~/.ssh/known_hosts an (aufgerufen,
// wenn der Nutzer einen neuen Key in der GUI bestaetigt).
void addToOpenSshKnownHosts(const SSHSessionPtr &session, const QString &host, int port)
{
    if (!session)
        return;
    std::lock_guard<std::recursive_mutex> lock(session->mutex());
    LIBSSH2_SESSION *sess = session->raw();
    LIBSSH2_KNOWNHOSTS *kh = libssh2_knownhost_init(sess);
    if (!kh)
        return;
    const QString khPath = openSshKnownHostsPath();
    libssh2_knownhost_readfile(kh, khPath.toUtf8().constData(),
                               LIBSSH2_KNOWNHOST_FILE_OPENSSH);
    size_t len = 0;
    int type = 0;
    const char *key = libssh2_session_hostkey(sess, &len, &type);
    if (key) {
        // Nicht-Standardport wird als "[host]:port" hinterlegt (OpenSSH-Format).
        const QByteArray hostSpec = (port == 22)
                                        ? host.toUtf8()
                                        : QStringLiteral("[%1]:%2").arg(host).arg(port).toUtf8();
        const int typemask = LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW
                             | knownHostKeyBit(type);
        libssh2_knownhost_addc(kh, hostSpec.constData(), nullptr, key, len, nullptr, 0,
                               typemask, nullptr);
        QDir().mkpath(QFileInfo(khPath).path());
        libssh2_knownhost_writefile(kh, khPath.toUtf8().constData(),
                                    LIBSSH2_KNOWNHOST_FILE_OPENSSH);
    }
    libssh2_knownhost_free(kh);
}

// --- ProxyJump / Sprung-Host -----------------------------------------------

// Zerlegt die Sprungspezifikation "[user@]host[:port]" in ein Profil. Fehlt der
// Benutzer, wird der des Ziels genommen. Authentifizierung am Sprung-Host:
// derselbe Schluessel wie das Ziel (falls Key-Auth), sonst der SSH-Agent — das
// deckt die typische Bastion-Konfiguration ab.
static ServerProfile parseJumpSpec(const QString &spec, const ServerProfile &base)
{
    ServerProfile j;
    QString s = spec.trimmed();
    const int at = s.indexOf(QLatin1Char('@'));
    if (at >= 0) {
        j.username = s.left(at);
        s = s.mid(at + 1);
    } else {
        j.username = base.username;
    }
    const int colon = s.lastIndexOf(QLatin1Char(':'));
    bool portOk = false;
    if (colon >= 0) {
        const int p = s.mid(colon + 1).toInt(&portOk);
        if (portOk) {
            j.host = s.left(colon);
            j.port = p;
        }
    }
    if (!portOk) {
        j.host = s;
        j.port = 22;
    }
    if (base.authMethod == QLatin1String("key") && !base.keyPath.isEmpty()) {
        j.authMethod = QStringLiteral("key");
        j.keyPath = base.keyPath;
        j.passphrase = base.passphrase;
    } else {
        j.authMethod = QStringLiteral("agent");
    }
    j.knownHostsPolicy = base.knownHostsPolicy;
    j.name = QStringLiteral("Sprung-Host %1").arg(j.host);
    return j;
}

// Schaufelt Bytes zwischen unserem lokalen Socket (sockB) und dem direct-tcpip-
// Kanal des Sprung-Hosts, bis eine Seite schliesst oder stop gesetzt wird.
// Laeuft in einem eigenen Thread und nutzt AUSSCHLIESSLICH die Sprung-Session
// (die sonst niemand mehr anfasst), sperrt deren Mutex also gefahrlos.
static void pumpProxyJump(SSHSessionPtr jump, LIBSSH2_CHANNEL *chan, int sockB,
                          std::atomic<bool> *stop)
{
    LIBSSH2_SESSION *js = jump->raw();
    const int jsock = jump->socket();
    {
        std::lock_guard<std::recursive_mutex> lk(jump->mutex());
        libssh2_session_set_blocking(js, 0);  // interleaved: beide Richtungen
    }
    // sockB bleibt blockierend: send blockt hoechstens, bis die Ziel-Session
    // (anderer Thread) liest — kein Busy-Spin. recv nur nach select().
    std::vector<char> buf(32768);
    const int maxfd = (sockB > jsock ? sockB : jsock) + 1;

    while (!stop->load()) {
        fd_set fdr;
        FD_ZERO(&fdr);
        FD_SET(static_cast<SOCKET>(sockB), &fdr);
        FD_SET(static_cast<SOCKET>(jsock), &fdr);
        timeval tv{0, 50 * 1000};
        select(maxfd, &fdr, nullptr, nullptr, &tv);
        if (stop->load())
            break;

        // Richtung 1: Ziel -> Sprung-Kanal
        if (FD_ISSET(static_cast<SOCKET>(sockB), &fdr)) {
            const int n = ::recv(sockB, buf.data(), static_cast<int>(buf.size()), 0);
            if (n <= 0)
                break;  // Ziel-Session hat ihren Socket geschlossen
            int off = 0;
            while (off < n && !stop->load()) {
                ssize_t w;
                {
                    std::lock_guard<std::recursive_mutex> lk(jump->mutex());
                    w = libssh2_channel_write(chan, buf.data() + off, n - off);
                }
                if (w == LIBSSH2_ERROR_EAGAIN) {
                    waitSocket(jsock, js, 1000);
                    continue;
                }
                if (w < 0) {
                    stop->store(true);
                    break;
                }
                off += static_cast<int>(w);
            }
        }

        // Richtung 2: Sprung-Kanal -> Ziel (alles Gepufferte abholen)
        for (;;) {
            ssize_t r;
            {
                std::lock_guard<std::recursive_mutex> lk(jump->mutex());
                r = libssh2_channel_read(chan, buf.data(), buf.size());
            }
            if (r <= 0)
                break;  // EAGAIN (nichts da) oder Fehler/EOF -> unten pruefen
            int off = 0;
            while (off < r && !stop->load()) {
                const int s = ::send(sockB, buf.data() + off, static_cast<int>(r - off), 0);
                if (s > 0) {
                    off += s;
                } else {
                    stop->store(true);
                    break;
                }
            }
        }
        bool eof;
        {
            std::lock_guard<std::recursive_mutex> lk(jump->mutex());
            eof = libssh2_channel_eof(chan) != 0;
        }
        if (eof)
            break;
    }

    closeSocket(sockB);  // schliesst unsere Seite -> Ziel bekommt EOF
    std::lock_guard<std::recursive_mutex> lk(jump->mutex());
    libssh2_channel_close(chan);
    libssh2_channel_free(chan);
}

// Baut die Verbindung zum Ziel ueber einen Sprung-Host auf und liefert den
// lokalen Socket, ueber den das Ziel danach seinen SSH-Handshake fuehrt. Die
// Sprung-Session sowie der Pump-Thread werden in target hinterlegt und beim
// Schliessen von target wieder abgebaut. Nur EIN Hop wird unterstuetzt.
int openViaProxyJump(const SSHSessionPtr &target, const ServerProfile &profile,
                     HostKeyStore *hostkeys)
{
    const ServerProfile jump = parseJumpSpec(profile.proxyJump, profile);
    SSHSessionPtr jsess;
    try {
        jsess = connectSession(jump, hostkeys);
    } catch (const std::exception &e) {
        fail(QStringLiteral("Sprung-Host (%1) nicht erreichbar: %2")
                 .arg(jump.display(), QString::fromUtf8(e.what())));
    }

    LIBSSH2_CHANNEL *chan = nullptr;
    {
        std::lock_guard<std::recursive_mutex> lk(jsess->mutex());
        const QByteArray th = profile.host.toUtf8();
        // Sprung-Session ist blockierend -> wartet, bis der Kanal offen ist.
        chan = libssh2_channel_direct_tcpip_ex(jsess->raw(), th.constData(), profile.port,
                                               "127.0.0.1", 22);
    }
    if (!chan)
        fail(QStringLiteral("Sprung-Host konnte keinen Kanal zu %1:%2 öffnen "
                            "(Weiterleitung am Bastion-Host erlaubt?).")
                 .arg(profile.host)
                 .arg(profile.port));

    int sockA = -1, sockB = -1;
    if (!loopbackSocketPair(sockA, sockB)) {
        std::lock_guard<std::recursive_mutex> lk(jsess->mutex());
        libssh2_channel_close(chan);
        libssh2_channel_free(chan);
        fail(QStringLiteral("Interner Socket-Kanal für ProxyJump fehlgeschlagen."));
    }

    target->m_jump = jsess;
    target->m_pumpStop.store(false);
    target->m_pumpThread =
        std::thread(&pumpProxyJump, jsess, chan, sockB, &target->m_pumpStop);
    return sockA;
}

SSHSessionPtr connectSession(const ServerProfile &profile, HostKeyStore *hostkeys)
{
    ensureInit();
    auto session = std::shared_ptr<SSHSession>(new SSHSession());
    session->profile = profile;

    // Transport: direkt ODER ueber einen Sprung-Host (ProxyJump). Im Jump-Fall
    // laeuft der SSH-Handshake ueber einen lokalen Socket, dessen Gegenseite ein
    // Pump-Thread mit dem direct-tcpip-Kanal des Bastion-Hosts verbindet.
    if (!profile.proxyJump.trimmed().isEmpty())
        session->m_socket = openViaProxyJump(session, profile, hostkeys);
    else
        session->m_socket = tcpConnect(profile.host, profile.port);
    LIBSSH2_SESSION *sess = libssh2_session_init();
    if (!sess) {
        closeSocket(session->m_socket);
        fail("libssh2-Session konnte nicht erstellt werden.");
    }
    session->m_session = sess;
    libssh2_session_set_blocking(sess, 1);
    libssh2_session_set_timeout(sess, (profile.connectTimeout > 0 ? profile.connectTimeout : 20)
                                          * 1000);

    // Verbindungs-Feinsteuerung (alles VOR dem Handshake wirksam):
    if (profile.compression)
        libssh2_session_flag(sess, LIBSSH2_FLAG_COMPRESS, 1);
    if (!profile.ciphers.isEmpty()) {
        const QByteArray c = profile.ciphers.toUtf8();
        libssh2_session_method_pref(sess, LIBSSH2_METHOD_CRYPT_CS, c.constData());
        libssh2_session_method_pref(sess, LIBSSH2_METHOD_CRYPT_SC, c.constData());
    }
    if (!profile.kexAlgorithms.isEmpty())
        libssh2_session_method_pref(sess, LIBSSH2_METHOD_KEX,
                                    profile.kexAlgorithms.toUtf8().constData());

    if (const int rc = libssh2_session_handshake(sess, session->m_socket); rc != 0) {
        QString message = QStringLiteral("SSH-Handshake fehlgeschlagen: %1").arg(lastSshError(sess));
        // Bei einem Fehler in der Algorithmus-Einigung liegt es meist an den vom
        // Crypto-Backend nicht unterstuetzten Verfahren (WinCNG kann kein
        // ed25519-Hostkey und kein curve25519). Dann ist der Hinweis konkret.
        if (rc == LIBSSH2_ERROR_KEX_FAILURE)
            message += QStringLiteral(
                "\n\nDer Server bietet nur Verfahren an, die diese Version nicht "
                "beherrscht (z. B. ausschließlich ed25519-Host-Key oder "
                "curve25519). Prüfe, ob der Server zusätzlich ecdsa/rsa als "
                "Host-Key bzw. ecdh/diffie-hellman als Schlüsseltausch erlaubt.");
        throw HostKeyError(message);
    }

    // Keepalive ERST NACH dem Handshake konfigurieren. Wird es vorher gesetzt,
    // scheitert der Schluesseltausch mit "Unable to exchange encryption keys"
    // (libssh2-Eigenheit — die Keepalive-Logik greift sonst in den KEX ein).
    if (profile.keepaliveSeconds > 0)
        libssh2_keepalive_config(sess, 1, profile.keepaliveSeconds);

    // --- Host-Key-Pruefung VOR jeder Authentifizierung ---
    QString algo;
    const QString fp = hostFingerprint(sess, algo);
    QString status = QStringLiteral("ignored");
    if (profile.knownHostsPolicy != QLatin1String("ignore") && hostkeys) {
        const auto exact = hostkeys->get(profile.host, profile.port, algo);
        if (exact) {
            if (*exact == fp) {
                status = QStringLiteral("known");
            } else {
                // Abbruch VOR der Authentifizierung — es gehen keine
                // Zugangsdaten an einen moeglicherweise fremden Server.
                throw HostKeyChangedError(
                    QStringLiteral("HOST-KEY HAT SICH GEÄNDERT! Möglicher MITM-Angriff.\n"
                                   "Erwartet: %1\nErhalten: %2").arg(*exact, fp),
                    *exact, fp, algo);
            }
        } else {
            const auto legacy = hostkeys->getLegacy(profile.host, profile.port);
            // OpenSSH-Interop: vertraut das System-ssh (~/.ssh/known_hosts) dem
            // Key bereits, uebernehmen wir das (kein erneutes Nachfragen).
            const int osk = core::getSettingBool(QStringLiteral("openssh_known_hosts"), true)
                                ? checkOpenSshKnownHosts(sess, profile.host, profile.port)
                                : 0;
            if (legacy && *legacy == fp) {
                status = QStringLiteral("known");
            } else if (osk == 1) {
                status = QStringLiteral("known");
            } else if (osk == -1) {
                throw HostKeyChangedError(
                    QStringLiteral("HOST-KEY weicht von ~/.ssh/known_hosts ab! "
                                   "Möglicher MITM-Angriff.\nErhalten: %1").arg(fp),
                    QString(), fp, algo);
            } else if (profile.knownHostsPolicy == QLatin1String("strict")) {
                throw HostKeyError(QStringLiteral("Unbekannter Host-Key (strict):\n%1").arg(fp));
            } else {
                status = QStringLiteral("unknown");  // accept-new: TOFU, UI bestaetigt
            }
        }
    }
    session->hostFingerprint = fp;
    session->hostKeyAlgo = algo;
    session->hostKeyStatus = status;

    // --- Authentifizierung ---
    const QByteArray user = profile.username.toUtf8();
    bool authed = false;
    if (profile.authMethod == QLatin1String("key") && !profile.keyPath.isEmpty()) {
        const QString kp = resolveKeyPath(profile.keyPath);
        QFile kf(kp);
        if (!kf.open(QIODevice::ReadOnly))
            fail(QStringLiteral("Schlüsseldatei nicht lesbar: %1").arg(kp));
        QByteArray keyData = kf.readAll();
        if (core::isPpk(keyData)) {
            keyData = core::ppkToOpenssh(keyData, profile.passphrase);  // PPK -> OpenSSH
        }
        QByteArray pass = profile.passphrase.toUtf8();
        const int rc = libssh2_userauth_publickey_frommemory(
            sess, user.constData(), user.size(), nullptr, 0,
            keyData.constData(), keyData.size(),
            profile.passphrase.isEmpty() ? nullptr : pass.constData());
        authed = (rc == 0);
        // Schluessel-Klartext und Passphrase sofort aus dem Speicher wischen.
        secureZero(keyData);
        secureZero(pass);
        if (!authed)
            fail(QStringLiteral("Schlüssel-Authentifizierung fehlgeschlagen: %1")
                     .arg(lastSshError(sess)));
    } else if (profile.authMethod == QLatin1String("password")) {
        QByteArray pw = profile.password.toUtf8();
        authed = libssh2_userauth_password(sess, user.constData(), pw.constData()) == 0;
        if (!authed) {
            // Fallback: Server, die kein "password" anbieten (PAM), akzeptieren
            // oft "keyboard-interactive". Das Passwort geht ueber das Abstract
            // an den Callback. Eigener Puffer (kein Copy-on-Write-Teilen mit pw).
            QByteArray kbdPw(pw.constData(), pw.size());
            *libssh2_session_abstract(sess) = &kbdPw;
            authed = libssh2_userauth_keyboard_interactive(
                         sess, user.constData(), &kbdInteractiveResponse) == 0;
            *libssh2_session_abstract(sess) = nullptr;
            secureZero(kbdPw);
        }
        secureZero(pw);
        if (!authed)
            fail(QStringLiteral("Passwort-Authentifizierung fehlgeschlagen: %1")
                     .arg(lastSshError(sess)));
    } else {  // agent
        LIBSSH2_AGENT *agent = libssh2_agent_init(sess);
        if (agent && libssh2_agent_connect(agent) == 0
            && libssh2_agent_list_identities(agent) == 0) {
            struct libssh2_agent_publickey *ident = nullptr, *prev = nullptr;
            while (libssh2_agent_get_identity(agent, &ident, prev) == 0) {
                if (libssh2_agent_userauth(agent, user.constData(), ident) == 0) {
                    authed = true;
                    break;
                }
                prev = ident;
            }
        }
        if (agent) {
            libssh2_agent_disconnect(agent);
            libssh2_agent_free(agent);
        }
        if (!authed)
            fail("SSH-Agent-Authentifizierung fehlgeschlagen (läuft ein Agent mit passendem Key?).");
    }

    session->osType = detectOs(*session);
    return session;
}

// ---------------------------------------------------------------------------
// SFTPFileSystem
// ---------------------------------------------------------------------------

static QString posixParent(const QString &path)
{
    QString p = QDir::cleanPath(path);
    const int idx = p.lastIndexOf(QLatin1Char('/'));
    if (idx < 0)
        return p;
    if (idx == 0)
        return QStringLiteral("/");
    return p.left(idx);
}

SFTPFileSystem::SFTPFileSystem(SSHSessionPtr session) : m_session(std::move(session))
{
    isRemote = true;
    label = m_session->label();
}

std::vector<FileEntry> SFTPFileSystem::listDir(const QString &path)
{
    std::lock_guard<std::recursive_mutex> lock(m_session->mutex());
    LIBSSH2_SFTP *sftp = m_session->sftp();
    std::vector<FileEntry> entries;
    if (posixParent(path) != path) {
        FileEntry up;
        up.name = QStringLiteral("..");
        up.type = core::EntryType::Parent;
        entries.push_back(up);
    }
    const QByteArray p = path.toUtf8();
    LIBSSH2_SFTP_HANDLE *handle = libssh2_sftp_opendir(sftp, p.constData());
    if (!handle)
        fail(QStringLiteral("Verzeichnis nicht lesbar: %1").arg(path));

    char name[1024];
    char longentry[2048];
    LIBSSH2_SFTP_ATTRIBUTES attrs;
    while (true) {
        const int rc = libssh2_sftp_readdir_ex(handle, name, sizeof(name),
                                                longentry, sizeof(longentry), &attrs);
        if (rc <= 0)
            break;
        const QString fn = QString::fromUtf8(name, rc);
        if (fn == QLatin1String(".") || fn == QLatin1String(".."))
            continue;
        // Sicherheit: ein boesartiger Server darf keine Namen mit Pfad-Trennern
        // liefern (z. B. "../../etc/passwd") — sonst koennte ein Download beim
        // Zusammensetzen des Zielpfads das gewaehlte Verzeichnis verlassen.
        if (fn.contains(QLatin1Char('/')) || fn.contains(QLatin1Char('\\')))
            continue;
        FileEntry e;
        e.name = fn;
        const unsigned long perm = (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) ? attrs.permissions : 0;
        if (LIBSSH2_SFTP_S_ISLNK(perm))
            e.type = core::EntryType::Symlink;
        else if (LIBSSH2_SFTP_S_ISDIR(perm))
            e.type = core::EntryType::Dir;
        else
            e.type = core::EntryType::File;
        e.size = (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE) ? qint64(attrs.filesize) : 0;
        e.permissions = static_cast<quint32>(perm);
        if (attrs.flags & LIBSSH2_SFTP_ATTR_ACMODTIME)
            e.modified = QDateTime::fromSecsSinceEpoch(attrs.mtime);
        if (attrs.flags & LIBSSH2_SFTP_ATTR_UIDGID) {
            e.owner = QString::number(attrs.uid);
            e.group = QString::number(attrs.gid);
        }
        if (e.type == core::EntryType::Symlink) {
            char target[1024];
            const int tn = libssh2_sftp_readlink(sftp, join(path, fn).toUtf8().constData(),
                                                 target, sizeof(target));
            if (tn > 0)
                e.linkTarget = QString::fromUtf8(target, tn);
        }
        entries.push_back(std::move(e));
    }
    libssh2_sftp_closedir(handle);

    std::sort(entries.begin(), entries.end(), [](const FileEntry &a, const FileEntry &b) {
        if (a.type == core::EntryType::Parent) return true;
        if (b.type == core::EntryType::Parent) return false;
        if (a.isDir() != b.isDir()) return a.isDir();
        return a.name.toLower() < b.name.toLower();
    });
    return entries;
}

bool SFTPFileSystem::isDir(const QString &path)
{
    std::lock_guard<std::recursive_mutex> lock(m_session->mutex());
    LIBSSH2_SFTP *sftp = m_session->sftp();
    LIBSSH2_SFTP_ATTRIBUTES attrs;
    if (libssh2_sftp_stat(sftp, path.toUtf8().constData(), &attrs) != 0)
        return false;
    return (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) && LIBSSH2_SFTP_S_ISDIR(attrs.permissions);
}

void SFTPFileSystem::mkdir(const QString &path)
{
    std::lock_guard<std::recursive_mutex> lock(m_session->mutex());
    LIBSSH2_SFTP *sftp = m_session->sftp();
    if (libssh2_sftp_mkdir(sftp, path.toUtf8().constData(), 0755) != 0)
        fail(QStringLiteral("Verzeichnis konnte nicht angelegt werden: %1").arg(path));
}

void SFTPFileSystem::remove(const QString &path, bool recursive)
{
    std::lock_guard<std::recursive_mutex> lock(m_session->mutex());
    if (isDir(path)) {
        if (recursive) {
            for (const FileEntry &e : listDir(path)) {
                if (e.type == core::EntryType::Parent)
                    continue;
                remove(join(path, e.name), true);
            }
            libssh2_sftp_rmdir(m_session->sftp(), path.toUtf8().constData());
        } else {
            if (libssh2_sftp_rmdir(m_session->sftp(), path.toUtf8().constData()) != 0)
                fail(QStringLiteral("Verzeichnis nicht leer oder gesperrt: %1").arg(path));
        }
    } else {
        if (libssh2_sftp_unlink(m_session->sftp(), path.toUtf8().constData()) != 0)
            fail(QStringLiteral("Löschen fehlgeschlagen: %1").arg(path));
    }
}

QByteArray SFTPFileSystem::readBytes(const QString &path, qint64 maxBytes)
{
    std::lock_guard<std::recursive_mutex> lock(m_session->mutex());
    LIBSSH2_SFTP *sftp = m_session->sftp();
    LIBSSH2_SFTP_HANDLE *handle =
        libssh2_sftp_open(sftp, path.toUtf8().constData(), LIBSSH2_FXF_READ, 0);
    if (!handle)
        fail(QStringLiteral("Kann Datei nicht lesen: %1").arg(path));
    QByteArray out;
    char buf[32768];
    while (out.size() < maxBytes) {
        const ssize_t n = libssh2_sftp_read(handle, buf,
                                            qMin<qint64>(sizeof(buf), maxBytes - out.size()));
        if (n <= 0)
            break;
        out.append(buf, n);
    }
    libssh2_sftp_close(handle);
    return out;
}

QString SFTPFileSystem::readText(const QString &path, qint64 maxBytes)
{
    return QString::fromUtf8(readBytes(path, maxBytes));
}

void SFTPFileSystem::writeBytes(const QString &path, const QByteArray &data)
{
    std::lock_guard<std::recursive_mutex> lock(m_session->mutex());
    LIBSSH2_SFTP *sftp = m_session->sftp();
    LIBSSH2_SFTP_HANDLE *handle = libssh2_sftp_open(
        sftp, path.toUtf8().constData(),
        LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC, 0644);
    if (!handle)
        fail(QStringLiteral("Kann Datei nicht schreiben: %1").arg(path));
    qint64 sent = 0;
    while (sent < data.size()) {
        const ssize_t n = libssh2_sftp_write(handle, data.constData() + sent, data.size() - sent);
        if (n < 0) {
            libssh2_sftp_close(handle);
            fail(QStringLiteral("Schreiben fehlgeschlagen: %1").arg(path));
        }
        sent += n;
    }
    libssh2_sftp_close(handle);
}

void SFTPFileSystem::writeText(const QString &path, const QString &content)
{
    writeBytes(path, content.toUtf8());
}

void SFTPFileSystem::rename(const QString &oldPath, const QString &newPath)
{
    std::lock_guard<std::recursive_mutex> lock(m_session->mutex());
    LIBSSH2_SFTP *sftp = m_session->sftp();
    libssh2_sftp_unlink(sftp, newPath.toUtf8().constData());  // best-effort overwrite
    if (libssh2_sftp_rename(sftp, oldPath.toUtf8().constData(), newPath.toUtf8().constData()) != 0)
        fail(QStringLiteral("Umbenennen fehlgeschlagen: %1 → %2").arg(oldPath, newPath));
}

void SFTPFileSystem::symlink(const QString &target, const QString &linkPath)
{
    std::lock_guard<std::recursive_mutex> lock(m_session->mutex());
    LIBSSH2_SFTP *sftp = m_session->sftp();
    // libssh2_sftp_symlink_ex(sftp, orig, orig_len, link, link_len, SYMLINK):
    // legt `link` an, das auf `orig` (= target) zeigt. Der link-Parameter ist
    // char* (nicht const) -> eigener veraenderbarer Puffer.
    const QByteArray orig = target.toUtf8();
    QByteArray link = linkPath.toUtf8();
    if (libssh2_sftp_symlink_ex(sftp, orig.constData(),
                                static_cast<unsigned int>(orig.size()), link.data(),
                                static_cast<unsigned int>(link.size()), LIBSSH2_SFTP_SYMLINK)
        != 0)
        fail(QStringLiteral("Symlink konnte nicht angelegt werden: %1 → %2")
                 .arg(linkPath, target));
}

void SFTPFileSystem::chmod(const QString &path, quint32 mode)
{
    std::lock_guard<std::recursive_mutex> lock(m_session->mutex());
    LIBSSH2_SFTP *sftp = m_session->sftp();
    LIBSSH2_SFTP_ATTRIBUTES attrs{};
    attrs.flags = LIBSSH2_SFTP_ATTR_PERMISSIONS;
    attrs.permissions = mode;
    if (libssh2_sftp_setstat(sftp, path.toUtf8().constData(), &attrs) != 0)
        fail(QStringLiteral("chmod fehlgeschlagen: %1").arg(path));
}

qint64 SFTPFileSystem::size(const QString &path)
{
    std::lock_guard<std::recursive_mutex> lock(m_session->mutex());
    LIBSSH2_SFTP *sftp = m_session->sftp();
    LIBSSH2_SFTP_ATTRIBUTES attrs;
    if (libssh2_sftp_stat(sftp, path.toUtf8().constData(), &attrs) != 0)
        return 0;
    return (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE) ? qint64(attrs.filesize) : 0;
}

QString SFTPFileSystem::join(const QString &path, const QString &name) const
{
    QString p = path;
    while (p.endsWith(QLatin1Char('/')) && p.size() > 1)
        p.chop(1);
    return (p == QLatin1String("/")) ? p + name : p + QLatin1Char('/') + name;
}

QString SFTPFileSystem::parent(const QString &path) const { return posixParent(path); }

QString SFTPFileSystem::basename(const QString &path) const
{
    return QDir::cleanPath(path).section(QLatin1Char('/'), -1);
}

QString SFTPFileSystem::home()
{
    std::lock_guard<std::recursive_mutex> lock(m_session->mutex());
    LIBSSH2_SFTP *sftp = m_session->sftp();
    char target[1024];
    const QString start = m_session->profile.startPath.isEmpty()
                              ? QStringLiteral(".") : m_session->profile.startPath;
    int n = libssh2_sftp_realpath(sftp, start.toUtf8().constData(), target, sizeof(target));
    if (n <= 0)
        n = libssh2_sftp_realpath(sftp, ".", target, sizeof(target));
    return n > 0 ? QString::fromUtf8(target, n) : QStringLiteral("/");
}

// ---------------------------------------------------------------------------
// RemoteCommandRunner
// ---------------------------------------------------------------------------

// POSIX-Shell-Quoting (Aequivalent zu shlex.quote).
static QString shQuote(const QString &s)
{
    if (s.isEmpty())
        return QStringLiteral("''");
    QString q = s;
    q.replace(QLatin1Char('\''), QLatin1String("'\"'\"'"));
    return QLatin1Char('\'') + q + QLatin1Char('\'');
}

RemoteCommandRunner::RemoteCommandRunner(SSHSessionPtr session) : m_session(std::move(session))
{
    label = m_session->label();
}

QString RemoteCommandRunner::wrap(const QString &command, const QString &cwd) const
{
    if (!cwd.isEmpty() && cwd != QLatin1String(".") && cwd != QString())
        return QStringLiteral("cd %1 && %2").arg(shQuote(cwd), command);
    return command;
}

void RemoteCommandRunner::stream(const QString &command, const QString &cwd,
                                 const LineCallback &onLine, const CancelTokenPtr &cancel)
{
    lastExitStatus.reset();
    const QString full = wrap(command, cwd);
    std::lock_guard<std::recursive_mutex> lock(m_session->mutex());
    LIBSSH2_SESSION *sess = m_session->raw();
    const int sock = m_session->socket();
    LIBSSH2_CHANNEL *channel = libssh2_channel_open_session(sess);
    if (!channel)
        fail(QStringLiteral("Kanal konnte nicht geöffnet werden: %1").arg(lastSshError(sess)));
    if (libssh2_channel_exec(channel, full.toUtf8().constData()) != 0) {
        libssh2_channel_free(channel);
        fail(QStringLiteral("Befehl fehlgeschlagen: %1").arg(lastSshError(sess)));
    }
    libssh2_session_set_blocking(sess, 0);
    QByteArray pending;
    const auto flush = [&](bool all) {
        int idx;
        while ((idx = pending.indexOf('\n')) >= 0) {
            QByteArray raw = pending.left(idx);
            pending.remove(0, idx + 1);
            while (raw.endsWith('\r')) raw.chop(1);
            onLine(QString::fromUtf8(raw));
        }
        if (all && !pending.isEmpty()) {
            onLine(QString::fromUtf8(pending));
            pending.clear();
        }
    };
    char buf[16384];
    bool cancelled = false;
    for (;;) {
        if (cancel && cancel->isCancelled()) { cancelled = true; break; }
        const ssize_t n = libssh2_channel_read(channel, buf, sizeof(buf));
        if (n == LIBSSH2_ERROR_EAGAIN) {
            if (libssh2_channel_eof(channel))
                break;
            waitSocket(sock, sess, 200);
            continue;
        }
        if (n < 0)
            break;
        if (n == 0) {
            if (libssh2_channel_eof(channel))
                break;
            continue;
        }
        pending.append(buf, n);
        flush(false);
    }
    // stderr nachziehen
    for (;;) {
        const ssize_t e = libssh2_channel_read_stderr(channel, buf, sizeof(buf));
        if (e == LIBSSH2_ERROR_EAGAIN) break;
        if (e <= 0) break;
        pending.append(buf, e);
    }
    flush(true);
    libssh2_session_set_blocking(sess, 1);
    if (cancelled)
        libssh2_channel_send_eof(channel);
    libssh2_channel_close(channel);
    lastExitStatus = cancelled ? -1 : libssh2_channel_get_exit_status(channel);
    libssh2_channel_free(channel);
}

std::optional<QString> RemoteCommandRunner::resolveDir(const QString &cwd, const QString &target)
{
    const QString full = QStringLiteral("cd %1 && cd %2 && pwd")
                             .arg(shQuote(cwd.isEmpty() ? QStringLiteral(".") : cwd),
                                  shQuote(target));
    try {
        const ExecResult r = m_session->exec(full);
        if (r.exitStatus == 0 && !r.out.isEmpty())
            return QString::fromUtf8(r.out).trimmed();
    } catch (...) {
    }
    return std::nullopt;
}

void RemoteCommandRunner::runTerminal(const QString &command, const QString &cwd,
                                      const LineCallback &onChunk, const CancelTokenPtr &cancel,
                                      int cols, int rows)
{
    lastExitStatus.reset();
    // Echtes PTY -> Programme geben Farben/Formatierung aus. Pager deaktivieren.
    const QString prefix =
        QStringLiteral("export PAGER=cat GIT_PAGER=cat SYSTEMD_PAGER=cat 2>/dev/null; ");
    const QString full = prefix + wrap(command, cwd);
    std::lock_guard<std::recursive_mutex> lock(m_session->mutex());
    LIBSSH2_SESSION *sess = m_session->raw();
    const int sock = m_session->socket();
    LIBSSH2_CHANNEL *channel = libssh2_channel_open_session(sess);
    if (!channel)
        fail(QStringLiteral("Kanal konnte nicht geöffnet werden: %1").arg(lastSshError(sess)));
    libssh2_channel_request_pty_ex(channel, "xterm-256color", 14, nullptr, 0, cols, rows, 0, 0);
    if (libssh2_channel_exec(channel, full.toUtf8().constData()) != 0) {
        libssh2_channel_free(channel);
        fail(QStringLiteral("Befehl fehlgeschlagen: %1").arg(lastSshError(sess)));
    }
    libssh2_session_set_blocking(sess, 0);
    char buf[8192];
    bool cancelled = false;
    for (;;) {
        if (cancel && cancel->isCancelled()) { cancelled = true; break; }
        const ssize_t n = libssh2_channel_read(channel, buf, sizeof(buf));
        if (n == LIBSSH2_ERROR_EAGAIN) {
            if (libssh2_channel_eof(channel))
                break;
            waitSocket(sock, sess, 200);
            continue;
        }
        if (n <= 0) {
            if (libssh2_channel_eof(channel))
                break;
            continue;
        }
        onChunk(QString::fromUtf8(buf, n));
    }
    libssh2_session_set_blocking(sess, 1);
    if (!cancelled)
        lastExitStatus = libssh2_channel_get_exit_status(channel);
    else
        libssh2_channel_send_eof(channel);
    libssh2_channel_close(channel);
    libssh2_channel_free(channel);
}

// ---------------------------------------------------------------------------
// RemoteShell (interaktives PTY)
// ---------------------------------------------------------------------------

std::unique_ptr<RemoteShell> RemoteShell::open(SSHSessionPtr session, int cols, int rows)
{
    std::lock_guard<std::recursive_mutex> lock(session->mutex());
    LIBSSH2_SESSION *sess = session->raw();
    LIBSSH2_CHANNEL *channel = libssh2_channel_open_session(sess);
    if (!channel)
        fail(QStringLiteral("Shell-Kanal konnte nicht geöffnet werden: %1").arg(lastSshError(sess)));
    libssh2_channel_request_pty_ex(channel, "xterm-256color", 14, nullptr, 0, cols, rows, 0, 0);
    if (libssh2_channel_shell(channel) != 0) {
        libssh2_channel_free(channel);
        fail(QStringLiteral("Shell konnte nicht gestartet werden: %1").arg(lastSshError(sess)));
    }
    auto shell = std::unique_ptr<RemoteShell>(new RemoteShell(std::move(session)));
    shell->m_channel = channel;
    return shell;
}

RemoteShell::~RemoteShell()
{
    close();
}

void RemoteShell::close()
{
    if (m_closed || !m_channel)
        return;
    m_closed = true;
    std::lock_guard<std::recursive_mutex> lock(m_session->mutex());
    auto *channel = static_cast<LIBSSH2_CHANNEL *>(m_channel);
    libssh2_channel_close(channel);
    libssh2_channel_free(channel);
    m_channel = nullptr;
}

void RemoteShell::write(const QByteArray &data)
{
    if (m_closed || !m_channel)
        return;
    std::lock_guard<std::recursive_mutex> lock(m_session->mutex());
    auto *channel = static_cast<LIBSSH2_CHANNEL *>(m_channel);
    qint64 sent = 0;
    while (sent < data.size()) {
        const ssize_t n = libssh2_channel_write(channel, data.constData() + sent, data.size() - sent);
        if (n == LIBSSH2_ERROR_EAGAIN) {
            waitSocket(m_session->socket(), m_session->raw(), 200);
            continue;
        }
        if (n < 0)
            break;
        sent += n;
    }
}

QByteArray RemoteShell::read(int maxBytes, int timeoutMs)
{
    if (m_closed || !m_channel)
        return {};
    QByteArray out(maxBytes, Qt::Uninitialized);
    ssize_t n;
    {
        std::lock_guard<std::recursive_mutex> lock(m_session->mutex());
        auto *channel = static_cast<LIBSSH2_CHANNEL *>(m_channel);
        libssh2_session_set_blocking(m_session->raw(), 0);
        n = libssh2_channel_read(channel, out.data(), maxBytes);
        libssh2_session_set_blocking(m_session->raw(), 1);
    }
    if (n == LIBSSH2_ERROR_EAGAIN) {
        // Lock zwischen den Poll-Zyklen freigeben, damit SFTP parallel laeuft.
        waitSocket(m_session->socket(), m_session->raw(), timeoutMs);
        return {};
    }
    if (n <= 0)
        return {};
    out.resize(n);
    return out;
}

void RemoteShell::resize(int cols, int rows)
{
    if (m_closed || !m_channel)
        return;
    std::lock_guard<std::recursive_mutex> lock(m_session->mutex());
    auto *channel = static_cast<LIBSSH2_CHANNEL *>(m_channel);
    libssh2_channel_request_pty_size(channel, cols, rows);
}

} // namespace ncssh::net
