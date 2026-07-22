// Persistente Verwaltung gespeicherter Server-Profile (JSON).
// (Port von core/profiles.py)
#pragma once

#include "ncssh/core/models.hpp"

#include <QString>
#include <optional>
#include <vector>

namespace ncssh::core {

// Laedt/speichert ServerProfile-Objekte aus einer JSON-Datei.
class ProfileStore {
public:
    ProfileStore();

    // --- Persistenz --------------------------------------------------------
    void load();
    void save() const;

    // --- CRUD --------------------------------------------------------------
    std::vector<ServerProfile> profiles() const { return m_profiles; }
    std::optional<ServerProfile> get(const QString &name) const;
    void upsert(const ServerProfile &profile);
    void remove(const QString &name);

    // Laedt gespeicherte Secrets aus dem Keyring in das Profil (vor dem Connect).
    void hydrate(ServerProfile &profile) const;

private:
    std::vector<ServerProfile> m_profiles;
};

} // namespace ncssh::core
