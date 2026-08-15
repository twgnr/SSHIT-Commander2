// Git-Status fuer ein Verzeichnis (Badges M/A/D/R/? je direktem Kind).
// parsePorcelain ist rein/testbar; gitStatus fuehrt git aus (im Worker-Thread).
#pragma once

#include <QHash>
#include <QString>

namespace ncssh::core {

// Schnell (nur Dateisystem) pruefen, ob ein .git im Pfad-Baum liegt.
// Vermeidet das teure Starten von git in Nicht-Repos.
bool inGitRepo(const QString &directory);

// "git status --porcelain"-Ausgabe -> {direkter_kind_name: badge}.
// Pfade sind relativ zum Arbeitsverzeichnis; fuer tief liegende Aenderungen
// erhaelt das oberste Verzeichnis den Badge. Treffen mehrere unterschiedliche
// Stati auf denselben Namen, gewinnt "M" (gemischt).
QHash<QString, QString> parsePorcelain(const QString &text);

// Fuehrt git status im Verzeichnis aus (leeres Dict, wenn kein Repo).
QHash<QString, QString> gitStatus(const QString &directory, int timeoutMs = 4000);

} // namespace ncssh::core
