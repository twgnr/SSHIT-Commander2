// Ollama-HTTP-Client — Implementierung.
#include "ncssh/net/ollama.hpp"

#include <QEventLoop>
#include <QJsonDocument>
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

QString joinUrl(const QString &baseUrl, const QString &path)
{
    QString base = baseUrl;
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    return base + path;
}

QNetworkRequest makeRequest(const QString &baseUrl, const QString &path)
{
    QNetworkRequest req{QUrl(joinUrl(baseUrl, path))};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    return req;
}

// Startet die Anfrage: mit Payload als POST (JSON-Body), sonst GET.
QNetworkReply *startRequest(QNetworkAccessManager &nam, const QString &baseUrl,
                            const QString &path, const std::optional<QJsonObject> &payload)
{
    const QNetworkRequest req = makeRequest(baseUrl, path);
    if (payload)
        return nam.post(req, QJsonDocument(*payload).toJson(QJsonDocument::Compact));
    return nam.get(req);
}

// Fuehrt die Anfrage blockierend aus und parst den kompletten Body als
// JSON-Objekt. Verbindungs-/HTTP-Fehler werden als OllamaUnreachable geworfen.
QJsonObject readJson(const QString &baseUrl, const QString &path,
                     const std::optional<QJsonObject> &payload, int timeoutMs)
{
    QNetworkAccessManager nam;  // lokal je Aufruf — lebt auf dem Worker-Thread
    nam.setTransferTimeout(timeoutMs);
    std::unique_ptr<QNetworkReply> reply(startRequest(nam, baseUrl, path, payload));

    QEventLoop loop;
    QObject::connect(reply.get(), &QNetworkReply::finished, &loop, &QEventLoop::quit);
    if (!reply->isFinished())
        loop.exec();

    if (reply->error() != QNetworkReply::NoError)
        throw OllamaUnreachable(QStringLiteral("Ollama nicht erreichbar: %1")
                                    .arg(reply->errorString()));

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        throw OllamaError(QStringLiteral("Ungültige JSON-Antwort des Ollama-Servers"));
    return doc.object();
}

// Oeffnet die Anfrage und liefert jede Antwortzeile als geparstes JSON-Objekt
// an onObject (NDJSON-Streaming). Leere und nicht parsbare Zeilen werden
// uebersprungen. Verbindungsfehler werden als
// OllamaUnreachable geworfen; ein Abbruch ueber das CancelToken beendet den
// Transfer still.
void streamJsonLines(const QString &baseUrl, const QString &path,
                     const QJsonObject &payload, const JsonCallback &onObject,
                     const CancelTokenPtr &cancel)
{
    QNetworkAccessManager nam;  // lokal je Aufruf — lebt auf dem Worker-Thread
    nam.setTransferTimeout(0);  // kein Timeout — Streams duerfen lange laufen
    std::unique_ptr<QNetworkReply> reply(startRequest(nam, baseUrl, path, payload));

    QEventLoop loop;
    QByteArray buffer;
    bool aborted = false;

    const auto isCancelled = [&cancel] { return cancel && cancel->isCancelled(); };

    // Verarbeitet alle vollstaendigen Zeilen im Puffer; atEnd auch den Rest
    // ohne abschliessendes '\n'. Bricht kooperativ zwischen den Zeilen ab.
    const auto processBuffer = [&](bool atEnd) {
        const auto emitLine = [&](const QByteArray &raw) {
            const QByteArray line = raw.trimmed();
            if (line.isEmpty())
                return;
            QJsonParseError err{};
            const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
            if (err.error != QJsonParseError::NoError || !doc.isObject())
                return;
            if (onObject)
                onObject(doc.object());
        };
        qsizetype idx = -1;
        while ((idx = buffer.indexOf('\n')) >= 0) {
            const QByteArray raw = buffer.left(idx);
            buffer.remove(0, idx + 1);
            emitLine(raw);
            if (isCancelled()) {
                aborted = true;
                reply->abort();
                return;
            }
        }
        if (atEnd) {
            emitLine(buffer);
            buffer.clear();
        }
    };

    QObject::connect(reply.get(), &QNetworkReply::readyRead, &loop, [&] {
        if (isCancelled()) {
            aborted = true;
            reply->abort();
            return;
        }
        buffer += reply->readAll();
        processBuffer(false);
    });
    QObject::connect(reply.get(), &QNetworkReply::finished, &loop, &QEventLoop::quit);

    // Pollt das CancelToken auch, wenn gerade keine Daten eintreffen.
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
        return;  // kooperativer Abbruch — kein Fehler

    if (reply->error() != QNetworkReply::NoError)
        throw OllamaUnreachable(QStringLiteral("Ollama nicht erreichbar: %1")
                                    .arg(reply->errorString()));

    buffer += reply->readAll();
    processBuffer(true);
}

} // namespace

QString version(const QString &baseUrl, int timeoutMs)
{
    const QJsonObject data =
        readJson(baseUrl, QStringLiteral("/api/version"), std::nullopt, timeoutMs);
    return data.value(QStringLiteral("version")).toVariant().toString();
}

std::vector<QJsonObject> listModels(const QString &baseUrl, int timeoutMs)
{
    const QJsonObject data =
        readJson(baseUrl, QStringLiteral("/api/tags"), std::nullopt, timeoutMs);
    const QJsonArray arr = data.value(QStringLiteral("models")).toArray();
    std::vector<QJsonObject> models;
    models.reserve(static_cast<std::size_t>(arr.size()));
    for (const QJsonValue &value : arr) {
        if (value.isObject())
            models.push_back(value.toObject());
    }
    return models;
}

void pullModel(const QString &baseUrl, const QString &model,
               const JsonCallback &onProgress, const CancelTokenPtr &cancel)
{
    const QJsonObject payload{
        {QStringLiteral("name"), model},
        {QStringLiteral("stream"), true},
    };
    streamJsonLines(baseUrl, QStringLiteral("/api/pull"), payload, onProgress, cancel);
}

void chat(const QString &baseUrl, const QString &model, const QJsonArray &messages,
          const QJsonObject &options, const JsonCallback &onChunk,
          const CancelTokenPtr &cancel)
{
    QJsonObject payload{
        {QStringLiteral("model"), model},
        {QStringLiteral("messages"), messages},
        {QStringLiteral("stream"), true},
    };
    if (!options.isEmpty())
        payload.insert(QStringLiteral("options"), options);
    streamJsonLines(baseUrl, QStringLiteral("/api/chat"), payload, onChunk, cancel);
}

} // namespace ncssh::net
