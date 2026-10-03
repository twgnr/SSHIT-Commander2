// Cloud-KI-Anbieter (Anthropic Claude, OpenAI bzw. OpenAI-kompatible Server,
// Google Gemini) — HTTP-Client mit Server-Sent-Events-Streaming.
//
// Wie net/ollama: nur QtNetwork, alle Funktionen BLOCKIEREND (laufen ueber die
// AsyncBridge auf Worker-Threads), Abbruch kooperativ ueber das CancelToken.
// Nachrichten kommen im gemeinsamen Format [{"role":"system|user|assistant",
// "content":str}] und werden je Anbieter umgesetzt.
#pragma once

#include "ncssh/gui/bridge.hpp"   // CancelToken

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <functional>
#include <stdexcept>

namespace ncssh::net {

using ncssh::gui::CancelTokenPtr;

// Anbieter-Kennungen (auch Einstellungswerte).
inline const QString kProviderOllama = QStringLiteral("ollama");
inline const QString kProviderAnthropic = QStringLiteral("anthropic");
inline const QString kProviderOpenAi = QStringLiteral("openai");
inline const QString kProviderOpenAiCompat = QStringLiteral("openai_compat");
inline const QString kProviderGemini = QStringLiteral("gemini");

inline const QString kAnthropicBaseUrl = QStringLiteral("https://api.anthropic.com");
inline const QString kOpenAiBaseUrl = QStringLiteral("https://api.openai.com/v1");
inline const QString kGeminiBaseUrl =
    QStringLiteral("https://generativelanguage.googleapis.com/v1beta");

// Fehler eines Cloud-Anbieters; die Meldung enthaelt (falls vorhanden) den
// Fehlertext des Anbieters, z.B. "invalid x-api-key".
class CloudAiError : public std::runtime_error {
public:
    explicit CloudAiError(const QString &message)
        : std::runtime_error(message.toStdString()) {}
};

struct CloudTarget {
    QString provider;   // kProvider*
    QString baseUrl;    // leer = Standard des Anbieters
    QString model;
    QString apiKey;     // bei OpenAI-kompatibel optional
};

using TextCallback = std::function<void(const QString &)>;

// Gestreamte Antwort; onText erhaelt die Text-Stuecke. Blockierend.
void cloudChat(const CloudTarget &target, const QJsonArray &messages,
               const TextCallback &onText, const CancelTokenPtr &cancel = {});

// Verfuegbare Modelle (IDs) — prueft zugleich Adresse und Schluessel. Blockierend.
QStringList cloudListModels(const CloudTarget &target, int timeoutMs = 15000);

// --- Einzeln testbare Bausteine (ohne Netz) ---------------------------------
// Anfrage-Body je Anbieter aus den gemeinsamen Nachrichten.
QJsonObject anthropicRequest(const QString &model, const QJsonArray &messages);
QJsonObject openAiRequest(const QString &model, const QJsonArray &messages);
QJsonObject geminiRequest(const QJsonArray &messages);
// Text aus einem SSE-"data:"-Objekt je Anbieter (leer = nichts auszugeben).
// Wirft CloudAiError bei Fehler-Ereignissen bzw. einer Ablehnung.
QString anthropicEventText(const QJsonObject &event);
QString openAiEventText(const QJsonObject &event);
QString geminiEventText(const QJsonObject &event);

} // namespace ncssh::net
