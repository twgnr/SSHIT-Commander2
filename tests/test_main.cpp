// Schlankes Test-Harness ohne Fremd-Dependency.
//
// Registriert Testfunktionen ueber das TEST()-Makro und fuehrt sie aus.
// Rueckgabe 0 = alle bestanden, sonst die Anzahl der Fehlschlaege.
// (Ersetzt pytest aus tests/ des Python-Originals.)
#include "tests/harness.hpp"

#include <QCoreApplication>
#include <cstdio>

int main(int argc, char *argv[])
{
    // Manche Tests nutzen Qt-Typen mit Event-Loop-Bezug (QSettings, QStandardPaths).
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("sshit-tests"));
    return ncssh::tests::runAll();
}
