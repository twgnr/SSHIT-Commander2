// Such-Befehle (Dateiname / Inhalt) — OS-abhaengig, rein/testbar.
// Unterstuetzt Wildcards (Glob) und Regex sowie diverse Filter (Typ, Binaer-
// dateien einbeziehen, ganzes Wort, Ordner ausschliessen, nur Dateinamen).
// (Port von core/search.py)
#pragma once

#include <QString>
#include <optional>

namespace ncssh::core {

struct NameSearchOptions {
    bool regex = false;
    QString kind = QStringLiteral("all");  // "all" | "file" | "dir"
    std::optional<int> maxDepth;
    std::optional<qint64> minSize;          // Bytes (nur Dateien)
    std::optional<qint64> maxSize;
    std::optional<double> newerThanDays;    // nur in den letzten N Tagen geaendert
};

// Sucht nach Datei-/Ordnernamen; liefert die Befehlszeile fuer das Ziel-OS.
QString nameSearchCmd(const QString &osType, const QString &root, const QString &pattern,
                      bool ignoreCase = true, int limit = 2000,
                      const NameSearchOptions &opts = {});

struct ContentSearchOptions {
    bool regex = false;
    bool wholeWord = false;
    bool includeBinary = false;
    bool namesOnly = false;
    QString excludeDir;   // Ordnernamen ausschliessen (kommagetrennt, POSIX)
    QString exclude;      // Datei-Globs ausschliessen (kommagetrennt, POSIX)
    bool invert = false;
    int context = 0;      // Kontextzeilen
};

// Durchsucht Datei-Inhalte; include = Glob-Filter (kommagetrennt moeglich).
QString contentSearchCmd(const QString &osType, const QString &root, const QString &text,
                         const QString &include = {}, bool ignoreCase = true,
                         int limit = 2000, const ContentSearchOptions &opts = {});

// Aus "pfad:zeile:text" den reinen Dateipfad extrahieren (auch Windows C:\...).
QString stripMatchLocation(const QString &line);

// POSIX-Shell-Quoting (Aequivalent zu Python shlex.quote).
QString shellQuote(const QString &s);

} // namespace ncssh::core
