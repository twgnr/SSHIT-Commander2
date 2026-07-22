#include "ncssh/core/profiles.hpp"

#include "ncssh/config.hpp"
#include "ncssh/core/secrets.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <algorithm>
#include <stdexcept>

namespace ncssh::core {

ProfileStore::ProfileStore()
{
    load();
}

// --- Persistenz ------------------------------------------------------------

void ProfileStore::load()
{
    const QString path = ncssh::profilesFile();
    QFile f(path);
    if (!f.exists()) {
        m_profiles.clear();
        return;
    }
    if (!f.open(QIODevice::ReadOnly))
        throw std::runtime_error(
            (QStringLiteral("Kann Datei nicht lesen: ") + path).toStdString());
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    m_profiles.clear();
    // Defekte oder unerwartete Datei -> leere Liste
    if (err.error != QJsonParseError::NoError || !doc.isArray())
        return;
    for (const QJsonValue v : doc.array()) {
        if (!v.isObject()) {
            m_profiles.clear();
            return;
        }
        m_profiles.push_back(ServerProfile::fromJson(v.toObject()));
    }
}

void ProfileStore::save() const
{
    QJsonArray data;
    for (const ServerProfile &p : m_profiles)
        data.append(p.toJson());
    ncssh::atomicWriteText(
        ncssh::profilesFile(),
        QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Indented)));
}

// --- CRUD ------------------------------------------------------------------

std::optional<ServerProfile> ProfileStore::get(const QString &name) const
{
    for (const ServerProfile &p : m_profiles)
        if (p.name == name)
            return p;
    return std::nullopt;
}

void ProfileStore::upsert(const ServerProfile &profile)
{
    const auto it = std::find_if(m_profiles.begin(), m_profiles.end(),
                                 [&](const ServerProfile &p) { return p.name == profile.name; });
    if (it != m_profiles.end())
        *it = profile;
    else
        m_profiles.push_back(profile);
    save();
    // Secrets in den OS-Keyring (statt Klartext-Datei)
    if (profile.savePassword) {
        setSecret(profile.name, QStringLiteral("password"), profile.password);
        setSecret(profile.name, QStringLiteral("passphrase"), profile.passphrase);
    } else {
        deleteSecret(profile.name, QStringLiteral("password"));
        deleteSecret(profile.name, QStringLiteral("passphrase"));
    }
}

void ProfileStore::remove(const QString &name)
{
    std::erase_if(m_profiles, [&](const ServerProfile &p) { return p.name == name; });
    save();
    deleteSecret(name, QStringLiteral("password"));
    deleteSecret(name, QStringLiteral("passphrase"));
}

void ProfileStore::hydrate(ServerProfile &profile) const
{
    if (!profile.savePassword)
        return;
    if (profile.password.isEmpty())
        profile.password = getSecret(profile.name, QStringLiteral("password")).value_or(QString());
    if (profile.passphrase.isEmpty())
        profile.passphrase =
            getSecret(profile.name, QStringLiteral("passphrase")).value_or(QString());
}

} // namespace ncssh::core
