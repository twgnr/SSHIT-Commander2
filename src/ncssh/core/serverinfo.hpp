// Server-Info: wichtigste Eckdaten eines verbundenen (Linux/Unix-)Servers in
// EINEM SSH-Aufruf — System, Benutzer, Konfigurationsdateien, Ports, Dienste.
//
// serverInfoScript() liefert ein POSIX-sh-Skript (per stdin an "sh -s"), das
// seine Ausgabe in Abschnitte "##SECTION <name>" gliedert; parseServerInfo()
// zerlegt sie wieder. Beides ist rein und ohne Server testbar.
#pragma once

#include <QString>
#include <QStringList>
#include <vector>

namespace ncssh::core {

struct ServerUser {
    QString name;
    int uid = -1;
    int gid = -1;
    QString gecos;      // voller Name / Kommentar
    QString home;
    QString shell;
    bool login = false;   // Shell erlaubt eine Anmeldung (nicht nologin/false)
    bool admin = false;   // root oder Mitglied von sudo/wheel/admin
};

struct ServerConfigFile {
    QString path;
    QString category;     // z. B. "SSH", "Webserver", "Umgebung (.env)"
    bool readable = false;
    bool writable = false;
};

struct ServerInfo {
    QString hostname;
    QString os;
    QString kernel;
    QString uptime;
    QString identity;     // Ausgabe von "id": angemeldet als …
    std::vector<ServerUser> users;
    std::vector<ServerConfigFile> files;
    QString disk;
    QString memory;
    QString ports;
    QString services;
};

// Skript fuer "sh -s" (stdin). Nur Lesebefehle, Fehler werden verschluckt.
QString serverInfoScript();

// Ausgabe des Skripts zerlegen.
ServerInfo parseServerInfo(const QString &output);

// Kategorie einer Konfigurationsdatei anhand ihres Pfads (fuer Gruppierung).
QString configCategory(const QString &path);

} // namespace ncssh::core
