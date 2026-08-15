// Befehlsausfuehrung — Abstraktion ueber lokal (QProcess) und remote (ssh).
//
// Alle Methoden sind BLOCKIEREND; sie laufen ueber die AsyncBridge auf
// Worker-Threads. stream() liefert Zeilen ueber einen Callback und bricht
// kooperativ ueber das CancelToken ab.
#pragma once

#include "ncssh/core/models.hpp"
#include "ncssh/gui/bridge.hpp"   // CancelToken

#include <QString>
#include <functional>
#include <optional>

namespace ncssh::core {

using ncssh::gui::CancelTokenPtr;
using LineCallback = std::function<void(const QString &)>;

// Fuehrt Shell-Befehle in einem Arbeitsverzeichnis aus.
class CommandRunner {
public:
    virtual ~CommandRunner() = default;

    QString label = QStringLiteral("local");
    // Exit-Code des zuletzt beendeten Befehls (nullopt, solange unbekannt).
    std::optional<int> lastExitStatus;

    // Liefert Ausgabezeilen (ohne Zeilenende), waehrend der Befehl laeuft.
    virtual void stream(const QString &command, const QString &cwd,
                        const LineCallback &onLine, const CancelTokenPtr &cancel) = 0;

    // Loest ein `cd <target>` relativ zu cwd auf; nullopt falls ungueltig.
    virtual std::optional<QString> resolveDir(const QString &cwd, const QString &target) = 0;

    // Wie stream, aber fuer farbige Terminal-Ausgabe (Roh-Text inkl. Umbruch).
    // Standard: zeilenbasiert. Remote ueberschreibt dies mit einem echten PTY.
    virtual void runTerminal(const QString &command, const QString &cwd,
                             const LineCallback &onChunk, const CancelTokenPtr &cancel,
                             int cols = 120, int rows = 40);
};

class LocalCommandRunner : public CommandRunner {
public:
    LocalCommandRunner();

    void stream(const QString &command, const QString &cwd,
                const LineCallback &onLine, const CancelTokenPtr &cancel) override;
    std::optional<QString> resolveDir(const QString &cwd, const QString &target) override;
};

} // namespace ncssh::core
