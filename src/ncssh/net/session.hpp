// Verwaltet aktive SSH-Sessions (Connection-Pool, einfaches Lifecycle).
#pragma once

#include "ncssh/core/hostkeys.hpp"
#include "ncssh/core/models.hpp"
#include "ncssh/net/ssh.hpp"

#include <vector>

namespace ncssh::net {

// Ein geaenderter Host-Key bricht die Verbindung VOR der Authentifizierung ab —
// es werden also keine Zugangsdaten preisgegeben. Damit die Oberflaeche danach
// erklaeren kann, was passiert ist (und den Nutzer bewusst entscheiden laesst),
// merkt sich der Manager die Fingerprints des letzten Konflikts.
struct HostKeyMismatch {
    bool valid = false;
    QString host;
    int port = 22;
    QString algorithm;
    QString expected;   // gepinnt
    QString received;   // vom Server geliefert
};

class SessionManager {
public:
    SSHSessionPtr open(const core::ServerProfile &profile);
    void close(const SSHSessionPtr &session);
    void closeAll();

    // Konflikt des letzten Verbindungsversuchs (valid == false = keiner).
    const HostKeyMismatch &lastMismatch() const { return m_lastMismatch; }
    void clearMismatch() { m_lastMismatch = {}; }
    void setMismatch(const HostKeyMismatch &mismatch) { m_lastMismatch = mismatch; }

    core::HostKeyStore hostkeys;

private:
    std::vector<SSHSessionPtr> m_sessions;
    HostKeyMismatch m_lastMismatch;
};

} // namespace ncssh::net
