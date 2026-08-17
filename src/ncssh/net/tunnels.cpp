#include "ncssh/net/tunnels.hpp"

#include <libssh2.h>
#include <mutex>
#include <stdexcept>

#ifdef Q_OS_WIN
#  include <winsock2.h>
#  include <ws2tcpip.h>
using socklen_t = int;
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <unistd.h>
#  include <fcntl.h>
#endif

namespace ncssh::net {

static void closeSock(int s)
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

// Lauscht auf host:port (fuer -L / -D).
static int listenLocal(const QString &host, int port)
{
    int sock = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
    if (sock < 0)
        fail("Socket konnte nicht erstellt werden.");
    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&yes), sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, host.toUtf8().constData(), &addr.sin_addr);
    if (::bind(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0
        || ::listen(sock, 16) != 0) {
        closeSock(sock);
        fail(QStringLiteral("Port %1 konnte nicht gebunden werden.").arg(port));
    }
    return sock;
}

// Verbindet lokal zu host:port (fuer -R Zielseite).
static int connectLocal(const QString &host, int port)
{
    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *res = nullptr;
    if (getaddrinfo(host.toUtf8().constData(), QByteArray::number(port).constData(),
                    &hints, &res) != 0 || !res)
        return -1;
    int sock = -1;
    for (auto *ai = res; ai; ai = ai->ai_next) {
        sock = static_cast<int>(::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol));
        if (sock < 0)
            continue;
        if (::connect(sock, ai->ai_addr, static_cast<socklen_t>(ai->ai_addrlen)) == 0)
            break;
        closeSock(sock);
        sock = -1;
    }
    freeaddrinfo(res);
    return sock;
}

// Pumpt Daten bidirektional zwischen einem lokalen Socket und einem libssh2-
// Kanal. Die Kanal-I/O laeuft unter dem Session-Lock (nicht thread-safe).
static void pump(int sock, LIBSSH2_CHANNEL *channel, SSHSession *session,
                 std::atomic_bool &stop)
{
    LIBSSH2_SESSION *sess = session->raw();
#ifdef Q_OS_WIN
    u_long nb = 1;
    ioctlsocket(static_cast<SOCKET>(sock), FIONBIO, &nb);
#else
    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);
#endif
    char buf[16384];
    while (!stop.load()) {
        // Session schliesst (Trennen/App-Ende): sofort aussteigen, bevor auf
        // freigegebene libssh2-Objekte zugegriffen wird.
        if (session->closing)
            break;
        // Socket -> Kanal
        const int r = ::recv(sock, buf, sizeof(buf), 0);
        if (r > 0) {
            std::lock_guard<std::recursive_mutex> lock(session->mutex());
            libssh2_session_set_blocking(sess, 1);
            int sent = 0;
            while (sent < r) {
                const ssize_t w = libssh2_channel_write(channel, buf + sent, r - sent);
                if (w < 0)
                    break;
                sent += w;
            }
        } else if (r == 0) {
            break;  // lokale Seite geschlossen
        }
        // Kanal -> Socket
        {
            std::lock_guard<std::recursive_mutex> lock(session->mutex());
            libssh2_session_set_blocking(sess, 0);
            const ssize_t n = libssh2_channel_read(channel, buf, sizeof(buf));
            libssh2_session_set_blocking(sess, 1);
            if (n > 0) {
                int sent = 0;
                while (sent < n) {
                    const int w = ::send(sock, buf + sent, static_cast<int>(n - sent), 0);
                    if (w <= 0)
                        break;
                    sent += w;
                }
            } else if (n == 0 && libssh2_channel_eof(channel)) {
                break;
            }
        }
        if (r <= 0) {
#ifdef Q_OS_WIN
            Sleep(5);
#else
            usleep(5000);
#endif
        }
    }
    {
        std::lock_guard<std::recursive_mutex> lock(session->mutex());
        if (!session->closing && session->raw()) {
            libssh2_channel_close(channel);
            libssh2_channel_free(channel);
        }
    }
    closeSock(sock);
}

// Reine Adress-Extraktion (ab ATYP) — die fehleranfaellige Byte-Logik, damit
// sie ohne Server getestet werden kann.
bool parseSocks5Target(const QByteArray &data, QString &host, int &port)
{
    if (data.isEmpty())
        return false;
    const auto *b = reinterpret_cast<const unsigned char *>(data.constData());
    const int len = data.size();
    const int atyp = b[0];
    int pos = 1;
    if (atyp == 0x01) {  // IPv4
        if (len < pos + 4 + 2)
            return false;
        host = QStringLiteral("%1.%2.%3.%4")
                   .arg(b[pos]).arg(b[pos + 1]).arg(b[pos + 2]).arg(b[pos + 3]);
        pos += 4;
    } else if (atyp == 0x03) {  // Domain (1 Byte Laenge + n Bytes)
        if (len < pos + 1)
            return false;
        const int dlen = b[pos++];
        if (len < pos + dlen + 2)
            return false;
        host = QString::fromLatin1(data.constData() + pos, dlen);
        pos += dlen;
    } else if (atyp == 0x04) {  // IPv6
        if (len < pos + 16 + 2)
            return false;
        QStringList groups;
        for (int i = 0; i < 16; i += 2)
            groups << QString::number((b[pos + i] << 8) | b[pos + i + 1], 16);
        host = groups.join(QLatin1Char(':'));
        pos += 16;
    } else {
        return false;
    }
    // Port big-endian.
    port = (b[pos] << 8) | b[pos + 1];
    return true;
}

// SOCKS5-Handshake auf einem eingehenden Socket -> (destHost, destPort).
static bool socks5Handshake(int sock, QString &destHost, int &destPort)
{
    unsigned char buf[262];
    int n = ::recv(sock, reinterpret_cast<char *>(buf), 2, 0);
    if (n < 2 || buf[0] != 0x05)
        return false;
    const int nmethods = buf[1];
    ::recv(sock, reinterpret_cast<char *>(buf), nmethods, 0);
    unsigned char noauth[2] = {0x05, 0x00};
    ::send(sock, reinterpret_cast<const char *>(noauth), 2, 0);

    n = ::recv(sock, reinterpret_cast<char *>(buf), 4, 0);
    if (n < 4 || buf[1] != 0x01)  // nur CONNECT
        return false;
    const int atyp = buf[3];

    // Adressteil (ab ATYP) in einen Puffer sammeln und rein parsen.
    QByteArray tail(1, char(atyp));
    if (atyp == 0x01) {  // IPv4
        if (::recv(sock, reinterpret_cast<char *>(buf), 4, 0) < 4)
            return false;
        tail.append(reinterpret_cast<char *>(buf), 4);
    } else if (atyp == 0x03) {  // Domain
        unsigned char dlen = 0;
        if (::recv(sock, reinterpret_cast<char *>(&dlen), 1, 0) < 1)
            return false;
        if (::recv(sock, reinterpret_cast<char *>(buf), dlen, 0) < dlen)
            return false;
        tail.append(char(dlen));
        tail.append(reinterpret_cast<char *>(buf), dlen);
    } else {
        return false;
    }
    unsigned char portb[2];
    if (::recv(sock, reinterpret_cast<char *>(portb), 2, 0) < 2)
        return false;
    tail.append(reinterpret_cast<char *>(portb), 2);

    if (!parseSocks5Target(tail, destHost, destPort))
        return false;
    unsigned char reply[10] = {0x05, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0};
    ::send(sock, reinterpret_cast<const char *>(reply), 10, 0);
    return true;
}

// ---------------------------------------------------------------------------

Tunnel::Tunnel(SSHSessionPtr session, core::TunnelSpec spec)
    : m_session(std::move(session)), m_spec(std::move(spec))
{
}

Tunnel::~Tunnel()
{
    stop();
}

void Tunnel::start()
{
    if (m_spec.kind == QLatin1String("remote")) {
        // forward_listen auf dem Server anfordern (blockierend, Session-Lock).
        std::lock_guard<std::recursive_mutex> lock(m_session->mutex());
        int boundPort = 0;
        LIBSSH2_LISTENER *listener = libssh2_channel_forward_listen_ex(
            m_session->raw(), m_spec.listenHost.toUtf8().constData(),
            m_spec.listenPort, &boundPort, 16);
        if (!listener)
            fail("Remote-Weiterleitung wurde vom Server abgelehnt.");
        m_listener = listener;
        m_thread = std::thread([this] { runRemote(); });
    } else {
        m_listenSocket = listenLocal(m_spec.listenHost, m_spec.listenPort);
        m_thread = std::thread([this] { runLocalOrDynamic(); });
    }
}

void Tunnel::stop()
{
    if (m_stop.exchange(true))
        return;
    closeSock(m_listenSocket);
    m_listenSocket = -1;
    if (m_listener) {
        std::lock_guard<std::recursive_mutex> lock(m_session->mutex());
        if (!m_session->closing && m_session->raw())
            libssh2_channel_forward_cancel(static_cast<LIBSSH2_LISTENER *>(m_listener));
        m_listener = nullptr;
    }
    if (m_thread.joinable())
        m_thread.join();
    for (auto &w : m_workers) {
        if (w.joinable())
            w.join();
    }
    m_workers.clear();
}

void Tunnel::runLocalOrDynamic()
{
    const bool dynamic = (m_spec.kind == QLatin1String("dynamic"));
    while (!m_stop.load()) {
        sockaddr_in peer{};
        socklen_t plen = sizeof(peer);
        const int client =
            static_cast<int>(::accept(m_listenSocket, reinterpret_cast<sockaddr *>(&peer), &plen));
        if (client < 0)
            break;
        QString destHost = m_spec.destHost;
        int destPort = m_spec.destPort;
        if (dynamic && !socks5Handshake(client, destHost, destPort)) {
            closeSock(client);
            continue;
        }
        LIBSSH2_CHANNEL *channel = nullptr;
        {
            std::lock_guard<std::recursive_mutex> lock(m_session->mutex());
            if (m_session->closing || !m_session->raw()) {
                closeSock(client);
                break;
            }
            libssh2_session_set_blocking(m_session->raw(), 1);
            channel = libssh2_channel_direct_tcpip_ex(
                m_session->raw(), destHost.toUtf8().constData(), destPort,
                m_spec.listenHost.toUtf8().constData(), m_spec.listenPort);
        }
        if (!channel) {
            closeSock(client);
            continue;
        }
        SSHSession *sess = m_session.get();
        m_workers.emplace_back([client, channel, sess, this] {
            pump(client, channel, sess, m_stop);
        });
    }
}

void Tunnel::runRemote()
{
    auto *listener = static_cast<LIBSSH2_LISTENER *>(m_listener);
    while (!m_stop.load()) {
        LIBSSH2_CHANNEL *channel = nullptr;
        {
            std::lock_guard<std::recursive_mutex> lock(m_session->mutex());
            if (m_session->closing || !m_session->raw())
                break;
            libssh2_session_set_blocking(m_session->raw(), 0);
            channel = libssh2_channel_forward_accept(listener);
            libssh2_session_set_blocking(m_session->raw(), 1);
        }
        if (!channel) {
#ifdef Q_OS_WIN
            Sleep(50);
#else
            usleep(50000);
#endif
            continue;
        }
        const int local = connectLocal(m_spec.destHost, m_spec.destPort);
        if (local < 0) {
            std::lock_guard<std::recursive_mutex> lock(m_session->mutex());
            libssh2_channel_free(channel);
            continue;
        }
        SSHSession *sess = m_session.get();
        m_workers.emplace_back([local, channel, sess, this] {
            pump(local, channel, sess, m_stop);
        });
    }
}

std::unique_ptr<Tunnel> openTunnel(SSHSessionPtr session, const core::TunnelSpec &spec)
{
    if (spec.kind != QLatin1String("local") && spec.kind != QLatin1String("remote")
        && spec.kind != QLatin1String("dynamic"))
        fail(QStringLiteral("Unbekannter Tunnel-Typ: %1").arg(spec.kind));
    auto tunnel = std::make_unique<Tunnel>(std::move(session), spec);
    tunnel->start();
    return tunnel;
}

} // namespace ncssh::net
