// Persistente Verwaltung gespeicherter Server-Profile (JSON).
#pragma once

#include "ncssh/core/models.hpp"

#include <QString>
#include <optional>
#include <vector>

namespace ncssh::core {

// Laedt/speichert ServerProfile-Objekte aus einer JSON-Datei — bei aktiver
// App-Sperre verschluesselt (siehe applock.hpp).
class ProfileStore {
public:
    ProfileStore();

    // --- Persistenz --------------------------------------------------------
    void load();
    // Wirft, wenn die Datei verschluesselt und nicht entsperrt ist (unreadable)
    // — sonst ueberschriebe eine leere Liste die echten Profile.
    void save() const;
    // Verschluesselte Datei, die (ohne/mit falschem Datenschluessel) nicht
    // gelesen werden konnte.
    bool unreadable() const { return m_unreadable; }

    // --- CRUD --------------------------------------------------------------
    std::vector<ServerProfile> profiles() const { return m_profiles; }
    std::optional<ServerProfile> get(const QString &name) const;
    void upsert(const ServerProfile &profile);
    // Setzt NUR den Zeitstempel der letzten Verbindung und speichert — ohne
    // die Keyring-Secrets anzufassen (upsert wuerde sie mit dem Profilstand
    // ueberschreiben). false, wenn es kein Profil dieses Namens gibt.
    bool touchLastConnected(const QString &name, const QString &timestamp);
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
    bool m_unreadable = false;
};

// Beim Start (nach dem Entsperren): Ist servers.json verschluesselt, aber nicht
// lesbar (App-Sperre entfernt, applock.json fehlt/gehoert zu anderen Daten),
// wird sie als servers.json.locked-<Zeit> beiseitegelegt, damit die App mit
// leerer Liste weiterarbeiten kann. Liefert den neuen Dateinamen oder "".
QString setAsideUnreadableProfiles();

} // namespace ncssh::core
