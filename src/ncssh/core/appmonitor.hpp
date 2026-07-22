// Ermittelt das aktuell im Vordergrund liegende Programm (fuer kontext-
// abhaengige Layer im Makro-Manager).  Reine WinAPI-Loesung unter Windows;
// auf anderen Plattformen wird (0, "") geliefert.  (Port von core/appmonitor.py)
#pragma once

#include <QString>
#include <utility>

namespace ncssh::core {

// Liefert (pid, exe_basename_kleingeschrieben) des Vordergrundfensters
// oder (0, "") wenn nicht ermittelbar.
std::pair<quint32, QString> foregroundProcess();

} // namespace ncssh::core
