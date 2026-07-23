// Prueft den OS-Keyring (Windows Credential Manager): setSecret muss so
// schreiben, dass getSecret denselben Wert zurueckliest. Schlaegt das fehl,
// wird ein "Passwort speichern" still wirkungslos.
#include "tests/harness.hpp"

#include "ncssh/core/secrets.hpp"

using namespace ncssh::core;

TEST(secrets, roundtrip_write_read_delete)
{
    const QString profile = QStringLiteral("ncssh-selftest-Zx9");
    const QString value = QStringLiteral("geheim-äöü-123");

    setSecret(profile, QStringLiteral("password"), value);
    const auto read = getSecret(profile, QStringLiteral("password"));
#ifdef Q_OS_WIN
    CHECK(read.has_value());
    if (read)
        CHECK_EQ(*read, value);
    deleteSecret(profile, QStringLiteral("password"));
    CHECK(!getSecret(profile, QStringLiteral("password")).has_value());
#else
    // Ohne Backend ist ein No-Op erwartet (nullopt).
    CHECK(!read.has_value());
#endif
}

TEST(secrets, empty_value_removes_entry)
{
    const QString profile = QStringLiteral("ncssh-selftest-Zx9b");
    setSecret(profile, QStringLiteral("password"), QStringLiteral("etwas"));
    setSecret(profile, QStringLiteral("password"), QString());  // leer -> loeschen
    CHECK(!getSecret(profile, QStringLiteral("password")).has_value());
}
