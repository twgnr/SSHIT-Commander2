// Befehlskatalog mit Erklaerungen + Parameter-Schema fuer den Assistenten.
//
// Jeder CommandSpec hat ein Template mit {name}-Platzhaltern, die der
// Assistent (UI) aus CommandParam fuellt.
// (Port von core/commands.py)
#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <vector>

namespace ncssh::core {

struct CommandParam {
    QString name;                                // Platzhalter im Template, z.B. "path"
    QString label;                               // Anzeige im Assistenten
    QString kind = QStringLiteral("text");       // "text" | "choice" | "flag"
    QString description;
    QString defaultValue;                        // ("default" ist in C++ reserviert)
    QStringList choices;
    QString flagValue;                           // nur kind=="flag": eingefuegter Text wenn aktiv
    bool required = false;
};

struct CommandSpec {
    QString name;                                // Kurzname, z.B. "tar (komprimieren)"
    QString category;
    QString description;
    QString templateText;                        // ("template" ist in C++ reserviert)
    std::vector<CommandParam> params;
    QString example;
    bool danger = false;                         // destruktiv -> in UI markieren
    QString platform = QStringLiteral("posix");  // "posix" | "windows" | "any"

    QString searchText() const;
};

// Stellt sudo (optional als anderer Benutzer) voran. Nur sinnvoll unter posix.
QString wrapPrivilege(const QString &command, bool sudo = false, const QString &user = {});

// Setzt die Werte ins Template ein und normalisiert Whitespace.
QString render(const CommandSpec &spec, const QHash<QString, QString> &values);

// Der vollstaendige Katalog. Bewusst breit gefaechert; leicht erweiterbar.
// Anzeigetexte werden beim ersten Zugriff uebersetzt (wie beim Python-Import).
const std::vector<CommandSpec> &catalog();

// Befehle passend zum OS ("posix" oder "windows"); "any" immer dabei.
std::vector<CommandSpec> commandsFor(const QString &osType);

// Kategorien in Katalog-Reihenfolge (ohne Duplikate).
QStringList categories();

} // namespace ncssh::core
