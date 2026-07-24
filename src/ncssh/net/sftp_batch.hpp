// Skriptbarer SFTP-Batch: fuehrt eine Folge einfacher Dateibefehle gegen ein
// (Remote-)Dateisystem aus — Grundlage fuer geplante/wiederkehrende Aufgaben.
//
// Der Executor ist bewusst frei von GUI und laesst sich mit zwei lokalen
// Providern vollstaendig testen (put/get/mkdir/rm/rename/chmod/ln lokal<->lokal).
#pragma once

#include "ncssh/core/filesystem.hpp"
#include "ncssh/gui/bridge.hpp"

#include <QString>
#include <QStringList>
#include <functional>

namespace ncssh::net {

using core::FileSystemProvider;
using ncssh::gui::CancelTokenPtr;

struct BatchResult {
    QStringList log;      // eine Zeile je ausgefuehrtem Befehl (mit ✓/✗)
    int ok = 0;
    int failed = 0;
    bool aborted = false;
};

// Fuehrt `script` Zeile fuer Zeile aus. `remote` ist das Ziel-Dateisystem
// (SFTP), `local` das lokale (fuer put/get). Relative Pfade beziehen sich auf
// startRemoteDir bzw. startLocalDir; cd/lcd aendern das im Verlauf.
// onLog (optional) erhaelt jede Logzeile sofort (fuer Live-Ausgabe).
// stopOnError bricht beim ersten Fehler ab. cancel bricht kooperativ ab.
BatchResult runSftpBatch(const QString &script, FileSystemProvider *local,
                         FileSystemProvider *remote, const QString &startRemoteDir,
                         const QString &startLocalDir,
                         const std::function<void(const QString &)> &onLog = {},
                         bool stopOnError = false, const CancelTokenPtr &cancel = {});

// Zerlegt eine Befehlszeile in Tokens; doppelte Anfuehrungszeichen fassen
// Pfade mit Leerzeichen zusammen. Oeffentlich fuer Tests.
QStringList tokenizeBatchLine(const QString &line);

} // namespace ncssh::net
