// Git-Status fuer ein Verzeichnis (Badges M/A/D/R/? je direktem Kind).
// parsePorcelain ist rein/testbar; gitStatus fuehrt git aus (im Worker-Thread).
#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

namespace ncssh::core {

// Schnell (nur Dateisystem) pruefen, ob ein .git im Pfad-Baum liegt.
// Vermeidet das teure Starten von git in Nicht-Repos.
bool inGitRepo(const QString &directory);

// Schluessel fuer "gilt fuer alle Eintraege": das angezeigte Verzeichnis ist
// selbst neu (unverfolgt). '*' kommt in Windows-Dateinamen nicht vor.
inline const QString kGitAllEntries = QStringLiteral("*");

// "git status --porcelain"-Ausgabe -> {direkter_kind_name: badge}.
// Porcelain-Pfade sind IMMER relativ zur Repo-Wurzel; prefix ist der Pfad des
// angezeigten Verzeichnisses darin ("git rev-parse --show-prefix", z. B.
// "src/gui/"). Eintraege ausserhalb des Prefix werden ignoriert. Fuer tief
// liegende Aenderungen erhaelt das oberste Verzeichnis den Badge. Treffen
// mehrere unterschiedliche Stati auf denselben Namen, gewinnt "M" (gemischt).
// Meldet git das angezeigte Verzeichnis (oder einen Ordner darueber) als
// unverfolgt, steht "?" unter kGitAllEntries.
QHash<QString, QString> parsePorcelain(const QString &text, const QString &prefix = {});

// Fuehrt git status im Verzeichnis aus (leeres Dict, wenn kein Repo).
QHash<QString, QString> gitStatus(const QString &directory, int timeoutMs = 4000);

// Zwei Badges zusammenlegen: gleich -> dieser; "neu" und "neu, unverfolgt"
// ("A"/"?") -> "A" (beides neu, gruen); sonst "M" (gemischt). Leer zaehlt nicht.
QString mergeGitBadges(const QString &a, const QString &b);

// Sammelstatus mehrerer Badges (per mergeGitBadges): leer, wenn keine.
QString aggregateBadge(const QHash<QString, QString> &status);

// Name des direkten Kindes von directory, das auf dem Weg zu path liegt
// (path muss echt darunter liegen, sonst leer). Separatoren egal, unter
// Windows ohne Gross-/Kleinschreibung.
QString childTowards(const QString &directory, const QString &path);

// Markierungen OBERHALB von Repos: fuer jedes Repo (Wurzelpfad) unterhalb von
// directory mit Aenderungen erhaelt das Kind auf dem Weg dorthin dessen
// Sammelstatus -> {direkter_kind_name: badge}. Fuehrt git aus (Worker).
QHash<QString, QString> repoAncestorMarks(const QString &directory, const QStringList &repoRoots,
                                          int timeoutMs = 4000);

// Wurzel des Arbeitsverzeichnisses und URL von "origin" (leer, wenn kein Repo
// bzw. kein origin). Fuehrt git aus (Worker).
struct GitRepoInfo {
    QString root;
    QString originUrl;
};
GitRepoInfo gitRepoInfo(const QString &directory, int timeoutMs = 4000);

} // namespace ncssh::core
