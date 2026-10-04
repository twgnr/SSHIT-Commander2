// Serverprofile bei aktiver App-Sperre verschluesselt (AES-256-GCM mit einem
// per Passwort verpackten Datenschluessel).
#include "tests/harness.hpp"

#include "ncssh/config.hpp"
#include "ncssh/core/applock.hpp"
#include "ncssh/core/configio.hpp"
#include "ncssh/core/profiles.hpp"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

using namespace ncssh;
namespace lock = ncssh::core::applock;

namespace {

class IsolatedConfig {
public:
    IsolatedConfig() : m_old(qgetenv("APPDATA"))
    {
        qputenv("APPDATA", m_dir.path().toUtf8());
        lock::forgetDataKey();
    }
    ~IsolatedConfig()
    {
        lock::forgetDataKey();
        qputenv("APPDATA", m_old);
    }
    bool valid() const { return m_dir.isValid(); }

private:
    QTemporaryDir m_dir;
    QByteArray m_old;
};

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

const QString kHost = QStringLiteral("geheimer-host.example");

void addProfile()
{
    // Direkt in die Datei (ohne upsert -> kein Zugriff auf den echten Keyring).
    QJsonObject profile{{QStringLiteral("name"), QStringLiteral("enc-test")},
                        {QStringLiteral("host"), kHost},
                        {QStringLiteral("username"), QStringLiteral("root")}};
    QFile f(ncssh::profilesFile());
    f.open(QIODevice::WriteOnly);
    f.write(QJsonDocument(QJsonArray{profile}).toJson());
}

bool hasHost(const core::ProfileStore &store)
{
    for (const auto &p : store.profiles())
        if (p.host == kHost)
            return true;
    return false;
}

} // namespace

TEST(profile_encryption, profiles_are_encrypted_while_locked)
{
    IsolatedConfig cfg;
    CHECK(cfg.valid());
    addProfile();

    // Sperre einschalten (wie der Einstellungsdialog): Passwort + neu speichern.
    lock::setPassword(QStringLiteral("geheim-123"));
    CHECK(lock::hasDataKey());
    { core::ProfileStore store; CHECK(hasHost(store)); store.save(); }
    const QByteArray onDisk = readFile(ncssh::profilesFile());
    CHECK(lock::isEncrypted(onDisk));
    CHECK(!onDisk.contains(kHost.toUtf8()));          // kein Klartext mehr
    CHECK(!onDisk.contains("root"));

    // Ohne Datenschluessel (App nicht entsperrt): unlesbar, Speichern verweigert.
    lock::forgetDataKey();
    {
        core::ProfileStore locked;
        CHECK(locked.unreadable());
        CHECK(locked.profiles().empty());
        bool threw = false;
        try { locked.save(); } catch (const std::exception &) { threw = true; }
        CHECK(threw);
        CHECK_EQ(readFile(ncssh::profilesFile()), onDisk);   // Datei unangetastet
    }

    // Falsches Passwort entsperrt nichts, richtiges schon.
    CHECK(!lock::unlock(QStringLiteral("falsch-123")));
    CHECK(!lock::hasDataKey());
    CHECK(lock::unlock(QStringLiteral("geheim-123")));
    { core::ProfileStore store; CHECK(!store.unreadable()); CHECK(hasHost(store)); }

    // Export enthaelt die Profile im Klartext (portabel), die Datei bleibt verschluesselt.
    const QJsonArray exported =
        core::buildBundle().value(QStringLiteral("files")).toObject().value(QStringLiteral("servers")).toArray();
    CHECK_EQ(exported.size(), 1);
    CHECK_EQ(exported.at(0).toObject().value(QStringLiteral("host")).toString(), kHost);
}

TEST(profile_encryption, password_change_keeps_profiles_readable)
{
    IsolatedConfig cfg;
    CHECK(cfg.valid());
    addProfile();
    lock::setPassword(QStringLiteral("geheim-123"));
    { core::ProfileStore store; store.save(); }

    // Aendern ohne Schluessel im Speicher: mit dem bisherigen Passwort.
    lock::forgetDataKey();
    bool threw = false;
    try { lock::setPassword(QStringLiteral("neues-pass-1")); } catch (const std::exception &) { threw = true; }
    CHECK(threw);                                     // ohne altes Passwort nicht
    lock::setPassword(QStringLiteral("neues-pass-1"), QStringLiteral("geheim-123"));

    lock::forgetDataKey();
    CHECK(!lock::unlock(QStringLiteral("geheim-123")));
    CHECK(lock::unlock(QStringLiteral("neues-pass-1")));
    core::ProfileStore store;
    CHECK(hasHost(store));
}

TEST(profile_encryption, disabling_lock_writes_plain_profiles_again)
{
    IsolatedConfig cfg;
    CHECK(cfg.valid());
    addProfile();
    lock::setPassword(QStringLiteral("geheim-123"));
    { core::ProfileStore store; store.save(); }
    CHECK(lock::isEncrypted(readFile(ncssh::profilesFile())));

    // Wie der Einstellungsdialog: entschluesselt laden, Sperre weg, speichern.
    core::ProfileStore store;
    lock::disable();
    store.save();
    const QByteArray plain = readFile(ncssh::profilesFile());
    CHECK(!lock::isEncrypted(plain));
    CHECK(plain.contains(kHost.toUtf8()));
}

TEST(profile_encryption, tampering_is_detected_and_unreadable_file_set_aside)
{
    IsolatedConfig cfg;
    CHECK(cfg.valid());
    lock::setPassword(QStringLiteral("geheim-123"));
    const QByteArray env = lock::encryptData("vertraulich");
    CHECK_EQ(lock::decryptData(env).value_or(QByteArray()), QByteArray("vertraulich"));
    // Ein veraendertes Byte im Chiffrat -> GCM-Tag passt nicht.
    QJsonObject obj = QJsonDocument::fromJson(env).object();
    QByteArray data = QByteArray::fromBase64(obj.value(QStringLiteral("data")).toString().toLatin1());
    data[0] = char(data[0] ^ 0x01);
    obj.insert(QStringLiteral("data"), QString::fromLatin1(data.toBase64()));
    CHECK(!lock::decryptData(QJsonDocument(obj).toJson()).has_value());

    // applock.json geloescht: die verschluesselten Profile werden beiseitegelegt.
    addProfile();
    { core::ProfileStore store; store.save(); }
    lock::disable();
    const QString aside = core::setAsideUnreadableProfiles();
    CHECK(!aside.isEmpty());
    CHECK(QFileInfo::exists(aside));
    CHECK(!QFileInfo::exists(ncssh::profilesFile()));
    CHECK(lock::isEncrypted(readFile(aside)));       // nichts verloren, nur weggelegt
}

TEST(profile_encryption, lock_from_older_version_gets_a_data_key_on_unlock)
{
    IsolatedConfig cfg;
    CHECK(cfg.valid());
    lock::setPassword(QStringLiteral("geheim-123"));
    // Zustand wie in 1.0.4: Sperre ohne verpackten Datenschluessel.
    QJsonObject state = QJsonDocument::fromJson(readFile(lock::lockFile())).object();
    for (const char *key : {"key_salt", "key_iterations", "key_nonce", "key_data", "key_tag"})
        state.remove(QString::fromLatin1(key));
    QFile f(lock::lockFile());
    CHECK(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(state).toJson());
    f.close();
    lock::forgetDataKey();

    addProfile();   // noch Klartext
    CHECK(lock::unlock(QStringLiteral("geheim-123")));
    CHECK(lock::hasDataKey());
    CHECK(readFile(lock::lockFile()).contains("key_data"));
    { core::ProfileStore store; CHECK(hasHost(store)); store.save(); }
    CHECK(lock::isEncrypted(readFile(ncssh::profilesFile())));
}
