// Sicherheits-Audit: Parser fuer OS-/Paketdaten (rein, ohne I/O) plus
// Online-CVE-Abgleich ueber OSV.dev.
//
// Die Parser sind ohne Netzwerk testbar; osvQuerybatch/osvGetVuln fragen die
// freie OSV-API (ohne API-Key) blockierend ab und laufen ueber die AsyncBridge
// auf einem Worker-Thread.
#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace ncssh::core {

// Kernkomponenten, die gezielt gegen die CVE-DB (OSV) geprueft werden.
const QStringList &keyPackages();

// Ergebnis eines apt-Upgrade-Eintrags: (paket, neue_version, ist_security).
struct AptUpgrade {
    QString package;
    QString newVersion;
    bool security = false;
};

// dnf-CVE-Eintrag: (cve, schwere, paket).
struct DnfCve {
    QString cve;
    QString severity;
    QString package;
};

// Befund aus sshd_config: (direktive, wert, schwere, detail).
struct SshdFinding {
    QString directive;
    QString value;
    QString severity;
    QString detail;
};

// Lauschender Socket aus `ss -tuln`: (proto, host, port).
struct ListeningSocket {
    QString proto;
    QString host;
    QString port;
};

// Oeffentlich erreichbarer Socket: (proto, port).
struct PublicPort {
    QString proto;
    QString port;
};

// Konten-Befund: (konto, wert, schwere, detail).
struct AccountFinding {
    QString account;
    QString value;
    QString severity;
    QString detail;
};

// Auf die UI reduzierte OSV-Vuln-Antwort.
struct VulnSummary {
    QString id;
    QString severity;
    QString summary;
    QString url;
};

QHash<QString, QString> parseOsRelease(const QString &text);
std::vector<AptUpgrade> parseAptUpgrade(const QString &text);
std::vector<DnfCve> parseDnfCves(const QString &text);

// Globale Direktiven aus sshd_config (Schluessel klein, letzter Wert gewinnt).
QHash<QString, QString> parseSshdConfig(const QString &text);
// Bewertet sshd-Direktiven.
std::vector<SshdFinding> auditSshd(const QHash<QString, QString> &cfg);

// `ufw status`: "active" | "inactive" | nullopt (z.B. nicht installiert).
std::optional<QString> parseUfwStatus(const QString &text);

std::vector<ListeningSocket> parseListening(const QString &text);
std::vector<PublicPort> publicPorts(const std::vector<ListeningSocket> &listening);

// UID-0-Konten (ausser root) und Konten ohne Passwort.
std::vector<AccountFinding> auditAccounts(const QString &passwd, const QString &shadow);

// True, wenn 20auto-upgrades Unattended-Upgrade aktiviert ("1").
bool unattendedEnabled(const QString &text);
// Zertifikatspfade, die als ablaufend/abgelaufen markiert wurden (Zeilen "SOON <pfad>").
QStringList parseCertCheck(const QString &text);

// Reduziert eine OSV-Vuln-Antwort auf das, was die UI anzeigt.
VulnSummary summarizeVuln(const QJsonObject &vuln);

// Mappt os-release auf eine OSV-Ecosystem-Kennung (best effort); nullopt = unbekannt.
std::optional<QString> osvEcosystem(const QString &osId, const QString &versionId);

// --- Online-CVE-Abgleich ueber OSV.dev (blockierend, im Worker aufrufen) -----
// queries: [{"package":{"name","ecosystem"},"version"}]. Liefert OSV-Antwort.
QJsonObject osvQuerybatch(const QJsonArray &queries, int timeoutSec = 20);
// Vollstaendige Details zu einer OSV-/CVE-ID (Beschreibung, Schwere, Fix).
QJsonObject osvGetVuln(const QString &vulnId, int timeoutSec = 20);

} // namespace ncssh::core
