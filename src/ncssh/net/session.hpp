// Verwaltet aktive SSH-Sessions (Connection-Pool, einfaches Lifecycle).
// (Port von net/session.py)
#pragma once

#include "ncssh/core/hostkeys.hpp"
#include "ncssh/core/models.hpp"
#include "ncssh/net/ssh.hpp"

#include <vector>

namespace ncssh::net {

class SessionManager {
public:
    SSHSessionPtr open(const core::ServerProfile &profile);
    void close(const SSHSessionPtr &session);
    void closeAll();

    core::HostKeyStore hostkeys;

private:
    std::vector<SSHSessionPtr> m_sessions;
};

} // namespace ncssh::net
