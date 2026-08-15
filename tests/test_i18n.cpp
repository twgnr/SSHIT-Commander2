// Tests fuer die i18n-Schicht (core/i18n) und den englischen Katalog.
//
// Enthaelt das Vollstaendigkeits-Gate: ruft tools/i18n_extract.py --check und
// schlaegt fehl, sobald ein _t("…")-Literal keine englische Uebersetzung hat.
#include "tests/harness.hpp"

#include "ncssh/core/commands.hpp"
#include "ncssh/core/i18n.hpp"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

using namespace ncssh::core;

namespace {
// Setzt die Sprache am Ende des Tests wieder auf Deutsch (gemeinsamer Prozess).
struct LanguageGuard {
    ~LanguageGuard() { setLanguage(QStringLiteral("de")); }
};

QString sourceRoot()
{
    return QDir::fromNativeSeparators(QStringLiteral(NCSSH_SOURCE_DIR));
}

// python / python3 / py — je nachdem, was auf dem Rechner liegt.
QString findPython()
{
    for (const auto &name : {"python", "python3", "py"}) {
        const QString path = QStandardPaths::findExecutable(QString::fromLatin1(name));
        if (!path.isEmpty())
            return path;
    }
    return {};
}
} // namespace

TEST(i18n, de_is_identity)
{
    LanguageGuard guard;
    setLanguage(QStringLiteral("de"));
    CHECK_EQ(_t("Speichern"), QStringLiteral("Speichern"));
    CHECK_EQ(currentLanguage(), QStringLiteral("de"));
}

TEST(i18n, en_translates_known_keys)
{
    LanguageGuard guard;
    setLanguage(QStringLiteral("en"));
    CHECK_EQ(_t("Speichern"), QStringLiteral("Save"));
    CHECK_EQ(_t("Einstellungen"), QStringLiteral("Settings"));
    CHECK_EQ(currentLanguage(), QStringLiteral("en"));
}

TEST(i18n, unknown_key_returns_source)
{
    LanguageGuard guard;
    setLanguage(QStringLiteral("en"));
    CHECK_EQ(_t(QString::fromUtf8("Völlig unbekannter Text 123")),
             QString::fromUtf8("Völlig unbekannter Text 123"));
}

TEST(i18n, format_placeholders)
{
    LanguageGuard guard;
    setLanguage(QStringLiteral("en"));
    // Platzhalter in C++/Qt: %1 statt {name}
    CHECK_EQ(_t("Fehler: %1").arg(QStringLiteral("x")), QStringLiteral("Error: x"));
    CHECK_EQ(_t("Modell"), QStringLiteral("Model"));
}

TEST(i18n, invalid_language_falls_back_to_de)
{
    LanguageGuard guard;
    setLanguage(QStringLiteral("xx"));
    CHECK_EQ(currentLanguage(), QStringLiteral("de"));
    CHECK_EQ(_t("Speichern"), QStringLiteral("Speichern"));
}

TEST(i18n, available_languages)
{
    CHECK_EQ(availableLanguages(), (QStringList{QStringLiteral("de"), QStringLiteral("en")}));
    CHECK_EQ(languageName(QStringLiteral("en")), QStringLiteral("English"));
}

TEST(i18n, command_catalog_translates)
{
    // Der Befehlskatalog wird einmalig
    // beim ersten Zugriff uebersetzt. Deshalb hier vor dem ersten catalog()
    // auf Englisch schalten; kein anderer Test fasst den Katalog an.
    LanguageGuard guard;
    setLanguage(QStringLiteral("en"));
    const auto &cat = catalog();
    CHECK(!cat.empty());

    const CommandSpec *ls = nullptr;
    for (const auto &spec : cat) {
        if (spec.templateText.startsWith(QStringLiteral("ls "))) {
            ls = &spec;
            break;
        }
    }
    CHECK(ls != nullptr);
    if (!ls)
        return;
    CHECK_EQ(ls->name, QString::fromUtf8("ls — list directory"));
    CHECK_EQ(ls->category, QStringLiteral("Files"));

    bool sawPath = false;
    for (const auto &p : ls->params) {
        if (p.name == QStringLiteral("path")) {
            sawPath = true;
            CHECK_EQ(p.label, QStringLiteral("Path"));
        }
    }
    CHECK(sawPath);

    // de = Identitaet (auf der Schluesselebene, der Katalog bleibt gecacht)
    setLanguage(QStringLiteral("de"));
    CHECK_EQ(_t("Dateien"), QStringLiteral("Dateien"));
}

TEST(i18n, completeness_gate)
{
    // Jeder _t()-Schluessel muss eine englische Uebersetzung haben.
    const QString python = findPython();
    if (python.isEmpty()) {
        std::printf("       (uebersprungen: kein Python im PATH fuer i18n_extract.py)\n");
        return;
    }
    const QString script = sourceRoot() + QStringLiteral("/tools/i18n_extract.py");
    if (!QFileInfo::exists(script)) {
        ::ncssh::tests::reportFailure(__FILE__, __LINE__,
                                      "tools/i18n_extract.py nicht gefunden: "
                                          + script.toStdString());
        return;
    }

    QProcess proc;
    proc.setWorkingDirectory(sourceRoot());
    proc.start(python, {script, QStringLiteral("--check")});
    if (!proc.waitForFinished(120000)) {
        ::ncssh::tests::reportFailure(__FILE__, __LINE__, "i18n_extract.py lief in ein Timeout");
        return;
    }
    if (proc.exitCode() != 0) {
        ::ncssh::tests::reportFailure(
            __FILE__, __LINE__,
            std::string("Fehlende Uebersetzungen:\n")
                + proc.readAllStandardOutput().toStdString()
                + proc.readAllStandardError().toStdString());
    }
}
