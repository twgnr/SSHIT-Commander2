// Ermittelt das aktuell im Vordergrund liegende Programm (fuer kontext-
// abhaengige Layer im Makro-Manager).  Reine WinAPI-Loesung unter Windows;
// auf anderen Plattformen wird (0, "") geliefert.
#pragma once

#include <QString>
#include <utility>

namespace ncssh::core {

// Liefert (pid, exe_basename_kleingeschrieben) des Vordergrundfensters
// oder (0, "") wenn nicht ermittelbar.
std::pair<quint32, QString> foregroundProcess();

// Fenster-Handle des Vordergrundfensters (0 = keins / nicht Windows).
quintptr foregroundWindowHandle();
// Gehoert das Fenster zu diesem Prozess (SSHIT-Commander selbst)?
bool isOwnProcessWindow(quintptr window);
// Holt ein Fenster nach vorn (minimiert -> wiederherstellen). false = nicht
// (mehr) vorhanden.
bool bringWindowToFront(quintptr window);

} // namespace ncssh::core
