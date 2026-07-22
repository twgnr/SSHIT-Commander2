// Datei-Transfer zwischen zwei Providern, mit optionalem Fortschritt.
// Unterstuetzt lokal<->lokal, lokal<->remote (Up-/Download) und remote<->remote.
// (Port von net/transfer.py)
#pragma once

#include "ncssh/core/filesystem.hpp"

#include <QString>
#include <functional>

namespace ncssh::net {

using core::FileSystemProvider;
using ProgressFn = std::function<void(qint64 /*copied*/, qint64 /*total*/)>;

// Datenmodell fuer die Transfer-Queue der GUI.
struct TransferJob {
    int id = 0;
    QString name;
    QString srcLabel;
    QString dstLabel;
    QString direction;                       // upload | download | local | remote
    QString status = QStringLiteral("pending"); // pending|running|done|error|cancelled
    qint64 copied = 0;
    qint64 total = 0;
    QString error;
    bool verified = false;
    double speed = 0.0;                       // Bytes/s
    double eta = 0.0;                         // geschaetzte Restsekunden

    int percent() const { return total ? int(copied * 100 / total) : 0; }
};

QString directionOf(FileSystemProvider *src, FileSystemProvider *dst);
qint64 pathSize(FileSystemProvider *provider, const QString &path);
bool pathIsDir(FileSystemProvider *provider, const QString &path);

// Uebertraegt srcPath -> dstPath (Datei oder Verzeichnis, rekursiv) mit
// Fortschritt. resume setzt eine einzelne Datei am Ziel-Offset fort.
void transferWithProgress(FileSystemProvider *src, const QString &srcPath,
                          FileSystemProvider *dst, const QString &dstPath,
                          const ProgressFn &progress, bool resume = false);

// Prueft nach einem Verzeichnis-Transfer, ob jede Quelldatei am Ziel in
// gleicher Groesse vorliegt (Symlinks ausgenommen).
bool verifyTree(FileSystemProvider *src, const QString &srcPath,
                FileSystemProvider *dst, const QString &dstPath);

// Rueckwaertskompatibler einfacher Transfer (ohne Fortschritt).
QString transfer(FileSystemProvider *src, const QString &srcPath,
                 FileSystemProvider *dst, const QString &dstPath);

} // namespace ncssh::net
