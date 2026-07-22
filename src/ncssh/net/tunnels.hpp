// SSH-Port-Forwarding ueber libssh2: lokal (-L), remote (-R), dynamisch (-D).
// (Port von net/tunnels.py; asyncssh -> libssh2 mit eigenen Pump-Threads)
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

} // namespace ncssh::net
