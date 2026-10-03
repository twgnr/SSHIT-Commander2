#include "ncssh/net/tunnels.hpp"

#include <QByteArray>
#include <algorithm>
#include <chrono>
#include <libssh2.h>
#include <mutex>
#include <stdexcept>

#ifdef Q_OS_WIN
#  include <winsock2.h>
#  include <ws2tcpip.h>
using socklen_t = int;
using SockT = SOCKET;
#else
#  include <sys/socket.h>
#  include <sys/select.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <cerrno>
using SockT = int;
#endif

namespace ncssh::net {

using Clock = std::chrono::steady_clock;

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

static void setNonBlocking(int sock)
{
#ifdef Q_OS_WIN
    u_long nb = 1;
    ioctlsocket(static_cast<SOCKET>(sock), FIONBIO, &nb);
#else
    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);
#endif
}

// Nicht-blockierender Socket hat gerade nichts (kein echter Fehler).
static bool sockWouldBlock()
{
#ifdef Q_OS_WIN
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EWOULDBLOCK || errno == EAGAIN;
#endif
}

static bool connectInProgress()
{
#ifdef Q_OS_WIN
    const int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
#else
    return errno == EINPROGRESS;
#endif
}

// select() auf bis zu zwei Sockets; nie mit leeren Mengen (Windows liefert
// dann sofort einen Fehler -> die Aufrufer wuerden drehen).
static void waitSockets(int a, bool aRead, bool aWrite, int b, bool bRead, bool bWrite,
                        int timeoutMs)
{
    fd_set fr, fw;
    FD_ZERO(&fr);
    FD_ZERO(&fw);
    int maxFd = -1;
    bool any = false;
    const auto add = [&](int s, bool r, bool w) {
        if (s < 0 || (!r && !w))
            return;
        if (r)
            FD_SET(static_cast<SockT>(s), &fr);
        if (w)
            FD_SET(static_cast<SockT>(s), &fw);
        maxFd = std::max(maxFd, s);
        any = true;
    };
    add(a, aRead, aWrite);
    add(b, bRead, bWrite);
    if (!any) {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
        return;
    }
    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    ::select(maxFd + 1, &fr, &fw, nullptr, &tv);
}

// Liest genau n Bytes von einem nicht-blockierenden Socket — mit Frist und
// Abbruch. Ein stummer SOCKS-Client blockierte frueher mit recv() ohne
// Timeout den Accept-Thread und damit stop()/join (Tab-Schliessen, App-Ende).
static bool recvExact(int sock, char *buf, int n, const std::atomic_bool &stop,
                      Clock::time_point deadline)
{
    int got = 0;
    while (got < n) {
        if (stop.load() || Clock::now() >= deadline)
            return false;
        const int r = ::recv(sock, buf + got, n - got, 0);
        if (r > 0) {
            got += r;
            continue;
        }
        if (r == 0 || !sockWouldBlock())
            return false;
        waitSockets(sock, true, false, -1, false, false, 100);
    }
    return true;
}

static bool sendAll(int sock, const char *buf, int n, const std::atomic_bool &stop,
                    Clock::time_point deadline)
{
    int sent = 0;
    while (sent < n) {
        if (stop.load() || Clock::now() >= deadline)
            return false;
        const int w = ::send(sock, buf + sent, n - sent, 0);
        if (w > 0) {
            sent += w;
            continue;
        }
        if (w == 0 || !sockWouldBlock())
            return false;
        waitSockets(sock, false, true, -1, false, false, 100);
    }
    return true;
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
    // Nicht blockierend: meldet select() "lesbar", der Client hat aber schon
    // wieder abgebrochen, blockierte accept() sonst — und stop() (Tab
    // schliessen, Beenden) haenge bis zum naechsten Client.
    setNonBlocking(sock);
    return sock;
}

// Verbindet lokal zu host:port (fuer -R Zielseite). Nicht-blockierend mit
// Frist und Abbruch: ein blockierendes connect() haengt unter Windows bei
// unerreichbaren Zielen ~21 s je Adresse — so lange hinge auch stop().
static int connectLocal(const QString &host, int port, const std::atomic_bool &stop,
                        int timeoutMs = 10000)
{
    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *res = nullptr;
    if (getaddrinfo(host.toUtf8().constData(), QByteArray::number(port).constData(),
                    &hints, &res) != 0 || !res)
        return -1;
    int sock = -1;
    for (auto *ai = res; ai && !stop.load(); ai = ai->ai_next) {
        sock = static_cast<int>(::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol));
        if (sock < 0)
            continue;
        setNonBlocking(sock);
        bool ok = ::connect(sock, ai->ai_addr, static_cast<socklen_t>(ai->ai_addrlen)) == 0;
        if (!ok && connectInProgress()) {
            for (int waited = 0; waited < timeoutMs && !stop.load(); waited += 100) {
                fd_set fw, fe;
                FD_ZERO(&fw);
                FD_ZERO(&fe);
                FD_SET(static_cast<SockT>(sock), &fw);
                FD_SET(static_cast<SockT>(sock), &fe);
                struct timeval tv;
                tv.tv_sec = 0;
                tv.tv_usec = 100 * 1000;
                const int s = ::select(sock + 1, nullptr, &fw, &fe, &tv);
                if (s < 0)
                    break;
                if (s == 0)
                    continue;
                int err = 0;
                socklen_t len = sizeof(err);
                getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&err), &len);
                ok = err == 0 && FD_ISSET(static_cast<SockT>(sock), &fw);
                break;
            }
        }
        if (ok)
            break;
        closeSock(sock);
        sock = -1;
    }
    freeaddrinfo(res);
    return sock;
}

// Pumpt Daten bidirektional zwischen einem lokalen Socket und einem libssh2-
// Kanal. Regeln:
//  - Kanal-I/O nur unter dem Session-Lock, NICHT-blockierend (blocking wird
//    vor dem Freigeben auf 1 zurueckgesetzt) — ein blockierendes Schreiben
//    unter dem Lock legte frueher bei vollem Kanalfenster die ganze Session
//    lahm.
//  - Teil-Schreibvorgaenge in BEIDE Richtungen werden gepuffert und spaeter
//    fortgesetzt. Frueher gingen bei EAGAIN/WSAEWOULDBLOCK die Restbytes
//    verloren — der Datenstrom war still kaputt.
//  - Echte Socket-Fehler (WSAECONNRESET ...) beenden die Schleife, statt als
//    "keine Daten" endlos alle 5 ms zu pollen.
//  - Gewartet wird ohne Lock auf beide Sockets (lokal + SSH).
static void pump(int sock, LIBSSH2_CHANNEL *channel, SSHSession *session,
                 const std::atomic_bool &stop)
{
    LIBSSH2_SESSION *sess = nullptr;
    {
        std::lock_guard<std::recursive_mutex> lock(session->mutex());
        sess = session->raw();
    }
    setNonBlocking(sock);

    // Obergrenze je Richtung: bei vollem Puffer wird die Quelle nicht weiter
    // gelesen (Gegendruck), statt unbegrenzt Speicher zu belegen.
    constexpr int kMaxPending = 256 * 1024;
    // Nach einseitigem Ende (EOF in eine Richtung) hoechstens so lange ohne
    // Fortschritt weiterlaufen — sonst bliebe ein Worker mit halb
    // geschlossener Verbindung ewig haengen.
    constexpr auto kHalfCloseIdle = std::chrono::seconds(30);

    QByteArray toChannel;   // vom lokalen Socket gelesen, noch nicht im Kanal
    QByteArray toSocket;    // aus dem Kanal gelesen, noch nicht im Socket
    bool sockEof = false;   // lokale Seite sendet nichts mehr
    bool chanEof = false;   // Gegenseite sendet nichts mehr
    bool eofSent = false;   // EOF an die Gegenseite weitergegeben
    bool sockShut = false;  // Sende-Richtung des lokalen Sockets geschlossen
    bool sessionGone = false;
    Clock::time_point lastProgress = Clock::now();
    char buf[16384];

    while (!stop.load()) {
        bool progress = false;
        bool failed = false;

        // 1) Lokaler Socket -> Puffer
        if (!sockEof && toChannel.size() < kMaxPending) {
            const int r = ::recv(sock, buf, sizeof(buf), 0);
            if (r > 0) {
                toChannel.append(buf, r);
                progress = true;
            } else if (r == 0) {
                sockEof = true;
                progress = true;
            } else if (!sockWouldBlock()) {
                break;   // echter Fehler (z. B. Verbindung zurueckgesetzt)
            }
        }

        // 2)+3) Kanal-I/O in einem Lock-Block
        int dir = 0;
        int sshSock = -1;
        {
            std::lock_guard<std::recursive_mutex> lock(session->mutex());
            // Session schliesst (Trennen/App-Ende): sofort aussteigen, bevor auf
            // freigegebene libssh2-Objekte zugegriffen wird.
            if (session->closing || session->raw() != sess || !sess) {
                sessionGone = true;
                break;
            }
            libssh2_session_set_blocking(sess, 0);
            while (!toChannel.isEmpty()) {
                const ssize_t w = libssh2_channel_write(channel, toChannel.constData(),
                                                        static_cast<size_t>(toChannel.size()));
                if (w == LIBSSH2_ERROR_EAGAIN)
                    break;   // Rest bleibt im Puffer und wird spaeter gesendet
                if (w < 0) {
                    failed = true;
                    break;
                }
                toChannel.remove(0, static_cast<qsizetype>(w));
                progress = true;
            }
            if (!failed && sockEof && toChannel.isEmpty() && !eofSent) {
                const int rc = libssh2_channel_send_eof(channel);
                if (rc == 0)
                    eofSent = true;
                else if (rc != LIBSSH2_ERROR_EAGAIN)
                    failed = true;
            }
            if (!failed && !chanEof && toSocket.size() < kMaxPending) {
                const ssize_t n = libssh2_channel_read(channel, buf, sizeof(buf));
                if (n > 0) {
                    toSocket.append(buf, static_cast<qsizetype>(n));
                    progress = true;
                } else if (n == 0 || n == LIBSSH2_ERROR_EAGAIN) {
                    if (libssh2_channel_eof(channel)) {
                        chanEof = true;
                        progress = true;
                    } else if (n == 0) {
                        // 0 ohne EOF: Transportfehler (libssh2 meldet ihn so)
                        // oder Daten auf einem anderen Teilstrom.
                        const int e = libssh2_session_last_errno(sess);
                        if (e == LIBSSH2_ERROR_SOCKET_DISCONNECT
                            || e == LIBSSH2_ERROR_SOCKET_RECV
                            || e == LIBSSH2_ERROR_SOCKET_SEND)
                            failed = true;
                    }
                } else {
                    failed = true;
                }
            }
            dir = libssh2_session_block_directions(sess);
            libssh2_session_set_blocking(sess, 1);
            sshSock = session->socket();
        }
        if (failed)
            break;

        // 4) Puffer -> lokaler Socket (Teil-Sends: Rest bleibt gepuffert)
        while (!toSocket.isEmpty()) {
            const int w = ::send(sock, toSocket.constData(), static_cast<int>(toSocket.size()), 0);
            if (w > 0) {
                toSocket.remove(0, w);
                progress = true;
                continue;
            }
            if (w < 0 && sockWouldBlock())
                break;
            failed = true;
            break;
        }
        if (failed)
            break;

        // Gegenseite fertig und alles zugestellt: Sende-Richtung lokal
        // schliessen, damit der Client das Ende sieht.
        if (chanEof && toSocket.isEmpty() && !sockShut) {
#ifdef Q_OS_WIN
            ::shutdown(static_cast<SOCKET>(sock), SD_SEND);
#else
            ::shutdown(sock, SHUT_WR);
#endif
            sockShut = true;
        }
        // Beide Richtungen beendet und alles uebertragen -> fertig.
        if (chanEof && toSocket.isEmpty() && sockEof && toChannel.isEmpty())
            break;

        const Clock::time_point now = Clock::now();
        if (progress) {
            lastProgress = now;
            continue;
        }
        if ((chanEof || eofSent) && now - lastProgress > kHalfCloseIdle)
            break;

        // Nichts ging voran: OHNE Lock warten — lokal lesen (falls Platz),
        // lokal schreiben (falls etwas ansteht), SSH in libssh2s Richtung.
        const bool wantLocalRead = !sockEof && toChannel.size() < kMaxPending;
        const bool wantLocalWrite = !toSocket.isEmpty();
        const bool sshWrite = (dir & LIBSSH2_SESSION_BLOCK_OUTBOUND) != 0;
        const bool sshRead = !sshWrite || (dir & LIBSSH2_SESSION_BLOCK_INBOUND) != 0;
        waitSockets(sock, wantLocalRead, wantLocalWrite, sshSock, sshRead, sshWrite, 50);
    }
    {
        std::lock_guard<std::recursive_mutex> lock(session->mutex());
        // Kanaele einer sterbenden Session nicht anfassen — libssh2_session_free
        // raeumt sie ab.
        if (!sessionGone && !session->closing && session->raw() == sess && sess) {
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

// SOCKS5-Handshake auf einem eingehenden (nicht-blockierenden) Socket ->
// (destHost, destPort). Laeuft im Worker der Verbindung, mit Gesamtfrist und
// Abbruch per stop — ein stummer Client haelt weder weitere Verbindungen
// noch stop() auf.
static bool socks5Handshake(int sock, QString &destHost, int &destPort,
                            const std::atomic_bool &stop)
{
    const Clock::time_point deadline = Clock::now() + std::chrono::seconds(10);
    char buf[262];
    if (!recvExact(sock, buf, 2, stop, deadline) || static_cast<unsigned char>(buf[0]) != 0x05)
        return false;
    const int nmethods = static_cast<unsigned char>(buf[1]);
    if (!recvExact(sock, buf, nmethods, stop, deadline))
        return false;
    const char noauth[2] = {0x05, 0x00};
    if (!sendAll(sock, noauth, 2, stop, deadline))
        return false;

    if (!recvExact(sock, buf, 4, stop, deadline) || buf[1] != 0x01)  // nur CONNECT
        return false;
    const int atyp = static_cast<unsigned char>(buf[3]);

    // Adressteil (ab ATYP) in einen Puffer sammeln und rein parsen.
    QByteArray tail(1, char(atyp));
    if (atyp == 0x01) {  // IPv4
        if (!recvExact(sock, buf, 4, stop, deadline))
            return false;
        tail.append(buf, 4);
    } else if (atyp == 0x03) {  // Domain
        char dlen = 0;
        if (!recvExact(sock, &dlen, 1, stop, deadline))
            return false;
        const int n = static_cast<unsigned char>(dlen);
        if (!recvExact(sock, buf, n, stop, deadline))
            return false;
        tail.append(dlen);
        tail.append(buf, n);
    } else if (atyp == 0x04) {  // IPv6
        if (!recvExact(sock, buf, 16, stop, deadline))
            return false;
        tail.append(buf, 16);
    } else {
        return false;
    }
    char portb[2];
    if (!recvExact(sock, portb, 2, stop, deadline))
        return false;
    tail.append(portb, 2);

    if (!parseSocks5Target(tail, destHost, destPort))
        return false;
    const char reply[10] = {0x05, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0};
    return sendAll(sock, reply, 10, stop, deadline);
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
        if (m_session->closing || !m_session->raw())
            fail("Sitzung geschlossen.");
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
    // Der Accept-Thread prueft m_stop spaetestens alle 200 ms und baut seinen
    // Listener (lokal: Socket, remote: libssh2-Listener) danach selbst ab.
    if (m_thread.joinable())
        m_thread.join();
    closeSock(m_listenSocket);
    m_listenSocket = -1;
    if (m_listener) {
        // Nur wenn der Accept-Thread nie lief — dann gibt es keinen Wettlauf.
        std::lock_guard<std::recursive_mutex> lock(m_session->mutex());
        if (!m_session->closing && m_session->raw())
            libssh2_channel_forward_cancel(static_cast<LIBSSH2_LISTENER *>(m_listener));
        m_listener = nullptr;
    }
    // Worker beenden sich selbst: pump/Handshake/connect pruefen m_stop in
    // kurzen Abstaenden.
    for (auto &w : m_workers) {
        if (w.thread.joinable())
            w.thread.join();
    }
    m_workers.clear();
}

void Tunnel::spawnWorker(std::function<void()> fn)
{
    auto done = std::make_shared<std::atomic_bool>(false);
    Worker w;
    w.done = done;
    w.thread = std::thread([fn = std::move(fn), done] {
        fn();
        done->store(true);
    });
    m_workers.push_back(std::move(w));
}

void Tunnel::reapWorkers()
{
    for (auto it = m_workers.begin(); it != m_workers.end();) {
        if (it->done->load()) {
            if (it->thread.joinable())
                it->thread.join();   // sofort: der Thread ist fertig
            it = m_workers.erase(it);
        } else {
            ++it;
        }
    }
}

void Tunnel::runLocalOrDynamic()
{
    const bool dynamic = (m_spec.kind == QLatin1String("dynamic"));
    const int listenSock = m_listenSocket;
    while (!m_stop.load()) {
        reapWorkers();
        if (m_session->closing)
            break;
        // Mit Frist auf neue Verbindungen warten: so werden m_stop und fertige
        // Worker regelmaessig geprueft, auch wenn niemand verbindet.
        fd_set fr;
        FD_ZERO(&fr);
        FD_SET(static_cast<SockT>(listenSock), &fr);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200 * 1000;
        const int rc = ::select(listenSock + 1, &fr, nullptr, nullptr, &tv);
        if (rc < 0)
            break;
        if (rc == 0)
            continue;
        sockaddr_in peer{};
        socklen_t plen = sizeof(peer);
        const int client =
            static_cast<int>(::accept(listenSock, reinterpret_cast<sockaddr *>(&peer), &plen));
        if (client < 0) {
            if (sockWouldBlock())
                continue;
            break;
        }
        // Handshake und Kanal-Aufbau im Worker: der Accept-Thread darf an
        // keinem einzelnen Client haengen bleiben.
        spawnWorker([this, client, dynamic] {
            setNonBlocking(client);
            QString destHost = m_spec.destHost;
            int destPort = m_spec.destPort;
            if (dynamic && !socks5Handshake(client, destHost, destPort, m_stop)) {
                closeSock(client);
                return;
            }
            LIBSSH2_CHANNEL *channel = nullptr;
            {
                std::lock_guard<std::recursive_mutex> lock(m_session->mutex());
                if (m_stop.load() || m_session->closing || !m_session->raw()) {
                    closeSock(client);
                    return;
                }
                libssh2_session_set_blocking(m_session->raw(), 1);
                channel = libssh2_channel_direct_tcpip_ex(
                    m_session->raw(), destHost.toUtf8().constData(), destPort,
                    m_spec.listenHost.toUtf8().constData(), m_spec.listenPort);
            }
            if (!channel) {
                closeSock(client);
                return;
            }
            pump(client, channel, m_session.get(), m_stop);
        });
    }
}

void Tunnel::runRemote()
{
    auto *listener = static_cast<LIBSSH2_LISTENER *>(m_listener);
    for (;;) {
        reapWorkers();
        LIBSSH2_CHANNEL *channel = nullptr;
        {
            std::lock_guard<std::recursive_mutex> lock(m_session->mutex());
            // m_stop UNTER dem Lock pruefen, direkt vor der Nutzung des
            // Listeners. Abgebaut wird er nur hier im selben Thread (s. u.).
            if (m_stop.load() || m_session->closing || !m_session->raw())
                break;
            libssh2_session_set_blocking(m_session->raw(), 0);
            channel = libssh2_channel_forward_accept(listener);
            libssh2_session_set_blocking(m_session->raw(), 1);
        }
        if (!channel) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        // Lokales Ziel im Worker verbinden (mit Frist) — ein haengendes
        // connect() blockiert so weder weitere Verbindungen noch stop().
        spawnWorker([this, channel] {
            const int local = connectLocal(m_spec.destHost, m_spec.destPort, m_stop);
            if (local < 0) {
                std::lock_guard<std::recursive_mutex> lock(m_session->mutex());
                if (!m_session->closing && m_session->raw())
                    libssh2_channel_free(channel);
                return;
            }
            pump(local, channel, m_session.get(), m_stop);
        });
    }
    // Listener im Accept-Thread selbst abbauen: so kann kein paralleles
    // forward_accept mehr auf einem freigegebenen Listener laufen. Bei einer
    // sterbenden Session uebernimmt libssh2_session_free das.
    std::lock_guard<std::recursive_mutex> lock(m_session->mutex());
    if (!m_session->closing && m_session->raw())
        libssh2_channel_forward_cancel(listener);
    m_listener = nullptr;
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
