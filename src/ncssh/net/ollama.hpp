// Ollama-HTTP-Client (https://ollama.com) — lokales LLM, kein API-Key.
//
// Spricht einen lokal laufenden Ollama-Server ueber dessen REST-API an.
// Bewusst nur QtNetwork (QNetworkAccessManager + QEventLoop, blockierend im
// Worker) — wie net/cve — keine zusaetzliche Abhaengigkeit.
//
// Alle Funktionen sind BLOCKIEREND und laufen ueber die AsyncBridge auf
// Worker-Threads. Die Streaming-Funktionen (pullModel/chat) liefern jede
// NDJSON-Antwortzeile als geparstes QJsonObject ueber einen Callback.
// Abbruch erfolgt kooperativ ueber das CancelToken: es bricht den laufenden
// HTTP-Transfer ab — so propagiert die Cancellation bis in die HTTP-Schicht
// (bricht den laufenden Transfer ab).
#pragma once

#include "ncssh/gui/bridge.hpp"   // CancelToken

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <functional>
#include <stdexcept>
#include <vector>

namespace ncssh::net {

using ncssh::gui::CancelTokenPtr;

inline const QString DEFAULT_BASE_URL = QStringLiteral("http://localhost:11434");

// Health-Check / Modell-Liste (5 Sekunden).
inline constexpr int kConnectTimeoutMs = 5000;

// Allgemeiner Ollama-Fehler.
class OllamaError : public std::runtime_error {
public:
    explicit OllamaError(const QString &message)
        : std::runtime_error(message.toStdString()) {}
};

// Server nicht erreichbar (laeuft nicht / falsche URL / Timeout).
class OllamaUnreachable : public OllamaError {
public:
    using OllamaError::OllamaError;
};

// Empfaengt je gestreamter NDJSON-Zeile das geparste JSON-Objekt.
using JsonCallback = std::function<void(const QJsonObject &)>;

// Version des Servers (Health-Check). Blockierend.
QString version(const QString &baseUrl = DEFAULT_BASE_URL,
                int timeoutMs = kConnectTimeoutMs);

// Lokal vorhandene Modelle ("/api/tags"). Blockierend.
std::vector<QJsonObject> listModels(const QString &baseUrl = DEFAULT_BASE_URL,
                                    int timeoutMs = kConnectTimeoutMs);

// Laedt ein Modell herunter ("/api/pull", gestreamt).
//
// Liefert Fortschritts-Objekte {"status","digest","total","completed",...}
// an onProgress. Blockierend — via AsyncBridge konsumieren; Abbruch ueber
// das CancelToken.
void pullModel(const QString &baseUrl, const QString &model,
               const JsonCallback &onProgress,
               const CancelTokenPtr &cancel = {});

// Chat-Completion ("/api/chat", gestreamt).
//
// messages: [{"role":"system|user|assistant","content":str}, ...].
// Liefert {"message":{"role","content"},"done":bool} je Token-Chunk an
// onChunk; der letzte Chunk hat done=true. Blockierend — via AsyncBridge
// konsumieren; Abbruch ueber das CancelToken.
void chat(const QString &baseUrl, const QString &model,
          const QJsonArray &messages, const QJsonObject &options,
          const JsonCallback &onChunk,
          const CancelTokenPtr &cancel = {});

} // namespace ncssh::net
