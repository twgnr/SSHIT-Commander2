// Persistente Verwaltung gespeicherter Server-Profile (JSON).
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
    // Ersetzt das Profil oldName durch profile (neuer Name) und zieht die
    // Keyring-Secrets mit um. Wirft, wenn profile.name schon einem ANDEREN
    // Profil gehoert (das wuerde sonst still ueberschrieben).
    void rename(const QString &oldName, const ServerProfile &profile);
    // Freier Name auf Basis von base: base, "base (2)", "base (3)" …
    QString uniqueName(const QString &base) const;

    // Laedt gespeicherte Secrets aus dem Keyring in das Profil (vor dem Connect).
    void hydrate(ServerProfile &profile) const;

private:
    std::vector<ServerProfile> m_profiles;
};

} // namespace ncssh::core
