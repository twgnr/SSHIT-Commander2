// Cloud-KI-Anbieter: Anfrage-Bodies, Stream-Ereignisse und der komplette
// Streaming-Weg gegen einen lokalen Schein-Server (kein Netz, kein Schluessel).
#include "tests/harness.hpp"

#include "ncssh/core/ai.hpp"
#include "ncssh/core/settings.hpp"
#include "ncssh/gui/settings_dialog.hpp"
#include "ncssh/net/cloudai.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QTemporaryDir>

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>

using namespace ncssh;

namespace {

QJsonArray sampleMessages()
{
    return QJsonArray{
        QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                    {QStringLiteral("content"), QStringLiteral("Sei knapp.")}},
        QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                    {QStringLiteral("content"), QStringLiteral("Was ist ls?")}},
    };
}

// Antwortet auf die erste Anfrage mit body (HTTP status) und merkt sich die
// Anfrage-Kopfzeilen. Laeuft im Test-Thread — die blockierenden Aufrufe
// drehen dort ihre eigene Event-Schleife, die auch den Server bedient.
class FakeServer : public QObject {
public:
    FakeServer(int status, QByteArray contentType, QByteArray body)
        : m_status(status), m_type(std::move(contentType)), m_body(std::move(body))
    {
        m_server.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&m_server, &QTcpServer::newConnection, this, [this] {
            QTcpSocket *sock = m_server.nextPendingConnection();
            QObject::connect(sock, &QTcpSocket::readyRead, sock, [this, sock] {
                request += sock->readAll();
                if (!request.contains("\r\n\r\n") || answered)
                    return;
                answered = true;
                sock->write("HTTP/1.1 " + QByteArray::number(m_status) + " X\r\n"
                            "Content-Type: " + m_type + "\r\n"
                            "Content-Length: " + QByteArray::number(m_body.size()) + "\r\n"
                            "Connection: close\r\n\r\n" + m_body);
                sock->flush();
                sock->disconnectFromHost();
            });
        });
    }
    QString url() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()); }
    QByteArray request;
    bool answered = false;

private:
    QTcpServer m_server;
    int m_status;
    QByteArray m_type;
    QByteArray m_body;
};

} // namespace

TEST(cloudai, anthropic_request_moves_system_and_enables_fallback)
{
    const QJsonObject body = net::anthropicRequest(QStringLiteral("claude-opus-5-5"), sampleMessages());
    CHECK_EQ(body.value(QStringLiteral("system")).toString(), QStringLiteral("Sei knapp."));
    const QJsonArray msgs = body.value(QStringLiteral("messages")).toArray();
    CHECK_EQ(msgs.size(), 1);   // system steht NICHT in messages
    CHECK_EQ(msgs.first().toObject().value(QStringLiteral("role")).toString(), QStringLiteral("user"));
    CHECK(body.value(QStringLiteral("stream")).toBool());
    CHECK_EQ(body.value(QStringLiteral("fallbacks")).toString(), QStringLiteral("default"));
    // Aeltere Modelle bekommen keine (dort ungueltigen) Zusatzfelder.
    const QJsonObject haiku = net::anthropicRequest(QStringLiteral("claude-haiku-4-5"), sampleMessages());
    CHECK(!haiku.contains(QStringLiteral("fallbacks")));
    CHECK(!haiku.contains(QStringLiteral("output_config")));
}

TEST(cloudai, gemini_request_maps_roles)
{
    QJsonArray messages = sampleMessages();
    messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                                {QStringLiteral("content"), QStringLiteral("Listet Dateien.")}});
    const QJsonObject body = net::geminiRequest(messages);
    const QJsonArray contents = body.value(QStringLiteral("contents")).toArray();
    CHECK_EQ(contents.size(), 2);
    CHECK_EQ(contents.at(1).toObject().value(QStringLiteral("role")).toString(), QStringLiteral("model"));
    CHECK(body.contains(QStringLiteral("systemInstruction")));
}

TEST(cloudai, event_parsing_per_provider)
{
    CHECK_EQ(net::anthropicEventText(QJsonObject{
                 {QStringLiteral("type"), QStringLiteral("content_block_delta")},
                 {QStringLiteral("delta"), QJsonObject{{QStringLiteral("type"), QStringLiteral("text_delta")},
                                                       {QStringLiteral("text"), QStringLiteral("Hal")}}}}),
             QStringLiteral("Hal"));
    // Denk-Bloecke werden nicht angezeigt.
    CHECK(net::anthropicEventText(QJsonObject{
              {QStringLiteral("type"), QStringLiteral("content_block_delta")},
              {QStringLiteral("delta"), QJsonObject{{QStringLiteral("type"), QStringLiteral("thinking_delta")},
                                                    {QStringLiteral("thinking"), QStringLiteral("x")}}}})
              .isEmpty());
    CHECK_THROWS(net::anthropicEventText(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("message_delta")},
        {QStringLiteral("delta"), QJsonObject{{QStringLiteral("stop_reason"), QStringLiteral("refusal")}}}}));
    CHECK_EQ(net::openAiEventText(QJsonObject{
                 {QStringLiteral("choices"),
                  QJsonArray{QJsonObject{{QStringLiteral("delta"),
                                          QJsonObject{{QStringLiteral("content"), QStringLiteral("lo")}}}}}}}),
             QStringLiteral("lo"));
    CHECK_EQ(net::geminiEventText(QJsonObject{
                 {QStringLiteral("candidates"),
                  QJsonArray{QJsonObject{{QStringLiteral("content"),
                                          QJsonObject{{QStringLiteral("parts"),
                                                       QJsonArray{QJsonObject{{QStringLiteral("text"),
                                                                               QStringLiteral("!")}}}}}}}}}}),
             QStringLiteral("!"));
}

TEST(cloudai, anthropic_stream_end_to_end_against_fake_server)
{
    const QByteArray sse =
        "event: message_start\ndata: {\"type\":\"message_start\",\"message\":{}}\n\n"
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":0,"
        "\"delta\":{\"type\":\"text_delta\",\"text\":\"Hallo \"}}\n\n"
        "event: ping\ndata: {\"type\":\"ping\"}\n\n"
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":0,"
        "\"delta\":{\"type\":\"text_delta\",\"text\":\"Welt\"}}\n\n"
        "event: message_stop\ndata: {\"type\":\"message_stop\"}\n\n";
    FakeServer server(200, "text/event-stream", sse);
    QString text;
    net::cloudChat(net::CloudTarget{net::kProviderAnthropic, server.url(),
                                    QStringLiteral("claude-opus-5-5"), QStringLiteral("sk-test")},
                   sampleMessages(), [&text](const QString &t) { text += t; });
    CHECK_EQ(text, QStringLiteral("Hallo Welt"));
    const QByteArray req = server.request.toLower();
    CHECK(req.contains("x-api-key: sk-test"));
    CHECK(req.contains("anthropic-version: 2023-06-01"));
    CHECK(req.contains("anthropic-beta: server-side-fallback-2026-07-01"));
    CHECK(server.request.startsWith("POST /v1/messages"));
}

TEST(cloudai, http_error_carries_provider_message)
{
    FakeServer server(401, "application/json",
                      "{\"type\":\"error\",\"error\":{\"type\":\"authentication_error\","
                      "\"message\":\"invalid x-api-key\"}}");
    QString error;
    try {
        net::cloudChat(net::CloudTarget{net::kProviderAnthropic, server.url(),
                                        QStringLiteral("claude-opus-5-5"), QStringLiteral("falsch")},
                       sampleMessages(), {});
    } catch (const std::exception &e) {
        error = QString::fromUtf8(e.what());
    }
    CHECK(error.contains(QStringLiteral("401")));
    CHECK(error.contains(QStringLiteral("invalid x-api-key")));
    CHECK(error.contains(QStringLiteral("Schlüssel")));
}

TEST(cloudai, openai_compatible_stream_and_models)
{
    const QByteArray sse =
        "data: {\"choices\":[{\"delta\":{\"content\":\"O\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"K\"}}]}\n\n"
        "data: [DONE]\n\n";
    FakeServer chat(200, "text/event-stream", sse);
    QString text;
    // OpenAI-kompatibel: Schluessel optional (lokale Server wie LM Studio).
    net::cloudChat(net::CloudTarget{net::kProviderOpenAiCompat, chat.url() + QStringLiteral("/v1"),
                                    QStringLiteral("local-model"), QString()},
                   sampleMessages(), [&text](const QString &t) { text += t; });
    CHECK_EQ(text, QStringLiteral("OK"));
    CHECK(chat.request.startsWith("POST /v1/chat/completions"));

    FakeServer models(200, "application/json",
                      "{\"data\":[{\"id\":\"b-model\"},{\"id\":\"a-model\"}]}");
    const QStringList ids = net::cloudListModels(
        net::CloudTarget{net::kProviderOpenAiCompat, models.url() + QStringLiteral("/v1"), {}, {}});
    CHECK_EQ(ids.join(QLatin1Char(',')), QStringLiteral("a-model,b-model"));
}

TEST(cloudai, missing_key_is_reported_before_sending)
{
    QString error;
    try {
        net::cloudChat(net::CloudTarget{net::kProviderGemini, {}, QStringLiteral("gemini-x"), {}},
                       sampleMessages(), {});
    } catch (const std::exception &e) {
        error = QString::fromUtf8(e.what());
    }
    CHECK(error.contains(QStringLiteral("API-Schlüssel")));
}

TEST(cloudai, settings_switch_provider_and_save)
{
    // Einstellungen in ein frisches Verzeichnis umlenken.
    const QByteArray oldAppData = qgetenv("APPDATA");
    QTemporaryDir cfg;
    CHECK(cfg.isValid());
    qputenv("APPDATA", cfg.path().toLocal8Bit());
    {
        gui::SettingsDialog dlg(nullptr, nullptr);
        QComboBox *provider = nullptr;
        for (QComboBox *box : dlg.findChildren<QComboBox *>())
            if (box->findData(net::kProviderAnthropic) >= 0)
                provider = box;
        CHECK(provider != nullptr);
        if (provider) {
            provider->setCurrentIndex(provider->findData(net::kProviderAnthropic));
            auto *buttons = dlg.findChild<QDialogButtonBox *>();
            CHECK(buttons != nullptr);
            if (buttons)
                buttons->button(QDialogButtonBox::Save)->click();
            CHECK_EQ(core::aiProvider(), net::kProviderAnthropic);
            CHECK_EQ(core::aiModel(), QStringLiteral("claude-opus-5-5"));   // Vorbelegung
            CHECK(core::isCloudProvider(core::aiProvider()));
        }
    }
    qputenv("APPDATA", oldAppData);
}
