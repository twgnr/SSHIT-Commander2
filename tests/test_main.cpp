// Schlankes Test-Harness ohne Fremd-Dependency.
//
// Registriert Testfunktionen ueber das TEST()-Makro und fuehrt sie aus.
// Rueckgabe 0 = alle bestanden, sonst die Anzahl der Fehlschlaege.
// (Ersetzt pytest aus tests/ des Python-Originals.)
#include "tests/harness.hpp"

#include <QApplication>
#include <QFileInfo>
#include <cstdio>

int main(int argc, char *argv[])
{
    // QApplication (nicht QCoreApplication): die GUI-Smoke-Tests bauen echte
    // Widgets. Ohne Bildschirm laeuft das ueber die Offscreen-Plattform —
    // entspricht QT_QPA_PLATFORM=offscreen im Python-Original (smoke_gui.py).
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

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("sshit-tests"));
    return ncssh::tests::runAll();
}
