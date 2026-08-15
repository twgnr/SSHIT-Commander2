// Online-CVE-Abgleich ueber OSV.dev — Implementierung.
#include "ncssh/net/cve.hpp"

#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <memory>
#include <stdexcept>

namespace ncssh::net {

namespace {

const QString kBatchUrl = QStringLiteral("https://api.osv.dev/v1/querybatch");
const QString kVulnUrl = QStringLiteral("https://api.osv.dev/v1/vulns/");

// Fuehrt die Anfrage blockierend aus (POST bei postData, sonst GET) und
// liefert den kompletten Antwort-Body. Netz-/HTTP-Fehler werfen.
QByteArray fetchBlocking(const QUrl &url, const QByteArray *postData, int timeoutMs)
{
    QNetworkAccessManager nam;  // lokal je Aufruf — lebt auf dem Worker-Thread
    nam.setTransferTimeout(timeoutMs);

    QNetworkRequest req{url};
    std::unique_ptr<QNetworkReply> reply;
    if (postData) {
        req.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
        reply.reset(nam.post(req, *postData));
    } else {
        reply.reset(nam.get(req));
    }

    QEventLoop loop;
    QObject::connect(reply.get(), &QNetworkReply::finished, &loop, &QEventLoop::quit);
    if (!reply->isFinished())
        loop.exec();

    if (reply->error() != QNetworkReply::NoError)
        throw std::runtime_error(QStringLiteral("OSV.dev-Anfrage fehlgeschlagen: %1")
                                     .arg(reply->errorString())
                                     .toStdString());
    return reply->readAll();
}

QJsonObject parseObject(const QByteArray &raw)
{
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        throw std::runtime_error(
            QStringLiteral("Ungültige JSON-Antwort von OSV.dev").toStdString());
    return doc.object();
}

} // namespace

QJsonObject osvQuerybatch(const QJsonArray &queries, int timeoutMs)
{
    const QJsonObject payload{{QStringLiteral("queries"), queries}};
    const QByteArray data = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    return parseObject(fetchBlocking(QUrl(kBatchUrl), &data, timeoutMs));
}

QJsonObject osvGetVuln(const QString &vulnId, int timeoutMs)
{
    // Prozent-Kodierung wie urllib.parse.quote ('/' bleibt erhalten).
    const QString encoded =
        QString::fromLatin1(QUrl::toPercentEncoding(vulnId, QByteArrayLiteral("/")));
    return parseObject(fetchBlocking(QUrl(kVulnUrl + encoded), nullptr, timeoutMs));
}

} // namespace ncssh::net
