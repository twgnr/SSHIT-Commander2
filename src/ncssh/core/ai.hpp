// KI-Assistent: Orchestrierung rund um den lokalen Ollama-Server.
// (Port von core/ai.py; enthaelt zusaetzlich den Ollama-HTTP-Client aus
// net/ollama.py — HTTP via QNetworkAccessManager, blockierend im Worker.)
//
// Buendelt drei Dinge, alle UI-frei und damit testbar:
//
// * Einstellungen — schlanke Accessoren ueber core/settings.
// * Streaming — die blockierenden Streaming-Generatoren des Originals werden
//   zu blockierenden Methoden mit Zeilen-Callback (LineCallback) und
//   kooperativem Abbruch (CancelTokenPtr); die GUI konsumiert sie ueber
//   AsyncBridge::stream. Der Blocking->Async-Adapter (_aiter_blocking)
//   entfaellt, weil die Bridge bereits Worker-Threads stellt.
// * Prompts & Kontext — baut die Nachrichten fuer "Terminalausgabe erklaeren"
//   und "Frage zur Datei" und deckelt zu langen Kontext.
//
// Der Assistent ist rein beratend: die System-Prompts weisen das Modell an,
// Befehle nur vorzuschlagen und nichts auszufuehren.
#pragma once

#include "ncssh/core/runner.hpp"   // LineCallback, CancelTokenPtr

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <stdexcept>
#include <utility>

namespace ncssh::core {

// --- Einstellungen -----------------------------------------------------------
inline constexpr const char *AI_ENABLED = "ai_enabled";
inline constexpr const char *OLLAMA_URL = "ollama_url";
inline constexpr const char *AI_MODEL = "ai_model";

bool aiEnabled();
QString ollamaUrl();
QString aiModel();

// --- Ollama-HTTP-Client (Port von net/ollama.py) -----------------------------
// Spricht einen lokal laufenden Ollama-Server ueber dessen REST-API an.
// Alle Funktionen sind blockierend und laufen ueber die AsyncBridge auf
// Worker-Threads. Abbruch schliesst die HTTP-Verbindung (QNetworkReply::abort).

inline constexpr const char *OLLAMA_DEFAULT_BASE_URL = "http://localhost:11434";
inline constexpr int OLLAMA_CONNECT_TIMEOUT_MS = 5000;  // Health-Check / Modell-Liste

// Allgemeiner Ollama-Fehler.
class OllamaError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Server nicht erreichbar (laeuft nicht / falsche URL / Timeout).
class OllamaUnreachable : public OllamaError {
public:
    using OllamaError::OllamaError;
};

// Version des Servers (Health-Check). Blockierend.
QString ollamaVersion(const QString &baseUrl = QString::fromLatin1(OLLAMA_DEFAULT_BASE_URL),
                      int timeoutMs = OLLAMA_CONNECT_TIMEOUT_MS);

// Lokal vorhandene Modelle (/api/tags) als Liste von JSON-Objekten. Blockierend.
QJsonArray listModels(const QString &baseUrl = QString::fromLatin1(OLLAMA_DEFAULT_BASE_URL),
                      int timeoutMs = OLLAMA_CONNECT_TIMEOUT_MS);

// Chat-Completion (/api/chat, gestreamt). messages:
//   [{"role":"system|user|assistant","content":"..."}, ...]
// onText erhaelt die Antwort-Textstuecke (Token-Deltas). Blockierend.
void chatStream(const QString &baseUrl, const QString &model, const QJsonArray &messages,
                const QJsonObject &options, const LineCallback &onText,
                const CancelTokenPtr &cancel);

// Laedt ein Modell herunter (/api/pull, gestreamt). onLine erhaelt den
// Fortschritt als "completed\ttotal\tstatus" — Strings statt Objekte, damit der
// vorhandene Zeilen-Callback der Bridge unveraendert genutzt werden kann; das
// Panel splittet mit parsePullLine wieder. Blockierend.
void pullStream(const QString &baseUrl, const QString &model,
                const LineCallback &onLine, const CancelTokenPtr &cancel);

// Kehrt pullStream um: "completed\ttotal\tstatus" -> Struktur.
struct PullProgress {
    qint64 completed = 0;
    qint64 total = 0;
    QString status;
};
PullProgress parsePullLine(const QString &line);

// --- Kontext-Deckelung -------------------------------------------------------
inline constexpr int AI_MAX_CONTEXT_CHARS = 24000;

// Deckelt Datei-Inhalt; behaelt den Anfang (Header/Direktiven zuerst).
// Rueckgabe: (Text, wurdeGekuerzt).
std::pair<QString, bool> truncateFile(const QString &text, int limit = AI_MAX_CONTEXT_CHARS);

// Deckelt Terminalausgabe; behaelt das Ende (Fehler stehen meist unten).
std::pair<QString, bool> truncateTerminal(const QString &text, int limit = AI_MAX_CONTEXT_CHARS);

// --- Prompts -----------------------------------------------------------------
extern const char *const SYSTEM_TERMINAL;
extern const char *const SYSTEM_FILE;
extern const char *const SYSTEM_REPAIR;
extern const char *const SYSTEM_CODECHECK;

// Nachrichten fuer "beschaedigten Text per KI reparieren".
QJsonArray buildRepairMessages(const QString &text);

// Nachrichten fuer "Terminalausgabe erklaeren".
QJsonArray buildTerminalMessages(const QString &output, const QString &question = {});

// Nachrichten fuer "Frage zur Datei / Config erklaeren".
QJsonArray buildFileMessages(const QString &filename, const QString &content,
                             const QString &question = {});

// --- Quellcode-Fehleranalyse -------------------------------------------------
// Sprachname, wenn die Endung zu einer bekannten Programmiersprache gehoert
// (bestimmt den "KI Fehleranalyse"-Button). Leer = unbekannt.
QString sourceLanguage(const QString &filename);

// Nachrichten fuer "Quellcode auf Fehler pruefen" (nur Fehler, keine Erklaerung).
QJsonArray buildCodecheckMessages(const QString &filename, const QString &content,
                                  const QString &language = {});

} // namespace ncssh::core
