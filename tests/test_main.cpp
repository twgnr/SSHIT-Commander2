// Schlankes Test-Harness ohne Fremd-Dependency.
//
// Registriert Testfunktionen ueber das TEST()-Makro und fuehrt sie aus.
// Rueckgabe 0 = alle bestanden, sonst die Anzahl der Fehlschlaege.
#include "tests/harness.hpp"

#include <QApplication>
#include <QFileInfo>
#include <QTemporaryDir>
#include <cstdio>

int main(int argc, char *argv[])
{
    // QApplication (nicht QCoreApplication): die GUI-Smoke-Tests bauen echte
    // Widgets. Ohne Bildschirm laeuft das ueber die Offscreen-Plattform —
    // entspricht QT_QPA_PLATFORM=offscreen.
    //
    // Fehlt das Plugin, bleibt Qt beim Start mit einer MessageBox stehen (und
    // ein Testlauf haengt endlos) — deshalb wird die Vorgabe nur gesetzt bzw.
    // beibehalten, wenn qoffscreen tatsaechlich daneben liegt.
    const QString pluginDir =
        QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath() + QStringLiteral("/platforms/");
    const bool haveOffscreen = QFileInfo::exists(pluginDir + QStringLiteral("qoffscreen.dll"))
                               || QFileInfo::exists(pluginDir + QStringLiteral("libqoffscreen.so"));
    if (haveOffscreen) {
        if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
            qputenv("QT_QPA_PLATFORM", "offscreen");
    } else if (qgetenv("QT_QPA_PLATFORM") == "offscreen") {
        std::printf("Hinweis: qoffscreen-Plugin fehlt — Tests laufen auf der "
                    "Standard-Plattform.\n");
        qunsetenv("QT_QPA_PLATFORM");
    }

    // Konfiguration des ganzen Laufs in ein Wegwerf-Verzeichnis lenken: Tests,
    // die APPDATA selbst nicht (oder zu frueh zurueck-)setzen, ueberschrieben
    // sonst die echten Einstellungen des Nutzers (z. B. macros.json beim
    // Zerstoeren eines Makro-Dialogs).
    QTemporaryDir configHome;
    if (!configHome.isValid()) {
        std::printf("Temporaeres Konfigurationsverzeichnis fehlt — Abbruch, um die "
                    "echten Einstellungen nicht zu veraendern.\n");
        return 1;
    }
    qputenv("APPDATA", configHome.path().toLocal8Bit());
    qputenv("XDG_CONFIG_HOME", configHome.path().toLocal8Bit());

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("sshit-tests"));
    return ncssh::tests::runAll();
}
