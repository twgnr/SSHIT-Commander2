// Lokale Datei-Helfer: Pruefsummen, ZIP erstellen, Ordnergroesse (Bordmittel).
// (Port von core/fileops.py)
#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <optional>
#include <utility>
#include <vector>

namespace ncssh::core {

bool isArchive(const QString &name);

// Dateiname ohne Archiv-Endung (z.B. "daten.tar.gz" -> "daten").
QString archiveStem(const QString &name);

// Entpackt ein ZIP-/TAR-Archiv nach destDir (wird angelegt). Gibt die Anzahl
// der Eintraege zurueck. Wirft bei nicht unterstuetztem Format oder Pfad-Ausbruch.
int extractArchive(const QString &archive, const QString &destDir);

// Streaming-Pruefsumme einer lokalen Datei (Hex). algo: "md5"|"sha1"|"sha256"|"sha512".
QString hashFile(const QString &path, const QString &algo = QStringLiteral("sha256"));
QString hashBytes(const QByteArray &data, const QString &algo = QStringLiteral("sha256"));

// Packt names (relativ zu baseDir; Dateien/Ordner) in archive. Gibt die Anzahl
// geschriebener Eintraege zurueck.
int makeZip(const QString &archive, const QString &baseDir, const QStringList &names);

struct DirStats {
    qint64 size = 0;
    bool truncated = false;
    qint64 files = 0;
    qint64 dirs = 0;
    qint64 hidden = 0;
    std::optional<std::pair<QString, QDateTime>> newestModified;
    std::optional<std::pair<QString, QDateTime>> oldestModified;
    std::optional<std::pair<QString, QDateTime>> newestCreated;
    std::optional<std::pair<QString, qint64>> largest;
    std::vector<std::pair<QString, int>> topExt;  // bis zu 5
};

// Rekursive Statistik eines lokalen Verzeichnisses.
DirStats dirStats(const QString &path, int limitEntries = 200'000);

// Rekursive Gesamtgroesse -> (bytes, gekuerzt).
std::pair<qint64, bool> dirSize(const QString &path, int limitEntries = 200'000);

} // namespace ncssh::core
