// Absturzberichte: Bei einem Absturz (Zugriffsfehler, Stack-Ueberlauf,
// abort/qFatal, std::terminate …) schreibt die App einen Minidump (.dmp) und
// eine kurze Textdatei mit Ausnahme-Code, Modul+Offset und Aufrufkette in
// <Konfigurationsordner>/crashes. Beim naechsten Start weist sie darauf hin.
//
// Die Textdatei enthaelt nur Adressen (Modul+Offset), keine Inhalte; mit der
// passenden .pdb des Builds laesst sie sich auf Codezeilen zurueckfuehren.
#pragma once

#include <QString>

class QWidget;

namespace ncssh::gui {

// So frueh wie moeglich aufrufen (vor QApplication).
void installCrashHandler();

// Ordner der Absturzberichte.
QString crashDirectory();

// Nach dem Start: neue Absturzberichte seit dem letzten Hinweis melden.
void reportPreviousCrash(QWidget *parent);

// Eine in einem Ereignis abgefangene Ausnahme: in errors.log festhalten und
// (einmal je Sitzung) einen Hinweis zeigen — statt die App zu beenden.
void reportCaughtException(const QString &what);

} // namespace ncssh::gui
