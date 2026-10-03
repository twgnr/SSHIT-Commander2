// Parameter-Hilfe fuer die Konsole: erkennt den Befehl einer (noch nicht
// ausgefuehrten) Befehlszeile, liefert seine Optionen (aus dem Befehlskatalog
// und der "--help"-Ausgabe des Servers) und setzt Argumente in der vom
// Befehl erwarteten Reihenfolge zusammen (z. B. cp QUELLE ZIEL).
//
// Alles rein und ohne Server testbar; das Holen der Hilfe macht die GUI.
#pragma once

#include "ncssh/core/commands.hpp"

#include <QHash>
#include <QString>
#include <QStringList>
#include <vector>

namespace ncssh::core {

struct CommandOption {
    QString shortFlag;      // z. B. "-n"
    QString longFlag;       // z. B. "--lines"
    QString arg;            // Wert-Platzhalter, z. B. "NUM" (leer = Schalter)
    bool argWithEquals = false;   // "--lines=NUM" statt "--lines NUM"
    QString description;
    QString literal;        // Katalog-Flag: fertiger Text (z. B. "-r"), sonst leer

    QString display() const;                       // "-n, --lines=NUM"
    // Einzufuegender Text; value fuellt ein erwartetes Argument.
    QString insertText(const QString &value = {}) const;
};

// Zeile in Woerter zerlegen und fuehrende Praefixe (sudo [-u user],
// VAR=wert, nohup, time) ueberspringen: erstes Element ist der Befehl.
QStringList commandTokens(const QString &line);

// Befehlsname der Zeile (Pfad abgeschnitten, unter Windows kleingeschrieben).
QString commandName(const QString &line, const QString &osType);

// Kennt die Konsole den Befehl (Katalog bzw. gaengige Unix-Werkzeuge)?
bool isKnownCommand(const QString &name, const QString &osType);

// Katalog-Varianten passend zur Zeile ("git commit" -> nur die commit-Variante,
// "tar" -> packen und entpacken).
std::vector<CommandSpec> specsForLine(const QString &line, const QString &osType);

// Optionen aus den Schalter-Parametern eines Katalog-Eintrags.
std::vector<CommandOption> optionsFromSpec(const CommandSpec &spec);

// Positionsparameter (Text/Auswahl) eines Eintrags in Template-Reihenfolge.
std::vector<CommandParam> positionalParams(const CommandSpec &spec);

// Text, der an die getippte Zeile angefuegt wird: Template mit den Werten der
// Positionsparameter, ohne die schon getippten Befehlswoerter.
// Beispiel: "cp -r" + {src: a, dst: "b c"} -> "a \"b c\"".
QString positionalAppend(const CommandSpec &spec, const QHash<QString, QString> &values,
                         const QString &typedLine);

// Shell-Befehl, der die Hilfe des Befehls liefert (POSIX), oder leer, wenn
// der Befehl dafuer nicht sicher aufgerufen werden kann.
QString helpCommandFor(const QString &line);

// Optionen aus einer --help/-h-Ausgabe (GNU, BusyBox, git, docker …).
std::vector<CommandOption> parseHelpOptions(const QString &helpText);

} // namespace ncssh::core
