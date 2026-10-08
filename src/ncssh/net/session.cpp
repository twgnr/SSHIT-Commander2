#include "ncssh/net/session.hpp"

#include <algorithm>

namespace ncssh::net {

SSHSessionPtr SessionManager::open(const core::ServerProfile &profile,
                                   const ConnectControl &control)
{
    clearMismatch();
    SSHSessionPtr session;
    try {
        session = connectSession(profile, &hostkeys, control);
    } catch (const HostKeyChangedError &err) {
        // Fingerprints festhalten, damit die Oberflaeche sie zeigen kann; der
        // Fehler wird unveraendert weitergereicht.
        setMismatch({true, err.host, err.port, err.algorithm, err.expected, err.received});
        throw;
    } catch (const HostKeyUnknownError &err) {
        setMismatch({true, err.host, err.port, err.algorithm, QString(), err.fingerprint,
                     /*unknown=*/true});
        throw;
    }
    m_sessions.push_back(session);
    return session;
}

void SessionManager::close(const SSHSessionPtr &session)
{
    session->closing = true;
    m_sessions.erase(std::remove(m_sessions.begin(), m_sessions.end(), session),
                     m_sessions.end());
    session->close();
}

void SessionManager::closeAll()
{
    for (const auto &session : m_sessions) {
        session->closing = true;
        try {
            session->close();
        } catch (...) {
        }
    }
    m_sessions.clear();
}

} // namespace ncssh::net
