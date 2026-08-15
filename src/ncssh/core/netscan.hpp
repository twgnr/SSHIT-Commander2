// Netzwerk-Scanner-Engine (nur Bordmittel, keine externen Abhaengigkeiten).
//
// Host-Discovery per TCP-Connect und/oder OS-ping; MAC ueber die ARP-Tabelle,
// Hostnamen ueber Reverse-DNS, Freigaben ueber "net view" (Windows) bzw.
// "smbclient -L" (Linux). Der eigentliche Scan streamt Treffer ueber Callbacks;
// reine Parser-Funktionen sind ohne Netzwerk testbar.
#pragma once

#include "ncssh/gui/bridge.hpp"  // CancelTokenPtr

#include <QHash>
#include <QString>
#include <QStringList>
#include <functional>
#include <utility>
#include <vector>

namespace ncssh::core {

using ncssh::gui::CancelTokenPtr;

// Kuratierte Port-Voreinstellungen (Anzeigeschluessel -> Portliste).
const QHash<QString, QVector<int>> &portPresets();

// Bekannter Dienstname zu einem Port (sonst die Portnummer als Text).
QString serviceName(int port);

// Portliste aus Text: "22,80,1-1024" (Bereiche und Einzelwerte).
QVector<int> parsePorts(const QString &text, int limit = 4096);

// URL(s) einer moeglichen Weboberflaeche aus offenen Ports.
QStringList webUrls(const QString &ip, const QVector<int> &ports);

// Wandelt eine Eingabe in eine Liste von Ziel-Adressen (CIDR, Bereiche, Listen).
QStringList parseTargets(const QString &text, int limit = 4096);

// "\\host" (ohne Freigabe) -> "host"; sonst leer.
QString bareUncHost(const QString &text);

// Beste lokale IPv4-Adresse (ohne tatsaechlichen Verbindungsaufbau).
QString localIpv4();

// Vorschlag fuer die IP-Range: das lokale /24, falls ermittelbar.
QString defaultRange();

struct ScanOptions {
    QStringList targets;
    QVector<int> ports;      // default: portPresets()["common"]
    bool ping = true;
    bool onlyAlive = true;
    bool resolveNames = true;
    bool detectMac = true;
    bool detectShares = true;
    bool identify = true;    // Banner, Web-Titel, NetBIOS-Name
    double timeout = 0.5;    // Sekunden je TCP-Versuch
    int concurrency = 100;

    ScanOptions() = default;
    explicit ScanOptions(const QStringList &t) : targets(t) {}
};

struct HostResult {
    QString ip;
    QString hostname;
    QString mac;
    QVector<int> openPorts;
    bool alive = false;
    QStringList shares;
    QStringList web;         // erkannte Weboberflaechen-URLs
    double latency = 0.0;    // ms (0 = unbekannt)
    int ttl = 0;             // TTL der Ping-Antwort (0 = unbekannt)
    QString osGuess;         // grobe OS-Schaetzung aus der TTL
    QString vendor;          // Hersteller aus der MAC (OUI)
    QString netbios;         // NetBIOS-Name (Windows)
    QString webTitle;        // <title> der Weboberflaeche
    QStringList banners;     // "port Dienst-Banner"

    bool hasShares() const { return !shares.isEmpty(); }
    bool hasWeb() const { return !web.isEmpty(); }
    QStringList services() const;
};

// Parser (rein, testbar)
QString parseArp(const QString &text, const QString &ip = {});
QStringList parseNetView(const QString &text);
QStringList parseSmbclient(const QString &text);
std::pair<int, double> parsePing(const QString &text);
QString osFromTtl(int ttl);
QString parseNbtstat(const QString &text);
QString parseHtmlTitle(const QString &html);

// Einzelnen Host pruefen (fuer die Adresszeilen-Eingabe \\host).
HostResult scanHost(const QString &host);

// Scan mit Fortschritt/Treffern ueber Callbacks (blockierend, im Worker).
// onProgress(done, total); onHost(HostResult) je (nach onlyAlive) Treffer.
void scanEvents(const ScanOptions &opts,
                const std::function<void(int, int)> &onProgress,
                const std::function<void(const HostResult &)> &onHost,
                const CancelTokenPtr &cancel = {});

// Sendet ein Wake-on-LAN "Magic Packet" an die MAC. True bei Erfolg.
bool wakeOnLan(const QString &mac,
               const QString &broadcast = QStringLiteral("255.255.255.255"),
               int port = 9);

} // namespace ncssh::core
