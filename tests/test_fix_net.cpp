// Runde 2 der Fehlersuche, Netzwerkteil — soweit ohne SSH-Server pruefbar:
//  - HostKeyStore war ein ungeschuetzter QHash, den connectSession auf
//    Worker-Threads las, waehrend der GUI-Thread add()/save() aufrief
//    (parallele Tab-Verbindungen -> Data Race/Absturz).
//  - Ein stummer SOCKS-Client blockierte den Accept-Thread eines -D-Tunnels
//    (recv ohne Timeout): weitere Clients kamen nicht durch, stop() hing ewig.
// Nicht ohne Server testbar (bewusst ausgelassen): EOF/stderr-Spin in
// RemoteShell/stream, Datenverlust im Tunnel-Pump, -R-Listener-Wettlauf.
#include "tests/harness.hpp"

#include "ncssh/config.hpp"
#include "ncssh/core/hostkeys.hpp"
#include "ncssh/net/ssh.hpp"
#include "ncssh/net/tunnels.hpp"

#include <QTemporaryDir>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#ifdef Q_OS_WIN
#  include <winsock2.h>
#  include <ws2tcpip.h>
#endif

using namespace ncssh;

namespace {

// Lenkt configDir() (host_keys.json) auf ein frisches Verzeichnis um.
class FixNetConfigGuard {
public:
    FixNetConfigGuard() : m_old(qgetenv("APPDATA")) { qputenv("APPDATA", m_dir.path().toLocal8Bit()); }
    ~FixNetConfigGuard() { qputenv("APPDATA", m_old); }

private:
    QTemporaryDir m_dir;
    QByteArray m_old;
};

#ifdef Q_OS_WIN
struct WinsockInit {
    WinsockInit()
    {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
    }
    ~WinsockInit() { WSACleanup(); }
};

// Freien Loopback-Port ermitteln (kurz binden, wieder freigeben).
int freeLoopbackPort()
{
    const SOCKET s = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    int len = sizeof(addr);
    ::bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
    ::getsockname(s, reinterpret_cast<sockaddr *>(&addr), &len);
    const int port = ntohs(addr.sin_port);
    closesocket(s);
    return port;
}

SOCKET connectLoopback(int port)
{
    const SOCKET s = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<u_short>(port));
    if (::connect(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    DWORD timeoutMs = 3000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeoutMs),
               sizeof(timeoutMs));
    return s;
}
#endif

} // namespace

TEST(fix_net, hostkeys_concurrent_add_and_get)
{
    FixNetConfigGuard guard;
    core::HostKeyStore store;
    constexpr int kWriters = 4;
    constexpr int kPerWriter = 20;
    std::atomic_bool stopReaders{false};
    std::atomic_int reads{0};

    std::vector<std::thread> readers;
    for (int r = 0; r < 4; ++r) {
        readers.emplace_back([&] {
            while (!stopReaders.load()) {
                // Lesen waehrend paralleler add()s darf weder abstuerzen noch
                // einen halben Eintrag liefern.
                const auto fp = store.get(QStringLiteral("host0"), 22, QStringLiteral("ssh-ed25519"));
                if (fp)
                    (void)fp->size();
                const auto all = store.entries();
                (void)all.size();
                reads.fetch_add(1);
            }
        });
    }
    std::vector<std::thread> writers;
    for (int w = 0; w < kWriters; ++w) {
        writers.emplace_back([&store, w] {
            for (int i = 0; i < kPerWriter; ++i)
                store.add(QStringLiteral("host%1").arg(w), 1000 + i,
                          QStringLiteral("SHA256:fp-%1-%2").arg(w).arg(i),
                          QStringLiteral("ssh-ed25519"));
        });
    }
    for (auto &t : writers)
        t.join();
    stopReaders = true;
    for (auto &t : readers)
        t.join();

    CHECK(reads.load() > 0);
    CHECK_EQ(store.entries().size(), kWriters * kPerWriter);
    for (int w = 0; w < kWriters; ++w)
        for (int i = 0; i < kPerWriter; ++i) {
            const auto fp = store.get(QStringLiteral("host%1").arg(w), 1000 + i,
                                      QStringLiteral("ssh-ed25519"));
            CHECK(fp.has_value());
            if (fp)
                CHECK_EQ(*fp, QStringLiteral("SHA256:fp-%1-%2").arg(w).arg(i));
        }

    // Der zuletzt gespeicherte Stand auf der Platte ist vollstaendig.
    core::HostKeyStore reloaded;
    CHECK_EQ(reloaded.entries().size(), kWriters * kPerWriter);
}

TEST(fix_net, hostkeys_entries_is_a_copy)
{
    FixNetConfigGuard guard;
    core::HostKeyStore store;
    store.add(QStringLiteral("a"), 22, QStringLiteral("SHA256:x"), QStringLiteral("ssh-rsa"));
    QHash<QString, QString> copy = store.entries();
    copy.insert(QStringLiteral("fremd"), QStringLiteral("y"));
    CHECK_EQ(store.entries().size(), 1);
    store.removeKey(QStringLiteral("a:22|ssh-rsa"));
    CHECK(store.entries().isEmpty());
    CHECK_EQ(copy.size(), 2);
}

#ifdef Q_OS_WIN
TEST(fix_net, socks_silent_client_blocks_neither_accept_nor_stop)
{
    WinsockInit wsa;
    // Session ohne Verbindung: raw() == nullptr. Der Worker bricht nach dem
    // Handshake beim Kanal-Aufbau sauber ab — fuer diesen Test genuegt das.
    auto session = std::make_shared<net::SSHSession>();
    core::TunnelSpec spec;
    spec.kind = QStringLiteral("dynamic");
    spec.listenHost = QStringLiteral("127.0.0.1");
    spec.listenPort = freeLoopbackPort();
    std::unique_ptr<net::Tunnel> tunnel;
    try {
        tunnel = net::openTunnel(session, spec);
    } catch (const std::exception &) {
        CHECK(false);   // Port war doch belegt — sehr unwahrscheinlich
        return;
    }

    // 1) Stummer Client: verbindet und sendet nichts.
    const SOCKET silent = connectLoopback(spec.listenPort);
    CHECK(silent != INVALID_SOCKET);

    // 2) Zweiter Client muss trotzdem die Methoden-Antwort bekommen.
    const SOCKET talker = connectLoopback(spec.listenPort);
    CHECK(talker != INVALID_SOCKET);
    const char hello[3] = {0x05, 0x01, 0x00};
    CHECK_EQ(::send(talker, hello, 3, 0), 3);
    char reply[2] = {0, 0};
    int got = 0;
    while (got < 2) {
        const int r = ::recv(talker, reply + got, 2 - got, 0);
        if (r <= 0)
            break;
        got += r;
    }
    CHECK_EQ(got, 2);
    CHECK_EQ(int(reply[0]), 0x05);
    CHECK_EQ(int(reply[1]), 0x00);

    // 3) stop() darf trotz haengendem Handshake nicht blockieren.
    const auto t0 = std::chrono::steady_clock::now();
    tunnel->stop();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0).count();
    CHECK(ms < 3000);

    closesocket(silent);
    closesocket(talker);
    tunnel.reset();
}
#endif
