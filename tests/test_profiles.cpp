// Prueft, dass ein Server-Passwort ueber Speichern/Neuladen erhalten bleibt:
// die JSON-Datei enthaelt es NICHT (Klartext-Schutz), der Keyring schon, und
// hydrate() holt es vor dem Verbinden zurueck. Genau dieser Weg wurde als
// "Passwort wird nicht gespeichert" hinterfragt.
#include "tests/harness.hpp"

#include "ncssh/config.hpp"
#include "ncssh/core/profiles.hpp"
#include "ncssh/core/secrets.hpp"

#include <QFile>
#include <QTemporaryDir>

using namespace ncssh::core;

TEST(profiles, password_persists_via_keyring_not_json)
{
    const QByteArray oldAppData = qgetenv("APPDATA");
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    qputenv("APPDATA", tmp.path().toUtf8());

    const QString name = QStringLiteral("ncssh-selftest-profil-Q7");

    {
        ProfileStore store;
        ServerProfile p;
        p.name = name;
        p.host = QStringLiteral("example.com");
        p.username = QStringLiteral("tobi");
        p.authMethod = QStringLiteral("password");
        p.password = QStringLiteral("s3cr3t-äöü");
        p.savePassword = true;
        store.upsert(p);   // schreibt JSON + Keyring
        store.save();
    }

    // Frischer Store liest die JSON — ohne Passwort im Klartext.
    ProfileStore store2;
    store2.load();
    auto loaded = store2.get(name);
    CHECK(loaded.has_value());
    if (loaded) {
        CHECK(loaded->savePassword);
#ifdef Q_OS_WIN
        CHECK(loaded->password.isEmpty());  // nicht in der Datei
        store2.hydrate(*loaded);            // aus dem Keyring nachladen
        CHECK_EQ(loaded->password.toStdString(), std::string("s3cr3t-\xc3\xa4\xc3\xb6\xc3\xbc"));
#endif
    }

    // Sicherstellen, dass die profiles.json wirklich kein Klartext-Passwort haelt.
    QFile f(ncssh::profilesFile());
    CHECK(f.open(QIODevice::ReadOnly));
    const QByteArray json = f.readAll();
    f.close();
    CHECK(!json.contains("s3cr3t"));

    // Aufraeumen (Keyring + Datei).
    store2.remove(name);
    qputenv("APPDATA", oldAppData);
}

TEST(profiles, unchecking_save_password_removes_secret)
{
    const QByteArray oldAppData = qgetenv("APPDATA");
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    qputenv("APPDATA", tmp.path().toUtf8());

    const QString name = QStringLiteral("ncssh-selftest-profil-Q8");
    ProfileStore store;
    ServerProfile p;
    p.name = name;
    p.host = QStringLiteral("h");
    p.authMethod = QStringLiteral("password");
    p.password = QStringLiteral("weg-damit");
    p.savePassword = true;
    store.upsert(p);
#ifdef Q_OS_WIN
    CHECK(getSecret(name, QStringLiteral("password")).has_value());
#endif

    // "Passwort speichern" abwaehlen -> Secret muss verschwinden.
    p.savePassword = false;
    store.upsert(p);
    CHECK(!getSecret(name, QStringLiteral("password")).has_value());

    store.remove(name);
    qputenv("APPDATA", oldAppData);
}
