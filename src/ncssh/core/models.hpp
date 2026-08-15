// Reine Datenmodelle — keine Logik, keine I/O.
#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVariantMap>
#include <optional>
#include <vector>

namespace ncssh::core {

enum class EntryType { Dir, File, Symlink, Parent /* die ".." Zeile */ };

struct FileEntry {
    QString name;
    EntryType type = EntryType::File;
    qint64 size = 0;
    QDateTime modified;                // ungueltig = unbekannt
    QDateTime created;                 // Erstelldatum (sofern verfuegbar)
    QDateTime accessed;                // letzter Zugriff (sofern verfuegbar)
    quint32 permissions = 0;           // st_mode (volle Bits)
    QString owner;
    QString group;
    QString linkTarget;
    // Versteckt? POSIX: fuehrender Punkt; Windows: Versteckt-/System-Attribut.
    bool hidden = false;
    // Optionale Zusatzdaten (z.B. Host-Metadaten im Netzwerkscanner-Modus).
    QVariantMap extra;

    bool isDir() const { return type == EntryType::Dir || type == EntryType::Parent; }

    // Rechte wie "rwxr-xr-x" (ohne Typ-Bit).
    QString permString() const;
    QString permOctal() const;
};

// Port-Weiterleitung. kind: "local" (-L), "remote" (-R), "dynamic" (-D/SOCKS).
struct TunnelSpec {
    QString kind = QStringLiteral("local");
    QString listenHost = QStringLiteral("127.0.0.1");
    int listenPort = 0;
    QString destHost;
    int destPort = 0;

    QJsonObject toJson() const;
    static TunnelSpec fromJson(const QJsonObject &data);
    QString label() const;
};

// Gespeicherte Server-Verbindung. Wird als JSON persistiert.
// Sicherheit: Passwort/Passphrase werden NIE im Klartext gespeichert, sondern
// bei savePassword im OS-Keyring (siehe core/secrets).
struct ServerProfile {
    QString name;
    QString host;
    int port = 22;
    QString username;
    QString authMethod = QStringLiteral("key");  // "key" | "password" | "agent"
    QString keyPath;
    QString passphrase;                          // Passphrase fuer den Key (nur zur Laufzeit)
    bool savePassword = false;
    QString password;                            // nur zur Laufzeit
    QString knownHostsPolicy = QStringLiteral("accept-new");  // "accept-new" | "strict" | "ignore"
    QString startPath = QStringLiteral(".");
    std::vector<TunnelSpec> tunnels;             // Auto-Start-Tunnel
    QString color;                               // Tab-Farbe (Hex), optional
    QString proxyJump;                           // ProxyJump (user@host[:port]), optional
    QString lastConnected;                       // ISO-Zeitstempel der letzten Verbindung
    // --- Verbindungs-Feinsteuerung (leer/0 = Standard) ---
    int keepaliveSeconds = 30;                   // libssh2-Keepalive-Intervall (0 = aus)
    int connectTimeout = 20;                     // Handshake-Timeout in Sekunden
    bool compression = false;                    // SSH-Kompression aushandeln
    QString ciphers;                             // bevorzugte Chiffren (kommagetrennt)
    QString kexAlgorithms;                        // bevorzugte Schluesseltausch-Verfahren
    bool agentForwarding = false;                // SSH-Agent an den Server weiterreichen

    QJsonObject toJson() const;
    static ServerProfile fromJson(const QJsonObject &data);
    QString display() const;
};

// Ergebnis eines ausgefuehrten Befehls.
struct CommandResult {
    QString command;
    int exitStatus = 0;
    QString output;
    QString error;
    QString cwd;
};

} // namespace ncssh::core
