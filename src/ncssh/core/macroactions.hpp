// Ausfuehrung der Makro-Aktionen — geraeteunabhaengiger Port des Stream-Deck-
// Handlers.
//
// Es laeuft KEINE Stream-Deck-Hardware: Tasten werden
// in der Qt-Oberflaeche angeklickt. Die Aktionslogik (Programme starten,
// Tastatur/Maus simulieren, Fenster verwalten, HTTP, Medien) ist uebernommen.
//
// Tastatur/Maus/Fenster laufen unter Windows nativ ueber die WinAPI (SendInput,
// keybd_event, EnumWindows) — die Python-Pakete pynput/pygetwindow/pycaw
// entfallen. Auf anderen Plattformen liefern diese Aktionen eine klare Meldung.
//
// executeAction gibt nullopt bei Erfolg oder eine Fehlermeldung zurueck (wirft
// nie), damit der Aufrufer Fehler ruhig anzeigen kann.
#pragma once

#include <QHash>
#include <QJsonValue>
#include <QMutex>
#include <QString>
#include <functional>
#include <optional>
#include <vector>

namespace ncssh::core::macroactions {

// Aktions-Metadaten — von Editor und Ausfuehrung gemeinsam genutzt.
// editor: text | number | none | layer | window | sequence | ssh | json
struct ActionSpec {
    QString key;
    QString label;
    QString group;
    QString editor = QStringLiteral("text");
    QString tooltip;
    bool navigation = false;  // Layerwechsel — vom Fenster behandelt
    bool gui = false;         // Popups/Zustandstasten — vom Fenster behandelt
};

const std::vector<ActionSpec> &actionSpecs();
const ActionSpec &spec(const QString &actionType);
// Aktionen nach Gruppe, Reihenfolge wie in actionSpecs().
std::vector<std::pair<QString, std::vector<ActionSpec>>> groupedActions();

// Ausfuehrungskontext — Bruecke zur Anwendung (z.B. SSH-Konsole).
// executeAction laeuft auf WORKER-Threads (oft mehrere parallel): die
// Rueckrufe muessen daher selbst in den GUI-Thread wechseln (der
// Makro-Manager haengt dafuer eine Weiterleitung davor), und die Zustands-
// Tabellen duerfen nur unter stateMutex angefasst werden.
struct ExecContext {
    // Befehl an die aktive Konsole (cmd, run). Wird im Worker gerufen.
    std::function<void(const QString &, bool)> sshSend;
    // Befehl an alle Konsolen des aktiven Tabs. Wird im Worker gerufen.
    std::function<void(const QString &, bool)> sshBroadcast;
    // Schuetzt toggleState/cycleIndex (parallele Worker + GUI-Thread).
    QMutex stateMutex;
    // Zustand fuer toggle_key ueber mehrere Tastendruecke.
    QHash<QString, bool> toggleState;
    // Laufende Index-Position je Taste (Mehrzustands-Tasten im Makro-Manager).
    QHash<QString, int> cycleIndex;
};

// Fuehrt eine Aktion aus. Gibt nullopt oder die Fehlermeldung zurueck.
std::optional<QString> executeAction(const QString &actionType,
                                     const QJsonValue &payload = QStringLiteral(""),
                                     ExecContext *context = nullptr,
                                     const QString &keyId = {});

// Wandelt ein Kuerzel wie "ctrl+alt+1" in das interne Hotkey-Format.
QString toPynputHotkey(const QString &shortcut);

// Liefert einen Hinweis, falls eine Aktion auf dieser Plattform nicht
// verfuegbar ist (sonst nullopt).
std::optional<QString> missingDependencyHint(const QString &actionType);

} // namespace ncssh::core::macroactions
