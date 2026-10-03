// Cloud-KI-Anbieter — Implementierung (SSE-Streaming ueber QtNetwork).
#include "ncssh/net/cloudai.hpp"

#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <memory>
#include <optional>

namespace ncssh::net {

namespace {

// Anthropic: Version der Messages-API und die Modelle, fuer die wir den
// serverseitigen Fallback bei Ablehnungen einschalten (fallbacks: "default").
const QByteArray kAnthropicVersion = "2023-06-01";
const QByteArray kAnthropicFallbackBeta = "server-side-fallback-2026-07-01";
const QStringList kAnthropicFallbackModels = {
    QStringLiteral("claude-fable-5-1"), QStringLiteral("claude-opus-5-5"),
    QStringLiteral("claude-opus-5"), QStringLiteral("claude-sonnet-5-5"),
};

QString trimSlash(QString url)
{
    while (url.endsWith(QLatin1Char('/')))
        url.chop(1);
    return url;
}

QString baseOf(const CloudTarget &t)
{
    if (!t.baseUrl.trimmed().isEmpty())
        return trimSlash(t.baseUrl.trimmed());
    if (t.provider == kProviderAnthropic)
        return kAnthropicBaseUrl;
    if (t.provider == kProviderGemini)
        return kGeminiBaseUrl;
    return kOpenAiBaseUrl;
}

QNetworkRequest makeRequest(const CloudTarget &t, const QString &url)
{
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    const QByteArray key = t.apiKey.trimmed().toUtf8();
    if (t.provider == kProviderAnthropic) {
        req.setRawHeader("x-api-key", key);
        req.setRawHeader("anthropic-version", kAnthropicVersion);
    } else if (t.provider == kProviderGemini) {
        req.setRawHeader("x-goog-api-key", key);   // Schluessel nicht in die URL
    } else if (!key.isEmpty()) {
        req.setRawHeader("Authorization", "Bearer " + key);
    }
    return req;
}

// Fehlermeldung aus einem Fehler-Body ({"error":{"message":…}} bzw. als Liste).
QString providerMessage(const QByteArray &body)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    QJsonObject obj = doc.isArray() ? doc.array().first().toObject() : doc.object();
    const QJsonValue err = obj.value(QStringLiteral("error"));
    if (err.isObject())
        return err.toObject().value(QStringLiteral("message")).toString();
    if (err.isString())
        return err.toString();
    return {};
}

[[noreturn]] void throwHttp(QNetworkReply *reply, const QByteArray &body)
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QString msg = providerMessage(body);
    if (msg.isEmpty())
        msg = reply->errorString();
    QString hint;
    if (status == 401 || status == 403)
        hint = QStringLiteral(" — API-Schlüssel prüfen");
    else if (status == 404)
        hint = QStringLiteral(" — Modell oder Adresse prüfen");
    else if (status == 429)
        hint = QStringLiteral(" — Limit erreicht, später erneut versuchen");
    throw CloudAiError(status > 0 ? QStringLiteral("HTTP %1: %2%3").arg(status).arg(msg, hint)
                                  : QStringLiteral("Nicht erreichbar: %1").arg(msg));
}

// Systemtext aus den gemeinsamen Nachrichten (alle "system"-Eintraege).
QString systemText(const QJsonArray &messages)
{
    QStringList parts;
    for (const QJsonValue &v : messages) {
        const QJsonObject m = v.toObject();
        if (m.value(QStringLiteral("role")).toString() == QLatin1String("system"))
            parts << m.value(QStringLiteral("content")).toString();
    }
    return parts.join(QStringLiteral("\n\n"));
}

// POST mit SSE-Antwort: jede "data:"-Zeile als JSON an onEvent. Abbruch ueber
// das CancelToken beendet den Transfer still.
void streamSse(const CloudTarget &t, const QString &url, const QJsonObject &payload,
               const std::function<void(const QJsonObject &)> &onEvent,
               const CancelTokenPtr &cancel, const QList<std::pair<QByteArray, QByteArray>> &extraHeaders = {})
{
    QNetworkAccessManager nam;   // lebt auf dem Worker-Thread
    // Inaktivitaets-Timeout: lange Denkpausen senden Ping-Ereignisse, ein
    // komplett stummer Server gilt nach 5 Minuten als weg.
    nam.setTransferTimeout(300000);
    QNetworkRequest req = makeRequest(t, url);
    req.setRawHeader("Accept", "text/event-stream");
    for (const auto &[name, value] : extraHeaders)
        req.setRawHeader(name, value);
    std::unique_ptr<QNetworkReply> reply(
        nam.post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact)));

    QEventLoop loop;
    QByteArray buffer;
    QByteArray all;   // fuer die Fehlermeldung bei HTTP-Fehlern
    bool aborted = false;
    std::optional<CloudAiError> failure;
    const auto isCancelled = [&cancel] { return cancel && cancel->isCancelled(); };

    const auto handleLine = [&](const QByteArray &raw) {
        const QByteArray line = raw.trimmed();
        if (!line.startsWith("data:"))
            return;   // "event:", Kommentare, Leerzeilen
        const QByteArray data = line.mid(5).trimmed();
        if (data.isEmpty() || data == "[DONE]")
            return;
        const QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isObject() && onEvent)
            onEvent(doc.object());
    };
    const auto processBuffer = [&](bool atEnd) {
        qsizetype idx = -1;
        while ((idx = buffer.indexOf('\n')) >= 0) {
            const QByteArray raw = buffer.left(idx);
            buffer.remove(0, idx + 1);
            try {
                handleLine(raw);
            } catch (const CloudAiError &e) {
                failure = e;
                reply->abort();
                return;
            }
        }
        if (atEnd && !buffer.isEmpty()) {
            try {
                handleLine(buffer);
            } catch (const CloudAiError &e) {
                failure = e;
            }
            buffer.clear();
        }
    };

    QObject::connect(reply.get(), &QNetworkReply::readyRead, &loop, [&] {
        if (isCancelled()) {
            aborted = true;
            reply->abort();
            return;
        }
        const QByteArray chunk = reply->readAll();
        all += chunk;
        // Nur bei Erfolg als SSE lesen; Fehler-Bodies sind normales JSON.
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 400)
            return;
        buffer += chunk;
        processBuffer(false);
    });
    QObject::connect(reply.get(), &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer poll;
    poll.setInterval(100);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (isCancelled()) {
            aborted = true;
            reply->abort();
        }
    });
    poll.start();
    if (!reply->isFinished())
        loop.exec();
    poll.stop();

    if (aborted || isCancelled())
        return;
    if (failure)
        throw *failure;
    all += reply->readAll();
    if (reply->error() != QNetworkReply::NoError)
        throwHttp(reply.get(), all);
    processBuffer(true);
    if (failure)
        throw *failure;
}

QJsonObject getJson(const CloudTarget &t, const QString &url, int timeoutMs)
{
    QNetworkAccessManager nam;
    nam.setTransferTimeout(timeoutMs);
    std::unique_ptr<QNetworkReply> reply(nam.get(makeRequest(t, url)));
    QEventLoop loop;
    QObject::connect(reply.get(), &QNetworkReply::finished, &loop, &QEventLoop::quit);
    if (!reply->isFinished())
        loop.exec();
    const QByteArray body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError)
        throwHttp(reply.get(), body);
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject())
        throw CloudAiError(QStringLiteral("Ungültige Antwort von %1").arg(url));
    return doc.object();
}

} // namespace

// --- Anfrage-Bodies ----------------------------------------------------------

QJsonObject anthropicRequest(const QString &model, const QJsonArray &messages)
{
    QJsonArray turns;
    for (const QJsonValue &v : messages) {
        const QJsonObject m = v.toObject();
        const QString role = m.value(QStringLiteral("role")).toString();
        if (role == QLatin1String("user") || role == QLatin1String("assistant"))
            turns.append(QJsonObject{{QStringLiteral("role"), role},
                                     {QStringLiteral("content"),
                                      m.value(QStringLiteral("content")).toString()}});
    }
    QJsonObject body{
        {QStringLiteral("model"), model},
        {QStringLiteral("max_tokens"), 64000},   // gestreamt -> kein HTTP-Timeout
        {QStringLiteral("stream"), true},
        {QStringLiteral("messages"), turns},
    };
    const QString system = systemText(messages);
    if (!system.isEmpty())
        body.insert(QStringLiteral("system"), system);
    if (kAnthropicFallbackModels.contains(model)) {
        // Aktuelle Modelle: Erklaer-/Analyseaufgaben brauchen kein Maximum an
        // Denktiefe; bei einer Ablehnung durch den Sicherheitsfilter
        // beantwortet ein von Anthropic empfohlenes Ersatzmodell.
        body.insert(QStringLiteral("output_config"),
                    QJsonObject{{QStringLiteral("effort"), QStringLiteral("medium")}});
        body.insert(QStringLiteral("fallbacks"), QStringLiteral("default"));
    }
    return body;
}

QJsonObject openAiRequest(const QString &model, const QJsonArray &messages)
{
    QJsonArray turns;
    for (const QJsonValue &v : messages) {
        const QJsonObject m = v.toObject();
        turns.append(QJsonObject{
            {QStringLiteral("role"), m.value(QStringLiteral("role")).toString()},
            {QStringLiteral("content"), m.value(QStringLiteral("content")).toString()}});
    }
    return QJsonObject{
        {QStringLiteral("model"), model},
        {QStringLiteral("stream"), true},
        {QStringLiteral("messages"), turns},
    };
}

QJsonObject geminiRequest(const QJsonArray &messages)
{
    QJsonArray contents;
    for (const QJsonValue &v : messages) {
        const QJsonObject m = v.toObject();
        const QString role = m.value(QStringLiteral("role")).toString();
        if (role == QLatin1String("system"))
            continue;
        contents.append(QJsonObject{
            {QStringLiteral("role"),
             role == QLatin1String("assistant") ? QStringLiteral("model") : QStringLiteral("user")},
            {QStringLiteral("parts"),
             QJsonArray{QJsonObject{
                 {QStringLiteral("text"), m.value(QStringLiteral("content")).toString()}}}}});
    }
    QJsonObject body{{QStringLiteral("contents"), contents}};
    const QString system = systemText(messages);
    if (!system.isEmpty())
        body.insert(QStringLiteral("systemInstruction"),
                    QJsonObject{{QStringLiteral("parts"),
                                 QJsonArray{QJsonObject{{QStringLiteral("text"), system}}}}});
    return body;
}

// --- Stream-Ereignisse -------------------------------------------------------

QString anthropicEventText(const QJsonObject &event)
{
    const QString type = event.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("error"))
        throw CloudAiError(QStringLiteral("Claude: %1").arg(
            event.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString()));
    if (type == QLatin1String("message_delta")
        && event.value(QStringLiteral("delta")).toObject().value(QStringLiteral("stop_reason"))
               .toString() == QLatin1String("refusal"))
        throw CloudAiError(QStringLiteral(
            "Claude hat die Anfrage abgelehnt (Sicherheitsfilter). Frage umformulieren."));
    if (type != QLatin1String("content_block_delta"))
        return {};
    const QJsonObject delta = event.value(QStringLiteral("delta")).toObject();
    if (delta.value(QStringLiteral("type")).toString() != QLatin1String("text_delta"))
        return {};   // z.B. Denk-Bloecke — nicht anzeigen
    return delta.value(QStringLiteral("text")).toString();
}

QString openAiEventText(const QJsonObject &event)
{
    if (event.contains(QStringLiteral("error")))
        throw CloudAiError(QStringLiteral("OpenAI: %1").arg(
            event.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString()));
    const QJsonArray choices = event.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty())
        return {};
    return choices.first().toObject().value(QStringLiteral("delta")).toObject()
        .value(QStringLiteral("content")).toString();
}

QString geminiEventText(const QJsonObject &event)
{
    if (event.contains(QStringLiteral("error")))
        throw CloudAiError(QStringLiteral("Gemini: %1").arg(
            event.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString()));
    const QString blocked = event.value(QStringLiteral("promptFeedback")).toObject()
                                .value(QStringLiteral("blockReason")).toString();
    if (!blocked.isEmpty())
        throw CloudAiError(QStringLiteral("Gemini hat die Anfrage blockiert (%1).").arg(blocked));
    QString text;
    const QJsonArray candidates = event.value(QStringLiteral("candidates")).toArray();
    if (candidates.isEmpty())
        return {};
    for (const QJsonValue &p : candidates.first().toObject().value(QStringLiteral("content"))
                                   .toObject().value(QStringLiteral("parts")).toArray())
        text += p.toObject().value(QStringLiteral("text")).toString();
    return text;
}

// --- Oeffentliche Funktionen -------------------------------------------------

void cloudChat(const CloudTarget &target, const QJsonArray &messages, const TextCallback &onText,
               const CancelTokenPtr &cancel)
{
    if (target.model.trimmed().isEmpty())
        throw CloudAiError(QStringLiteral("Kein Modell gewählt (Einstellungen → KI)."));
    if (target.apiKey.trimmed().isEmpty() && target.provider != kProviderOpenAiCompat)
        throw CloudAiError(QStringLiteral("Kein API-Schlüssel hinterlegt (Einstellungen → KI)."));
    const QString base = baseOf(target);
    const auto emitText = [&onText](const QString &text) {
        if (!text.isEmpty() && onText)
            onText(text);
    };
    if (target.provider == kProviderAnthropic) {
        QList<std::pair<QByteArray, QByteArray>> headers;
        if (kAnthropicFallbackModels.contains(target.model))
            headers.append({"anthropic-beta", kAnthropicFallbackBeta});
        streamSse(target, base + QStringLiteral("/v1/messages"),
                  anthropicRequest(target.model, messages),
                  [&](const QJsonObject &e) { emitText(anthropicEventText(e)); }, cancel, headers);
    } else if (target.provider == kProviderGemini) {
        const QString url = base + QStringLiteral("/models/%1:streamGenerateContent?alt=sse")
                                       .arg(QString::fromLatin1(QUrl::toPercentEncoding(target.model)));
        streamSse(target, url, geminiRequest(messages),
                  [&](const QJsonObject &e) { emitText(geminiEventText(e)); }, cancel);
    } else {
        streamSse(target, base + QStringLiteral("/chat/completions"),
                  openAiRequest(target.model, messages),
                  [&](const QJsonObject &e) { emitText(openAiEventText(e)); }, cancel);
    }
}

QStringList cloudListModels(const CloudTarget &target, int timeoutMs)
{
    if (target.apiKey.trimmed().isEmpty() && target.provider != kProviderOpenAiCompat)
        throw CloudAiError(QStringLiteral("Bitte zuerst den API-Schlüssel eintragen."));
    const QString base = baseOf(target);
    QStringList ids;
    if (target.provider == kProviderAnthropic) {
        const QJsonObject data = getJson(target, base + QStringLiteral("/v1/models?limit=100"), timeoutMs);
        for (const QJsonValue &v : data.value(QStringLiteral("data")).toArray())
            ids << v.toObject().value(QStringLiteral("id")).toString();
    } else if (target.provider == kProviderGemini) {
        const QJsonObject data = getJson(target, base + QStringLiteral("/models?pageSize=200"), timeoutMs);
        for (const QJsonValue &v : data.value(QStringLiteral("models")).toArray()) {
            const QJsonObject m = v.toObject();
            bool chat = false;
            for (const QJsonValue &method : m.value(QStringLiteral("supportedGenerationMethods")).toArray())
                chat |= method.toString() == QLatin1String("generateContent");
            if (chat)
                ids << m.value(QStringLiteral("name")).toString().remove(QStringLiteral("models/"));
        }
    } else {
        const QJsonObject data = getJson(target, base + QStringLiteral("/models"), timeoutMs);
        for (const QJsonValue &v : data.value(QStringLiteral("data")).toArray())
            ids << v.toObject().value(QStringLiteral("id")).toString();
        ids.sort();
    }
    ids.removeAll(QString());
    return ids;
}

} // namespace ncssh::net
