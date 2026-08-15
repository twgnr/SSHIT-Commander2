// Tests fuer Konfigurations-Im-/Export (core/configio).
#include "tests/harness.hpp"

#include "ncssh/config.hpp"
#include "ncssh/core/configio.hpp"
#include "ncssh/core/settings.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

using namespace ncssh::core;

namespace {
// Lenkt configDir() waehrend des Tests auf ein frisches Verzeichnis um und
// stellt die Umgebung danach wieder her (die Tests teilen sich einen Prozess).
class ConfigDirGuard
{
public:
    ConfigDirGuard()
        : m_appdata(qgetenv("APPDATA")), m_xdg(qgetenv("XDG_CONFIG_HOME"))
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

TEST(configio, build_export_apply_roundtrip)
{
    ConfigDirGuard guard;
    CHECK(guard.isValid());

    setSetting(QStringLiteral("theme"), QStringLiteral("Hell"));
    setSetting(QStringLiteral("pane_thumbnails"), true);

    const QJsonObject bundle = buildBundle();
    CHECK(availableSections(bundle).contains(QStringLiteral("settings")));

    QTemporaryDir outDir;
    CHECK(outDir.isValid());
    const QString out = outDir.filePath(QStringLiteral("cfg.json"));
    writeExport(out);

    // Wert aendern -> Import muss ihn wiederherstellen
    setSetting(QStringLiteral("theme"), QStringLiteral("Dunkel"));
    const QJsonObject loaded = readBundle(out);
    CHECK_EQ(applyBundle(loaded, {QStringLiteral("settings")}),
             (QStringList{QStringLiteral("settings")}));
    CHECK_EQ(getSettingString(QStringLiteral("theme")), QStringLiteral("Hell"));
    CHECK_EQ(getSettingBool(QStringLiteral("pane_thumbnails")), true);
}

TEST(configio, apply_only_selected_sections)
{
    ConfigDirGuard guard;
    CHECK(guard.isValid());

    setSetting(QStringLiteral("theme"), QStringLiteral("Mitternacht"));
    QTemporaryDir outDir;
    CHECK(outDir.isValid());
    const QString out = outDir.filePath(QStringLiteral("cfg.json"));
    writeExport(out);

    setSetting(QStringLiteral("theme"), QStringLiteral("Dunkel"));
    const QJsonObject loaded = readBundle(out);
    // servers nicht vorhanden -> nicht angewendet;
    // settings nicht gewaehlt -> unveraendert
    CHECK_EQ(applyBundle(loaded, {QStringLiteral("servers")}), QStringList{});
    CHECK_EQ(getSettingString(QStringLiteral("theme")), QStringLiteral("Dunkel"));
}

TEST(configio, read_bundle_rejects_foreign_file)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("bad.json"));
    QFile f(path);
    CHECK(f.open(QIODevice::WriteOnly));
    f.write("{\"foo\": 1}");
    f.close();

    CHECK_THROWS(readBundle(path));
}
