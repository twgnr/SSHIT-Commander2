// Tests fuer parseSocks5Target — die Adress-/Port-Bytes einer SOCKS5-CONNECT-
// Anfrage. Ein Fehler hier (ATYP-Verzweigung, Domain-Laenge, big-endian Port)
// wuerde dynamische Tunnel (-D) still zerstoeren; ohne echten Server faellt das
// sonst nicht auf.
#include "tests/harness.hpp"

#include "ncssh/net/tunnels.hpp"

using namespace ncssh::net;

namespace {

// Baut den Adressteil (ab ATYP) so, wie ihn ein SOCKS5-Client sendet.
QByteArray ipv4(quint8 a, quint8 b, quint8 c, quint8 d, quint16 port)
{
    QByteArray out;
    out.append(char(0x01));
    out.append(char(a)).append(char(b)).append(char(c)).append(char(d));
    out.append(char((port >> 8) & 0xff)).append(char(port & 0xff));
    return out;
}

QByteArray domain(const QByteArray &host, quint16 port)
{
    QByteArray out;
    out.append(char(0x03));
    out.append(char(host.size()));
    out.append(host);
    out.append(char((port >> 8) & 0xff)).append(char(port & 0xff));
    return out;
}

} // namespace

TEST(tunnels, socks5_ipv4)
{
    QString host;
    int port = 0;
    CHECK(parseSocks5Target(ipv4(192, 168, 7, 5, 8080), host, port));
    CHECK_EQ(host.toStdString(), std::string("192.168.7.5"));
    CHECK_EQ(port, 8080);
}

TEST(tunnels, socks5_domain)
{
    QString host;
    int port = 0;
    CHECK(parseSocks5Target(domain("example.com", 443), host, port));
    CHECK_EQ(host.toStdString(), std::string("example.com"));
    CHECK_EQ(port, 443);
}

TEST(tunnels, socks5_port_is_big_endian)
{
    // 0x01BB = 443 — pruegt, dass die Byte-Reihenfolge nicht vertauscht ist.
    QString host;
    int port = 0;
    QByteArray req = ipv4(10, 0, 0, 1, 0);
    req[req.size() - 2] = char(0x01);
    req[req.size() - 1] = char(0xBB);
    CHECK(parseSocks5Target(req, host, port));
    CHECK_EQ(port, 443);

    // Hoechster Port 65535 darf nicht negativ/verstuemmelt werden.
    CHECK(parseSocks5Target(ipv4(10, 0, 0, 1, 65535), host, port));
    CHECK_EQ(port, 65535);
}

TEST(tunnels, socks5_ipv6)
{
    QString host;
    int port = 0;
    QByteArray req;
    req.append(char(0x04));
    // 2001:db8::1
    const unsigned char addr[16] = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0,
                                    0,    0,    0,    0,    0, 0, 0, 1};
    req.append(reinterpret_cast<const char *>(addr), 16);
    req.append(char(0x00)).append(char(0x16));  // Port 22
    CHECK(parseSocks5Target(req, host, port));
    CHECK_EQ(host.toStdString(), std::string("2001:db8:0:0:0:0:0:1"));
    CHECK_EQ(port, 22);
}

TEST(tunnels, socks5_unknown_atyp_rejected)
{
    QString host;
    int port = 0;
    QByteArray req;
    req.append(char(0x07));  // ungueltiger Adresstyp
    req.append("xxxx");
    CHECK(!parseSocks5Target(req, host, port));
}

TEST(tunnels, socks5_short_buffers_rejected)
{
    QString host;
    int port = 0;
    // Leer.
    CHECK(!parseSocks5Target(QByteArray(), host, port));
    // IPv4 ohne Port.
    QByteArray shortIp;
    shortIp.append(char(0x01)).append("\x0a\x00\x00\x01", 4);
    CHECK(!parseSocks5Target(shortIp, host, port));
    // Domain-Laenge kuendigt mehr Bytes an, als vorhanden sind.
    QByteArray liar;
    liar.append(char(0x03)).append(char(50)).append("kurz");
    CHECK(!parseSocks5Target(liar, host, port));
    // Domain-ATYP ganz ohne Laengenbyte.
    QByteArray noLen;
    noLen.append(char(0x03));
    CHECK(!parseSocks5Target(noLen, host, port));
}
