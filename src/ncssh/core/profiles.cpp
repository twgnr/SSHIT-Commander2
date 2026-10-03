#include "ncssh/core/profiles.hpp"

#include "ncssh/config.hpp"
#include "ncssh/core/bookmarks.hpp"
#include "ncssh/core/filealarm.hpp"
#include "ncssh/core/secrets.hpp"
#include "ncssh/core/settings.hpp"

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

// Ersetzt in einem JSON-Baum jedes "profile": oldName durch newName
// (gespeicherte Tab-Zustaende: Sitzungswiederherstellung, Tab-Favoriten).
static QJsonValue replaceProfileRefs(const QJsonValue &value, const QString &oldName,
                                     const QString &newName, bool &changed)
{
    if (value.isArray()) {
        QJsonArray out;
        for (const QJsonValue &v : value.toArray())
            out.append(replaceProfileRefs(v, oldName, newName, changed));
        return out;
    }
    if (!value.isObject())
        return value;
    QJsonObject obj = value.toObject();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (it.key() == QLatin1String("profile") && it.value().toString() == oldName) {
            it.value() = newName;
            changed = true;
        } else if (it.value().isObject() || it.value().isArray()) {
            it.value() = replaceProfileRefs(it.value(), oldName, newName, changed);
        }
    }
    return obj;
}

// Nach dem Umbenennen eines Profils alle Stellen nachziehen, die es per Name
// referenzieren — sonst verbanden wiederhergestellte Tabs/Favoriten nicht mehr,
// Lesezeichen und Alarme dieses Servers waren verwaist. Fehler hier duerfen
// das Umbenennen selbst nicht scheitern lassen.
static void renameProfileReferences(const QString &oldName, const QString &newName)
{
    try {
        bool changed = false;
        const QJsonValue tabs = replaceProfileRefs(
            QJsonArray::fromVariantList(getSetting(QStringLiteral("session_tabs")).toList()),
            oldName, newName, changed);
        if (changed)
            setSetting(QStringLiteral("session_tabs"), tabs.toArray());
    } catch (...) {
    }
    try {
        QFile f(ncssh::tabFavoritesFile());
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
            f.close();
            bool changed = false;
            const QJsonValue fav = replaceProfileRefs(doc.object(), oldName, newName, changed);
            if (changed)
                ncssh::atomicWriteText(ncssh::tabFavoritesFile(),
                                       QString::fromUtf8(QJsonDocument(fav.toObject())
                                                             .toJson(QJsonDocument::Indented)));
        }
    } catch (...) {
    }
    try {
        BookmarkStore bookmarks;
        bookmarks.load();
        const QStringList paths = bookmarks.list(oldName);
        if (!paths.isEmpty()) {
            for (const QString &path : paths) {
                bookmarks.add(newName, path);
                bookmarks.remove(oldName, path);
            }
            bookmarks.save();
        }
    } catch (...) {
    }
    try {
        std::vector<AlarmSpec> alarms = loadAlarms();
        bool changed = false;
        for (AlarmSpec &a : alarms) {
            if (a.profile == oldName) {
                a.profile = newName;
                changed = true;
            }
        }
        if (changed)
            saveAlarms(alarms);
    } catch (...) {
    }
}

void ProfileStore::rename(const QString &oldName, const ServerProfile &profile)
{
    if (oldName.isEmpty() || oldName == profile.name) {
        upsert(profile);
        return;
    }
    if (get(profile.name))
        throw std::runtime_error(
            QStringLiteral("Ein Profil namens \"%1\" existiert bereits.")
                .arg(profile.name).toStdString());
    // Am alten Platz ersetzen (Reihenfolge bleibt), statt ein Duplikat
    // anzuhaengen.
    const auto it = std::find_if(m_profiles.begin(), m_profiles.end(),
                                 [&](const ServerProfile &p) { return p.name == oldName; });
    if (it != m_profiles.end())
        *it = profile;
    else
        m_profiles.push_back(profile);
    save();
    // Secrets haengen am Profilnamen: unter dem neuen Namen ablegen und die
    // alten entfernen — sonst blieben sie verwaist im Keyring liegen.
    if (profile.savePassword) {
        setSecret(profile.name, QStringLiteral("password"), profile.password);
        setSecret(profile.name, QStringLiteral("passphrase"), profile.passphrase);
    } else {
        deleteSecret(profile.name, QStringLiteral("password"));
        deleteSecret(profile.name, QStringLiteral("passphrase"));
    }
    deleteSecret(oldName, QStringLiteral("password"));
    deleteSecret(oldName, QStringLiteral("passphrase"));
    renameProfileReferences(oldName, profile.name);
}

QString ProfileStore::uniqueName(const QString &base) const
{
    if (!get(base))
        return base;
    for (int n = 2;; ++n) {
        // Verkettung statt arg(): ein "%2" im Namen wuerde sonst ersetzt.
        const QString candidate =
            base + QStringLiteral(" (") + QString::number(n) + QLatin1Char(')');
        if (!get(candidate))
            return candidate;
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
