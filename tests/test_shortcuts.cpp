// Tests fuer den Tastenkuerzel-Speicher (core/shortcuts).
//
// Wichtig, weil main_window die Kuerzel frueher fest verdrahtet hatte — der
// Einstellungs-Tab war damit wirkungslos. Diese Tests sichern den
// Speicher-/Merge-Pfad ab, den das Fenster jetzt tatsaechlich ausliest.
#include "tests/harness.hpp"

#include "ncssh/config.hpp"
#include "ncssh/core/shortcuts.hpp"

#include <QTemporaryDir>

using namespace ncssh::core;

namespace {
// Lenkt configDir() waehrend des Tests auf ein frisches Verzeichnis um.
class ConfigDirGuard
{
public:
    ConfigDirGuard() : m_appdata(qgetenv("APPDATA")), m_xdg(qgetenv("XDG_CONFIG_HOME"))
    {
        qputenv("APPDATA", m_dir.path().toLocal8Bit());
        qputenv("XDG_CONFIG_HOME", m_dir.path().toLocal8Bit());
    }
    ~ConfigDirGuard()
    {
        qputenv("APPDATA", m_appdata);
        qputenv("XDG_CONFIG_HOME", m_xdg);
    }
    bool isValid() const { return m_dir.isValid(); }

private:
    QTemporaryDir m_dir;
    QByteArray m_appdata;
    QByteArray m_xdg;
};
} // namespace

TEST(shortcuts, defaults_present)
{
    const auto defaults = defaultShortcuts();
    // Ein paar stabile IDs, die main_window registriert.
    CHECK(defaults.contains(QStringLiteral("connect")));
    CHECK(defaults.contains(QStringLiteral("settings")));
    CHECK_EQ(defaults.value(QStringLiteral("connect")), QStringLiteral("F9"));
}

TEST(shortcuts, override_roundtrip_and_merge)
{
    ConfigDirGuard guard;
    CHECK(guard.isValid());

    // Nur ein Kuerzel abweichend setzen.
    QHash<QString, QString> mapping = defaultShortcuts();
    mapping.insert(QStringLiteral("connect"), QStringLiteral("Ctrl+Shift+C"));
    saveShortcuts(mapping);

    // getShortcuts() muss die Aenderung liefern, alles andere per Default.
    const auto merged = getShortcuts();
    CHECK_EQ(merged.value(QStringLiteral("connect")), QStringLiteral("Ctrl+Shift+C"));
    CHECK_EQ(merged.value(QStringLiteral("settings")), QStringLiteral("Ctrl+,"));
}

TEST(shortcuts, only_deltas_are_stored)
{
    ConfigDirGuard guard;
    CHECK(guard.isValid());

    // Alles auf Default speichern -> es darf nichts gespeichert werden, damit
    // spaetere Default-Aenderungen bei einem Update automatisch greifen.
    saveShortcuts(defaultShortcuts());
    // Frisch einlesen: identisch zu den Defaults.
    const auto merged = getShortcuts();
    CHECK_EQ(merged, defaultShortcuts());

    // Ein leeres Kuerzel (bewusst deaktiviert) bleibt leer.
    QHash<QString, QString> mapping = defaultShortcuts();
    mapping.insert(QStringLiteral("venv_setup"), QString());
    saveShortcuts(mapping);
    CHECK(getShortcuts().value(QStringLiteral("venv_setup")).isEmpty());
}

TEST(shortcuts, label_lookup)
{
    CHECK(!labelFor(QStringLiteral("connect")).isEmpty());
    // Unbekannte ID faellt auf sich selbst zurueck.
    CHECK_EQ(labelFor(QStringLiteral("kein-solcher")), QStringLiteral("kein-solcher"));
}
