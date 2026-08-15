// SSH-Port-Forwarding ueber libssh2: lokal (-L), remote (-R), dynamisch (-D).
#pragma once

#include "ncssh/core/models.hpp"
#include "ncssh/net/ssh.hpp"

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace ncssh::net {

// Aktiver Tunnel; stop() beendet die Listener- und Pump-Threads sauber.
class Tunnel {
public:
    explicit Tunnel(SSHSessionPtr session, core::TunnelSpec spec);
    ~Tunnel();

    void start();  // wirft bei Fehler (Port belegt, forward-listen abgelehnt)
    void stop();

    const core::TunnelSpec &spec() const { return m_spec; }

private:
    void runLocalOrDynamic();  // -L / -D
    void runRemote();          // -R

    SSHSessionPtr m_session;
    core::TunnelSpec m_spec;
    std::atomic_bool m_stop{false};
    int m_listenSocket = -1;
    void *m_listener = nullptr;  // LIBSSH2_LISTENER* (remote)
    std::thread m_thread;
    std::vector<std::thread> m_workers;
};

// Oeffnet die Weiterleitung gemaess Spezifikation.
std::unique_ptr<Tunnel> openTunnel(SSHSessionPtr session, const core::TunnelSpec &spec);

// Parst den Adressteil einer SOCKS5-CONNECT-Anfrage (ab dem ATYP-Byte):
//   ATYP(1) | ADDR (IPv4=4 | Domain=1+n | IPv6=16) | PORT(2, big-endian)
// Liefert Host und Port; false bei zu kurzem Puffer oder unbekanntem Adresstyp.
// Rein (kein Socket) — deshalb testbar, waehrend der Rest einen Server braucht.
bool parseSocks5Target(const QByteArray &data, QString &host, int &port);

} // namespace ncssh::net
