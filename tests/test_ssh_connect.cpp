// Verbindungsaufbau: Schrittmeldungen und Gesamtfrist. Ein Server, der die
// TCP-Verbindung annimmt, aber nie SSH spricht, darf den Aufbau nicht laenger
// als die Frist blockieren — und der Fehler muss den haengenden Schritt nennen.
#include "tests/harness.hpp"

#include "ncssh/net/ssh.hpp"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QStringList>
#include <QTcpServer>

using namespace ncssh;

TEST(ssh_connect, deadline_aborts_silent_server_and_names_step)
{
    // Listener ohne Event-Loop: der Kernel nimmt die Verbindung im Backlog an,
    // es kommt aber nie ein SSH-Banner.
    QTcpServer server;
    CHECK(server.listen(QHostAddress::LocalHost, 0));

    core::ServerProfile profile;
    profile.host = QStringLiteral("127.0.0.1");
    profile.port = server.serverPort();
    profile.username = QStringLiteral("nobody");
    profile.authMethod = QStringLiteral("password");
    profile.connectTimeout = 20;   // Einzel-Timeout groesser als die Frist

    QStringList steps;
    net::ConnectControl control;
    control.progress = [&steps](const QString &text) { steps << text; };
    control.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);

    QElapsedTimer clock;
    clock.start();
    QString error;
    try {
        net::connectSession(profile, nullptr, control);
    } catch (const std::exception &exc) {
        error = QString::fromUtf8(exc.what());
    }
    CHECK(!error.isEmpty());
    CHECK(clock.elapsed() < 6000);              // Frist eingehalten, nicht 20 s
    CHECK(error.contains(QStringLiteral("bei:")));   // haengender Schritt benannt
    CHECK(steps.size() >= 2);                   // TCP + Handshake gemeldet
}

TEST(ssh_connect, refused_port_reports_reason)
{
    // Freien Port ermitteln und wieder schliessen -> Verbindung wird abgelehnt.
    quint16 port = 0;
    {
        QTcpServer probe;
        CHECK(probe.listen(QHostAddress::LocalHost, 0));
        port = probe.serverPort();
    }
    core::ServerProfile profile;
    profile.host = QStringLiteral("127.0.0.1");
    profile.port = port;
    profile.username = QStringLiteral("nobody");
    profile.connectTimeout = 5;

    QString error;
    try {
        net::connectSession(profile, nullptr, net::ConnectControl{});
    } catch (const std::exception &exc) {
        error = QString::fromUtf8(exc.what());
    }
    CHECK(error.startsWith(QStringLiteral("Verbindung fehlgeschlagen")));
    // Mehr als die nackte Kopfzeile: der Grund steht in einer zweiten Zeile.
    CHECK(error.contains(QLatin1Char('\n')));
}

TEST(ssh_connect, cancelled_attempt_stops_before_network)
{
    core::ServerProfile profile;
    profile.host = QStringLiteral("127.0.0.1");
    profile.port = 22;
    profile.username = QStringLiteral("nobody");

    net::ConnectControl control;
    control.cancelled = std::make_shared<std::atomic_bool>(true);
    QStringList steps;
    control.progress = [&steps](const QString &text) { steps << text; };

    QString error;
    try {
        net::connectSession(profile, nullptr, control);
    } catch (const std::exception &exc) {
        error = QString::fromUtf8(exc.what());
    }
    CHECK(error.startsWith(QStringLiteral("Abgebrochen")));
    CHECK(steps.isEmpty());   // kein Schritt mehr begonnen
}
