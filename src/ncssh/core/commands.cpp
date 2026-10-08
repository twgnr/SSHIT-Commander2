// Befehlskatalog mit Erklaerungen + Parameter-Schema fuer den Assistenten.
#include "ncssh/core/commands.hpp"

#include "ncssh/core/i18n.hpp"

#include <QRegularExpression>
#include <mutex>

namespace ncssh::core {

QString CommandSpec::searchText() const
{
    return QStringLiteral("%1 %2 %3 %4").arg(name, category, description, templateText).toLower();
}

QString wrapPrivilege(const QString &command, bool sudo, const QString &user)
{
    if (!sudo)
        return command;
    const QString u = user.trimmed();
    if (!u.isEmpty())
        return QStringLiteral("sudo -u %1 %2").arg(u, command);
    return QStringLiteral("sudo %1").arg(command);
}

QString render(const CommandSpec &spec, const QHash<QString, QString> &values)
{
    QHash<QString, QString> tokens;
    for (const CommandParam &p : spec.params) {
        const QString v = values.value(p.name, p.defaultValue);
        if (p.kind == QLatin1String("flag")) {
            const QString lv = v.toLower();
            const bool on = (lv == QLatin1String("1") || lv == QLatin1String("true")
                             || lv == QLatin1String("on") || lv == QLatin1String("yes"));
            tokens.insert(p.name, on ? p.flagValue : QString());
        } else if (!p.flagValue.isEmpty()) {
            // Wert mit eigenem Schalter ("-p {pid}"): leer -> beides weg.
            tokens.insert(p.name, v.trimmed().isEmpty() ? QString() : p.flagValue + QLatin1Char(' ') + v);
        } else {
            tokens.insert(p.name, v);
        }
    }
    // Einmaliger Durchlauf wie str.format(); bei unbekanntem Platzhalter
    // bleibt das Template unveraendert (entspricht dem KeyError-Fallback).
    static const QRegularExpression rePlaceholder(QStringLiteral("\\{([^{}]*)\\}"));
    QString out;
    int last = 0;
    bool ok = true;
    auto it = rePlaceholder.globalMatch(spec.templateText);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString key = m.captured(1);
        if (!tokens.contains(key)) {
            ok = false;
            break;
        }
        out += spec.templateText.mid(last, m.capturedStart() - last);
        out += tokens.value(key);
        last = m.capturedEnd();
    }
    if (ok)
        out += spec.templateText.mid(last);
    else
        out = spec.templateText;
    return out.simplified();
}

// ---------------------------------------------------------------------------
// Katalog. Bewusst breit gefaechert; leicht erweiterbar.
// ---------------------------------------------------------------------------
namespace {

using CP = CommandParam;
using CS = CommandSpec;

// --- Dateien / Suche / Archive / Rechte / System / Netzwerk / Git / Docker --
void addPosixBase(std::vector<CS> &c)
{
    c.push_back(CS{
        .name = _t("ls — Verzeichnis auflisten"), .category = _t("Dateien"),
        .description = _t("Listet Dateien; -l lang, -a versteckte, -h lesbare Größen."),
        .templateText = "ls {long} {all} {human} {path}",
        .params = {
            CP{.name = "long", .label = _t("Langformat (-l)"), .kind = "flag", .defaultValue = "on", .flagValue = "-l"},
            CP{.name = "all", .label = _t("Versteckte (-a)"), .kind = "flag", .flagValue = "-a"},
            CP{.name = "human", .label = _t("Lesbar (-h)"), .kind = "flag", .defaultValue = "on", .flagValue = "-h"},
            CP{.name = "path", .label = _t("Pfad"), .description = _t("Leer = aktuelles Verzeichnis")},
        },
        .example = "ls -l -h"});
    c.push_back(CS{
        .name = _t("cp — kopieren"), .category = _t("Dateien"),
        .description = _t("Kopiert Datei/Verzeichnis. -r für Verzeichnisse, -p erhält Attribute."),
        .templateText = "cp {recursive} {preserve} {src} {dst}",
        .params = {
            CP{.name = "recursive", .label = _t("Rekursiv (-r)"), .kind = "flag", .flagValue = "-r"},
            CP{.name = "preserve", .label = _t("Attribute erhalten (-p)"), .kind = "flag", .flagValue = "-p"},
            CP{.name = "src", .label = _t("Quelle"), .required = true},
            CP{.name = "dst", .label = _t("Ziel"), .required = true},
        },
        .example = "cp -r ./src ./backup"});
    c.push_back(CS{
        .name = _t("mv — verschieben/umbenennen"), .category = _t("Dateien"),
        .description = _t("Verschiebt oder benennt um."),
        .templateText = "mv {src} {dst}",
        .params = {
            CP{.name = "src", .label = _t("Quelle"), .required = true},
            CP{.name = "dst", .label = _t("Ziel"), .required = true},
        },
        .example = "mv alt.txt neu.txt"});
    c.push_back(CS{
        .name = _t("rm — löschen"), .category = _t("Dateien"),
        .description = _t("Löscht Dateien. -r rekursiv, -f erzwingt. VORSICHT!"),
        .templateText = "rm {recursive} {force} {path}",
        .params = {
            CP{.name = "recursive", .label = _t("Rekursiv (-r)"), .kind = "flag", .flagValue = "-r"},
            CP{.name = "force", .label = _t("Erzwingen (-f)"), .kind = "flag", .flagValue = "-f"},
            CP{.name = "path", .label = _t("Pfad"), .required = true},
        },
        .example = "rm -rf ./tmp", .danger = true});
    c.push_back(CS{
        .name = _t("mkdir — Verzeichnis anlegen"), .category = _t("Dateien"),
        .description = _t("Erstellt Verzeichnis; -p auch verschachtelt."),
        .templateText = "mkdir {parents} {path}",
        .params = {
            CP{.name = "parents", .label = _t("Mit Eltern (-p)"), .kind = "flag", .defaultValue = "on", .flagValue = "-p"},
            CP{.name = "path", .label = _t("Pfad"), .required = true},
        },
        .example = "mkdir -p a/b/c"});
    c.push_back(CS{
        .name = _t("du — Größe ermitteln"), .category = _t("Dateien"),
        .description = _t("Verzeichnisgrößen; -s Summe, -h lesbar."),
        .templateText = "du {summary} {human} {path}",
        .params = {
            CP{.name = "summary", .label = _t("Nur Summe (-s)"), .kind = "flag", .defaultValue = "on", .flagValue = "-s"},
            CP{.name = "human", .label = _t("Lesbar (-h)"), .kind = "flag", .defaultValue = "on", .flagValue = "-h"},
            CP{.name = "path", .label = _t("Pfad"), .defaultValue = "."},
        },
        .example = "du -s -h ."});

    // --- Suche -------------------------------------------------------------
    c.push_back(CS{
        .name = _t("find — Dateien suchen"), .category = _t("Suche"),
        .description = _t("Sucht im Baum nach Name/Typ."),
        .templateText = "find {path} -name {pattern} {type}",
        .params = {
            CP{.name = "path", .label = _t("Startpfad"), .defaultValue = "."},
            CP{.name = "pattern", .label = _t("Namensmuster"), .defaultValue = "\"*.log\"", .required = true},
            CP{.name = "type", .label = _t("Typ"), .kind = "choice", .choices = {"", "-type f", "-type d"}},
        },
        .example = "find . -name \"*.log\" -type f"});
    c.push_back(CS{
        .name = _t("grep — im Inhalt suchen"), .category = _t("Suche"),
        .description = _t("Durchsucht Dateien nach Muster. -r rekursiv, -i ignoriert Groß/Klein, -n Zeilennr."),
        .templateText = "grep {recursive} {ignorecase} {linenum} {pattern} {path}",
        .params = {
            CP{.name = "recursive", .label = _t("Rekursiv (-r)"), .kind = "flag", .defaultValue = "on", .flagValue = "-r"},
            CP{.name = "ignorecase", .label = _t("Groß/klein egal (-i)"), .kind = "flag", .flagValue = "-i"},
            CP{.name = "linenum", .label = _t("Zeilennummern (-n)"), .kind = "flag", .defaultValue = "on", .flagValue = "-n"},
            CP{.name = "pattern", .label = _t("Suchmuster"), .required = true},
            CP{.name = "path", .label = _t("Pfad"), .defaultValue = "."},
        },
        .example = "grep -r -n \"TODO\" ."});

    // --- Archive -----------------------------------------------------------
    c.push_back(CS{
        .name = _t("tar — komprimieren"), .category = _t("Archive"),
        .description = _t("Erstellt ein .tar.gz-Archiv."),
        .templateText = "tar -czf {archive} {target}",
        .params = {
            CP{.name = "archive", .label = _t("Archivname"), .defaultValue = "archiv.tar.gz", .required = true},
            CP{.name = "target", .label = _t("Quelle"), .required = true},
        },
        .example = "tar -czf backup.tar.gz ./projekt"});
    c.push_back(CS{
        .name = _t("tar — entpacken"), .category = _t("Archive"),
        .description = _t("Entpackt ein .tar.gz-Archiv."),
        .templateText = "tar -xzf {archive} {dest}",
        .params = {
            CP{.name = "archive", .label = _t("Archiv"), .required = true},
            CP{.name = "dest", .label = _t("Zielordner (-C ...)")},
        },
        .example = "tar -xzf backup.tar.gz"});

    // --- Rechte ------------------------------------------------------------
    c.push_back(CS{
        .name = _t("chmod — Rechte setzen"), .category = _t("Rechte"),
        .description = _t("Setzt Dateirechte (oktal, z.B. 644/755). -R rekursiv."),
        .templateText = "chmod {recursive} {mode} {path}",
        .params = {
            CP{.name = "recursive", .label = _t("Rekursiv (-R)"), .kind = "flag", .flagValue = "-R"},
            CP{.name = "mode", .label = _t("Modus"), .kind = "choice", .choices = {"644", "664", "755", "775", "600", "700"}, .required = true},
            CP{.name = "path", .label = _t("Pfad"), .required = true},
        },
        .example = "chmod 755 script.sh"});
    c.push_back(CS{
        .name = _t("chown — Besitzer ändern"), .category = _t("Rechte"),
        .description = _t("Ändert Eigentümer/Gruppe. -R rekursiv."),
        .templateText = "chown {recursive} {owner} {path}",
        .params = {
            CP{.name = "recursive", .label = _t("Rekursiv (-R)"), .kind = "flag", .flagValue = "-R"},
            CP{.name = "owner", .label = _t("user:gruppe"), .required = true},
            CP{.name = "path", .label = _t("Pfad"), .required = true},
        },
        .example = "chown -R www-data:www-data /var/www"});

    // --- Prozesse / System -------------------------------------------------
    c.push_back(CS{
        .name = _t("ps — Prozesse"), .category = _t("System"),
        .description = _t("Zeigt laufende Prozesse."),
        .templateText = "ps aux {filter}",
        .params = {CP{.name = "filter", .label = _t("Filter (| grep ...)")}},
        .example = "ps aux | grep nginx"});
    c.push_back(CS{
        .name = _t("kill — Prozess beenden"), .category = _t("System"),
        .description = _t("Sendet ein Signal an eine PID. -9 erzwingt."),
        .templateText = "kill {force} {pid}",
        .params = {
            CP{.name = "force", .label = _t("SIGKILL (-9)"), .kind = "flag", .flagValue = "-9"},
            CP{.name = "pid", .label = _t("PID"), .required = true},
        },
        .example = "kill -9 12345", .danger = true});
    c.push_back(CS{
        .name = _t("df — Speicherplatz"), .category = _t("System"),
        .description = _t("Zeigt Plattenbelegung; -h lesbar."),
        .templateText = "df {human} {path}",
        .params = {
            CP{.name = "human", .label = _t("Lesbar (-h)"), .kind = "flag", .defaultValue = "on", .flagValue = "-h"},
            CP{.name = "path", .label = _t("Pfad")},
        },
        .example = "df -h"});
    c.push_back(CS{
        .name = _t("systemctl — Dienst steuern"), .category = _t("System"),
        .description = _t("Steuert systemd-Dienste."),
        .templateText = "systemctl {action} {service}",
        .params = {
            CP{.name = "action", .label = _t("Aktion"), .kind = "choice", .choices = {"status", "start", "stop", "restart", "enable", "disable"}, .required = true},
            CP{.name = "service", .label = _t("Dienst"), .required = true},
        },
        .example = "systemctl restart nginx"});

    // --- Netzwerk ----------------------------------------------------------
    c.push_back(CS{
        .name = _t("curl — HTTP-Request"), .category = _t("Netzwerk"),
        .description = _t("HTTP-Aufruf. -I nur Header, -L folgt Redirects, -o speichert."),
        .templateText = "curl {head} {follow} {url} {output}",
        .params = {
            CP{.name = "head", .label = _t("Nur Header (-I)"), .kind = "flag", .flagValue = "-I"},
            CP{.name = "follow", .label = _t("Redirects folgen (-L)"), .kind = "flag", .defaultValue = "on", .flagValue = "-L"},
            CP{.name = "url", .label = _t("URL"), .required = true},
            CP{.name = "output", .label = _t("Speichern (-o datei)")},
        },
        .example = "curl -L https://example.com"});
    c.push_back(CS{
        .name = _t("ss — offene Ports"), .category = _t("Netzwerk"),
        .description = _t("Zeigt lauschende Sockets (-tulpn)."),
        .templateText = "ss -tulpn {filter}",
        .params = {CP{.name = "filter", .label = _t("Filter (| grep ...)")}},
        .example = "ss -tulpn | grep :443"});

    // --- Git ---------------------------------------------------------------
    c.push_back(CS{
        .name = _t("git status"), .category = _t("Git"),
        .description = _t("Zeigt den Arbeitsbaum-Status."),
        .templateText = "git status {short}",
        .params = {CP{.name = "short", .label = _t("Kurz (-s)"), .kind = "flag", .flagValue = "-s"}},
        .example = "git status -s", .platform = "any"});
    c.push_back(CS{
        .name = _t("git log"), .category = _t("Git"),
        .description = _t("Zeigt die Commit-Historie."),
        .templateText = "git log {oneline} {graph} -n {count}",
        .params = {
            CP{.name = "oneline", .label = _t("Einzeilig"), .kind = "flag", .defaultValue = "on", .flagValue = "--oneline"},
            CP{.name = "graph", .label = _t("Graph"), .kind = "flag", .flagValue = "--graph"},
            CP{.name = "count", .label = _t("Anzahl"), .defaultValue = "20"},
        },
        .example = "git log --oneline -n 20", .platform = "any"});

    // --- Docker ------------------------------------------------------------
    c.push_back(CS{
        .name = _t("docker ps"), .category = _t("Docker"),
        .description = _t("Listet Container; -a auch gestoppte."),
        .templateText = "docker ps {all}",
        .params = {CP{.name = "all", .label = _t("Alle (-a)"), .kind = "flag", .flagValue = "-a"}},
        .example = "docker ps -a", .platform = "any"});
    c.push_back(CS{
        .name = _t("docker logs"), .category = _t("Docker"),
        .description = _t("Zeigt Container-Logs; -f folgt, --tail begrenzt."),
        .templateText = "docker logs {follow} --tail {tail} {container}",
        .params = {
            CP{.name = "follow", .label = _t("Folgen (-f)"), .kind = "flag", .flagValue = "-f"},
            CP{.name = "tail", .label = _t("Letzte N Zeilen"), .defaultValue = "100"},
            CP{.name = "container", .label = _t("Container"), .required = true},
        },
        .example = "docker logs --tail 100 web", .platform = "any"});
}

// --- Dateien (weitere) / Text ----------------------------------------------
void addPosixFilesText(std::vector<CS> &c)
{
    c.push_back(CS{
        .name = _t("cat — Datei ausgeben"), .category = _t("Dateien"),
        .description = _t("Gibt Dateiinhalt aus; -n mit Zeilennummern."),
        .templateText = "cat {number} {path}",
        .params = {
            CP{.name = "number", .label = _t("Zeilennummern (-n)"), .kind = "flag", .flagValue = "-n"},
            CP{.name = "path", .label = _t("Datei"), .required = true},
        },
        .example = "cat -n datei.txt"});
    c.push_back(CS{
        .name = _t("head — Anfang anzeigen"), .category = _t("Dateien"),
        .description = _t("Erste N Zeilen einer Datei."),
        .templateText = "head -n {lines} {path}",
        .params = {
            CP{.name = "lines", .label = _t("Zeilen"), .defaultValue = "20"},
            CP{.name = "path", .label = _t("Datei"), .required = true},
        },
        .example = "head -n 20 app.log"});
    c.push_back(CS{
        .name = _t("tail — Ende / live folgen"), .category = _t("Dateien"),
        .description = _t("Letzte N Zeilen; -f folgt live."),
        .templateText = "tail {follow} -n {lines} {path}",
        .params = {
            CP{.name = "follow", .label = _t("Folgen (-f)"), .kind = "flag", .flagValue = "-f"},
            CP{.name = "lines", .label = _t("Zeilen"), .defaultValue = "50"},
            CP{.name = "path", .label = _t("Datei"), .required = true},
        },
        .example = "tail -f -n 50 app.log"});
    c.push_back(CS{
        .name = _t("touch — Datei anlegen"), .category = _t("Dateien"),
        .description = _t("Legt leere Datei an / aktualisiert Zeitstempel."),
        .templateText = "touch {path}",
        .params = {CP{.name = "path", .label = _t("Datei"), .required = true}},
        .example = "touch neu.txt"});
    c.push_back(CS{
        .name = _t("ln — Symlink"), .category = _t("Dateien"),
        .description = _t("Erstellt einen symbolischen Link (-s)."),
        .templateText = "ln -s {target} {link}",
        .params = {
            CP{.name = "target", .label = _t("Ziel"), .required = true},
            CP{.name = "link", .label = _t("Linkname"), .required = true},
        },
        .example = "ln -s /opt/app app"});
    c.push_back(CS{
        .name = _t("stat — Datei-Infos"), .category = _t("Dateien"),
        .description = _t("Metadaten: Größe, Rechte, Zeiten."),
        .templateText = "stat {path}",
        .params = {CP{.name = "path", .label = _t("Pfad"), .required = true}},
        .example = "stat datei"});
    c.push_back(CS{
        .name = _t("tree — Baumansicht"), .category = _t("Dateien"),
        .description = _t("Verzeichnisbaum; -L begrenzt die Tiefe."),
        .templateText = "tree -L {depth} {path}",
        .params = {
            CP{.name = "depth", .label = _t("Tiefe"), .defaultValue = "2"},
            CP{.name = "path", .label = _t("Pfad"), .defaultValue = "."},
        },
        .example = "tree -L 2"});
    c.push_back(CS{
        .name = _t("wc — zählen"), .category = _t("Dateien"),
        .description = _t("Zählt Zeilen (-l), Wörter (-w), Bytes (-c)."),
        .templateText = "wc {lines} {words} {bytes} {path}",
        .params = {
            CP{.name = "lines", .label = _t("Zeilen (-l)"), .kind = "flag", .defaultValue = "on", .flagValue = "-l"},
            CP{.name = "words", .label = _t("Wörter (-w)"), .kind = "flag", .flagValue = "-w"},
            CP{.name = "bytes", .label = _t("Bytes (-c)"), .kind = "flag", .flagValue = "-c"},
            CP{.name = "path", .label = _t("Datei"), .required = true},
        },
        .example = "wc -l datei"});
    c.push_back(CS{
        .name = _t("rsync — synchronisieren"), .category = _t("Dateien"),
        .description = _t("Effizient kopieren/sync; -a Archiv, -z komprimiert, --delete spiegelt."),
        .templateText = "rsync -avz {delete} {src} {dst}",
        .params = {
            CP{.name = "delete", .label = _t("Spiegeln (--delete)"), .kind = "flag", .flagValue = "--delete"},
            CP{.name = "src", .label = _t("Quelle"), .required = true},
            CP{.name = "dst", .label = _t("Ziel"), .required = true},
        },
        .example = "rsync -avz ./ user@host:/backup/"});

    // --- Text --------------------------------------------------------------
    c.push_back(CS{
        .name = _t("sed — Ersetzen"), .category = _t("Text"),
        .description = _t("Stream-Editor; -i ändert die Datei direkt."),
        .templateText = "sed {inplace} -e {expr} {path}",
        .params = {
            CP{.name = "inplace", .label = _t("In Datei (-i)"), .kind = "flag", .flagValue = "-i"},
            CP{.name = "expr", .label = _t("Ausdruck"), .defaultValue = "'s/alt/neu/g'", .required = true},
            CP{.name = "path", .label = _t("Datei"), .required = true},
        },
        .example = "sed -i -e 's/alt/neu/g' datei"});
    c.push_back(CS{
        .name = _t("awk — Spalten"), .category = _t("Text"),
        .description = _t("Spaltenverarbeitung; Standard-Trenner Whitespace."),
        .templateText = "awk {program} {path}",
        .params = {
            CP{.name = "program", .label = _t("Programm"), .defaultValue = "'{print $1}'", .required = true},
            CP{.name = "path", .label = _t("Datei")},
        },
        .example = "awk '{print $1}' datei"});
    c.push_back(CS{
        .name = _t("sort — sortieren"), .category = _t("Text"),
        .description = _t("Sortiert Zeilen; -n numerisch, -r umgekehrt, -u eindeutig."),
        .templateText = "sort {numeric} {reverse} {unique} {path}",
        .params = {
            CP{.name = "numeric", .label = _t("Numerisch (-n)"), .kind = "flag", .flagValue = "-n"},
            CP{.name = "reverse", .label = _t("Umgekehrt (-r)"), .kind = "flag", .flagValue = "-r"},
            CP{.name = "unique", .label = _t("Eindeutig (-u)"), .kind = "flag", .flagValue = "-u"},
            CP{.name = "path", .label = _t("Datei")},
        },
        .example = "sort -n -r datei"});
    c.push_back(CS{
        .name = _t("diff — Dateien vergleichen"), .category = _t("Text"),
        .description = _t("Zeigt Unterschiede; -u unified."),
        .templateText = "diff -u {a} {b}",
        .params = {
            CP{.name = "a", .label = _t("Datei A"), .required = true},
            CP{.name = "b", .label = _t("Datei B"), .required = true},
        },
        .example = "diff -u alt neu"});
}

// --- System (weitere) / Netzwerk (weitere) / Paket / Archive (weitere) ------
void addPosixSystemNet(std::vector<CS> &c)
{
    c.push_back(CS{
        .name = _t("free — Speicher"), .category = _t("System"),
        .description = _t("RAM/Swap; -h lesbar."),
        .templateText = "free {human}",
        .params = {CP{.name = "human", .label = _t("Lesbar (-h)"), .kind = "flag", .defaultValue = "on", .flagValue = "-h"}},
        .example = "free -h"});
    c.push_back(CS{
        .name = _t("uptime — Laufzeit/Last"), .category = _t("System"),
        .description = _t("Systemlaufzeit und Load-Average."),
        .templateText = "uptime", .example = "uptime"});
    c.push_back(CS{
        .name = _t("id — Benutzer/Gruppen"), .category = _t("System"),
        .description = _t("Zeigt UID/GID und Gruppen."),
        .templateText = "id {user}",
        .params = {CP{.name = "user", .label = _t("Benutzer (optional)")}},
        .example = "id"});
    c.push_back(CS{
        .name = _t("lsblk — Blockgeräte"), .category = _t("System"),
        .description = _t("Laufwerke und Partitionen."),
        .templateText = "lsblk", .example = "lsblk"});
    c.push_back(CS{
        .name = _t("journalctl — Logs"), .category = _t("System"),
        .description = _t("systemd-Journal; -u Dienst, -f folgt, -n Zeilen."),
        .templateText = "journalctl {follow} -n {lines} {unit}",
        .params = {
            CP{.name = "follow", .label = _t("Folgen (-f)"), .kind = "flag", .flagValue = "-f"},
            CP{.name = "lines", .label = _t("Zeilen"), .defaultValue = "100"},
            CP{.name = "unit", .label = _t("-u Dienst")},
        },
        .example = "journalctl -u nginx -n 100"});
    c.push_back(CS{
        .name = _t("crontab — Cronjobs"), .category = _t("System"),
        .description = _t("Listet (-l) geplante Aufgaben."),
        .templateText = "crontab -l", .example = "crontab -l"});
    c.push_back(CS{
        .name = _t("uname — Systeminfo"), .category = _t("System"),
        .description = _t("Kernel-/OS-Infos; -a alles."),
        .templateText = "uname -a", .example = "uname -a"});

    // --- Netzwerk (weitere) ------------------------------------------------
    c.push_back(CS{
        .name = _t("wget — Download"), .category = _t("Netzwerk"),
        .description = _t("Lädt eine Datei; -O Zielname."),
        .templateText = "wget {url} {output}",
        .params = {
            CP{.name = "url", .label = _t("URL"), .required = true},
            CP{.name = "output", .label = _t("-O datei")},
        },
        .example = "wget https://example.com/file"});
    c.push_back(CS{
        .name = _t("dig — DNS-Abfrage"), .category = _t("Netzwerk"),
        .description = _t("Fragt DNS ab; +short kurz."),
        .templateText = "dig {short} {host} {type}",
        .params = {
            CP{.name = "short", .label = _t("Kurz (+short)"), .kind = "flag", .defaultValue = "on", .flagValue = "+short"},
            CP{.name = "host", .label = _t("Host"), .required = true},
            CP{.name = "type", .label = _t("Typ"), .kind = "choice", .choices = {"", "A", "AAAA", "MX", "TXT", "NS", "CNAME"}},
        },
        .example = "dig +short example.com"});
    c.push_back(CS{
        .name = _t("ip a — Adressen"), .category = _t("Netzwerk"),
        .description = _t("Netzwerk-Interfaces und IP-Adressen."),
        .templateText = "ip a", .example = "ip a"});
    c.push_back(CS{
        .name = _t("scp — sicher kopieren"), .category = _t("Netzwerk"),
        .description = _t("Kopiert über SSH; -r rekursiv."),
        .templateText = "scp {recursive} {src} {dst}",
        .params = {
            CP{.name = "recursive", .label = _t("Rekursiv (-r)"), .kind = "flag", .flagValue = "-r"},
            CP{.name = "src", .label = _t("Quelle"), .required = true},
            CP{.name = "dst", .label = _t("Ziel (user@host:/pfad)"), .required = true},
        },
        .example = "scp datei user@host:/tmp/"});

    // --- Prozesse / Paketverwaltung ---------------------------------------
    c.push_back(CS{
        .name = _t("pkill — nach Name beenden"), .category = _t("System"),
        .description = _t("Beendet Prozesse per Name; -9 erzwingt."),
        .templateText = "pkill {force} {name}",
        .params = {
            CP{.name = "force", .label = _t("SIGKILL (-9)"), .kind = "flag", .flagValue = "-9"},
            CP{.name = "name", .label = _t("Name"), .required = true},
        },
        .example = "pkill -9 node", .danger = true});
    c.push_back(CS{
        .name = _t("apt — installieren"), .category = _t("Paket"),
        .description = _t("Installiert ein Paket (Debian/Ubuntu)."),
        .templateText = "sudo apt install -y {package}",
        .params = {CP{.name = "package", .label = _t("Paket"), .required = true}},
        .example = "sudo apt install -y htop"});
    c.push_back(CS{
        .name = _t("apt — Update/Upgrade"), .category = _t("Paket"),
        .description = _t("Aktualisiert Paketlisten (optional + Upgrade)."),
        .templateText = "sudo apt update {upgrade}",
        .params = {CP{.name = "upgrade", .label = _t("+ Upgrade"), .kind = "flag", .flagValue = "&& sudo apt upgrade -y"}},
        .example = "sudo apt update"});
    c.push_back(CS{
        .name = _t("dnf — installieren"), .category = _t("Paket"),
        .description = _t("Installiert ein Paket (RHEL/Fedora)."),
        .templateText = "sudo dnf install -y {package}",
        .params = {CP{.name = "package", .label = _t("Paket"), .required = true}},
        .example = "sudo dnf install -y htop"});

    // --- Archive (weitere) -------------------------------------------------
    c.push_back(CS{
        .name = _t("zip — packen"), .category = _t("Archive"),
        .description = _t("Erstellt ein ZIP; -r rekursiv."),
        .templateText = "zip -r {archive} {target}",
        .params = {
            CP{.name = "archive", .label = _t("Archiv"), .defaultValue = "archiv.zip", .required = true},
            CP{.name = "target", .label = _t("Quelle"), .required = true},
        },
        .example = "zip -r a.zip ordner"});
    c.push_back(CS{
        .name = _t("unzip — entpacken"), .category = _t("Archive"),
        .description = _t("Entpackt ein ZIP; -d Zielordner."),
        .templateText = "unzip {archive} {dest}",
        .params = {
            CP{.name = "archive", .label = _t("Archiv"), .required = true},
            CP{.name = "dest", .label = _t("-d Ziel")},
        },
        .example = "unzip a.zip"});
}

// --- Git (weitere) / Docker (weitere) ---------------------------------------
void addGitDockerMore(std::vector<CS> &c)
{
    c.push_back(CS{
        .name = _t("git add"), .category = _t("Git"),
        .description = _t("Stellt Änderungen bereit; . für alles."),
        .templateText = "git add {path}",
        .params = {CP{.name = "path", .label = _t("Pfad"), .defaultValue = "."}},
        .example = "git add .", .platform = "any"});
    c.push_back(CS{
        .name = _t("git commit"), .category = _t("Git"),
        .description = _t("Erstellt einen Commit mit Nachricht."),
        .templateText = "git commit -m {message}",
        .params = {CP{.name = "message", .label = _t("Nachricht"), .defaultValue = "\"update\"", .required = true}},
        .example = "git commit -m \"fix\"", .platform = "any"});
    c.push_back(CS{
        .name = _t("git push"), .category = _t("Git"),
        .description = _t("Überträgt Commits zum Remote."),
        .templateText = "git push {remote} {branch}",
        .params = {
            CP{.name = "remote", .label = _t("Remote"), .defaultValue = "origin"},
            CP{.name = "branch", .label = _t("Branch")},
        },
        .example = "git push origin main", .platform = "any"});
    c.push_back(CS{
        .name = _t("git pull"), .category = _t("Git"),
        .description = _t("Holt und merged vom Remote."),
        .templateText = "git pull {remote} {branch}",
        .params = {
            CP{.name = "remote", .label = _t("Remote"), .defaultValue = "origin"},
            CP{.name = "branch", .label = _t("Branch")},
        },
        .example = "git pull", .platform = "any"});
    c.push_back(CS{
        .name = _t("git clone"), .category = _t("Git"),
        .description = _t("Klont ein Repository."),
        .templateText = "git clone {url}",
        .params = {CP{.name = "url", .label = _t("URL"), .required = true}},
        .example = "git clone https://…", .platform = "any"});
    c.push_back(CS{
        .name = _t("git checkout"), .category = _t("Git"),
        .description = _t("Wechselt Branch; -b erstellt neu."),
        .templateText = "git checkout {create} {branch}",
        .params = {
            CP{.name = "create", .label = _t("Neu (-b)"), .kind = "flag", .flagValue = "-b"},
            CP{.name = "branch", .label = _t("Branch"), .required = true},
        },
        .example = "git checkout -b feature", .platform = "any"});

    // --- Docker (weitere) --------------------------------------------------
    c.push_back(CS{
        .name = _t("docker images"), .category = _t("Docker"),
        .description = _t("Listet lokale Images."),
        .templateText = "docker images", .example = "docker images", .platform = "any"});
    c.push_back(CS{
        .name = _t("docker exec — in Container"), .category = _t("Docker"),
        .description = _t("Befehl im Container; -it interaktiv."),
        .templateText = "docker exec {interactive} {container} {cmd}",
        .params = {
            CP{.name = "interactive", .label = _t("Interaktiv (-it)"), .kind = "flag", .defaultValue = "on", .flagValue = "-it"},
            CP{.name = "container", .label = _t("Container"), .required = true},
            CP{.name = "cmd", .label = _t("Befehl"), .defaultValue = "bash"},
        },
        .example = "docker exec -it web bash", .platform = "any"});
    c.push_back(CS{
        .name = _t("docker stop"), .category = _t("Docker"),
        .description = _t("Stoppt einen Container."),
        .templateText = "docker stop {container}",
        .params = {CP{.name = "container", .label = _t("Container"), .required = true}},
        .example = "docker stop web", .platform = "any"});
    c.push_back(CS{
        .name = _t("docker rm — entfernen"), .category = _t("Docker"),
        .description = _t("Entfernt Container; -f erzwingt."),
        .templateText = "docker rm {force} {container}",
        .params = {
            CP{.name = "force", .label = _t("Erzwingen (-f)"), .kind = "flag", .flagValue = "-f"},
            CP{.name = "container", .label = _t("Container"), .required = true},
        },
        .example = "docker rm -f web", .danger = true, .platform = "any"});
}

// ===========================================================================
// Windows (cmd.exe / PowerShell). Werden nur bei Windows-Verbindung gezeigt.
// ===========================================================================
void addWindowsBase(std::vector<CS> &c)
{
    c.push_back(CS{
        .name = _t("dir — Verzeichnis auflisten"), .category = _t("Dateien"),
        .description = _t("Listet Dateien; /w breit, /a auch versteckte, /o-n sortiert."),
        .templateText = "dir {wide} {all} {path}",
        .params = {
            CP{.name = "wide", .label = _t("Breit (/w)"), .kind = "flag", .flagValue = "/w"},
            CP{.name = "all", .label = _t("Versteckte (/a)"), .kind = "flag", .flagValue = "/a"},
            CP{.name = "path", .label = _t("Pfad")},
        },
        .example = "dir /a", .platform = "windows"});
    c.push_back(CS{
        .name = _t("tree — Baumansicht"), .category = _t("Dateien"),
        .description = _t("Zeigt die Verzeichnisstruktur; /f auch Dateien."),
        .templateText = "tree {files} {path}",
        .params = {
            CP{.name = "files", .label = _t("Mit Dateien (/f)"), .kind = "flag", .defaultValue = "on", .flagValue = "/f"},
            CP{.name = "path", .label = _t("Pfad")},
        },
        .example = "tree /f", .platform = "windows"});
    c.push_back(CS{
        .name = _t("xcopy — kopieren"), .category = _t("Dateien"),
        .description = _t("Kopiert Dateien/Ordner. /E inkl. Unterordner, /I Ziel = Ordner."),
        .templateText = "xcopy {src} {dst} {recursive}",
        .params = {
            CP{.name = "src", .label = _t("Quelle"), .required = true},
            CP{.name = "dst", .label = _t("Ziel"), .required = true},
            CP{.name = "recursive", .label = _t("Rekursiv (/E /I)"), .kind = "flag", .defaultValue = "on", .flagValue = "/E /I"},
        },
        .example = "xcopy src dst /E /I", .platform = "windows"});
    c.push_back(CS{
        .name = _t("move — verschieben/umbenennen"), .category = _t("Dateien"),
        .description = _t("Verschiebt oder benennt um."),
        .templateText = "move {src} {dst}",
        .params = {
            CP{.name = "src", .label = _t("Quelle"), .required = true},
            CP{.name = "dst", .label = _t("Ziel"), .required = true},
        },
        .example = "move alt.txt neu.txt", .platform = "windows"});
    c.push_back(CS{
        .name = _t("del — löschen"), .category = _t("Dateien"),
        .description = _t("Löscht Dateien. /F erzwingt, /Q ohne Nachfrage, /S rekursiv. VORSICHT!"),
        .templateText = "del {force} {quiet} {recursive} {path}",
        .params = {
            CP{.name = "force", .label = _t("Erzwingen (/F)"), .kind = "flag", .flagValue = "/F"},
            CP{.name = "quiet", .label = _t("Ohne Nachfrage (/Q)"), .kind = "flag", .flagValue = "/Q"},
            CP{.name = "recursive", .label = _t("Rekursiv (/S)"), .kind = "flag", .flagValue = "/S"},
            CP{.name = "path", .label = _t("Pfad/Maske"), .required = true},
        },
        .example = "del /F /Q *.tmp", .danger = true, .platform = "windows"});
    c.push_back(CS{
        .name = _t("mkdir — Verzeichnis anlegen"), .category = _t("Dateien"),
        .description = _t("Erstellt ein Verzeichnis (inkl. Zwischenebenen)."),
        .templateText = "mkdir {path}",
        .params = {CP{.name = "path", .label = _t("Pfad"), .required = true}},
        .example = "mkdir a\\b\\c", .platform = "windows"});
    c.push_back(CS{
        .name = _t("type — Datei anzeigen"), .category = _t("Dateien"),
        .description = _t("Gibt den Inhalt einer Textdatei aus."),
        .templateText = "type {path}",
        .params = {CP{.name = "path", .label = _t("Datei"), .required = true}},
        .example = "type log.txt", .platform = "windows"});
    c.push_back(CS{
        .name = _t("findstr — im Inhalt suchen"), .category = _t("Suche"),
        .description = _t("Durchsucht Dateien nach Muster. /I ignoriert Groß/Klein, /S rekursiv, /N Zeilennr."),
        .templateText = "findstr {ignorecase} {recursive} {linenum} {pattern} {path}",
        .params = {
            CP{.name = "ignorecase", .label = _t("Groß/klein egal (/I)"), .kind = "flag", .flagValue = "/I"},
            CP{.name = "recursive", .label = _t("Rekursiv (/S)"), .kind = "flag", .defaultValue = "on", .flagValue = "/S"},
            CP{.name = "linenum", .label = _t("Zeilennummern (/N)"), .kind = "flag", .defaultValue = "on", .flagValue = "/N"},
            CP{.name = "pattern", .label = _t("Suchmuster"), .defaultValue = "\"TODO\"", .required = true},
            CP{.name = "path", .label = _t("Datei/Maske"), .defaultValue = "*.*"},
        },
        .example = "findstr /S /N \"TODO\" *.*", .platform = "windows"});
    c.push_back(CS{
        .name = _t("tasklist — Prozesse"), .category = _t("System"),
        .description = _t("Listet laufende Prozesse. Mit /FI filtern."),
        .templateText = "tasklist {filter}",
        .params = {CP{.name = "filter", .label = _t("Filter (z.B. /FI \"IMAGENAME eq x.exe\")")}},
        .example = "tasklist /FI \"IMAGENAME eq python.exe\"", .platform = "windows"});
    c.push_back(CS{
        .name = _t("taskkill — Prozess beenden"), .category = _t("System"),
        .description = _t("Beendet einen Prozess per PID. /F erzwingt, /T inkl. Kindprozesse."),
        .templateText = "taskkill {force} {tree} /PID {pid}",
        .params = {
            CP{.name = "force", .label = _t("Erzwingen (/F)"), .kind = "flag", .defaultValue = "on", .flagValue = "/F"},
            CP{.name = "tree", .label = _t("Mit Kindern (/T)"), .kind = "flag", .flagValue = "/T"},
            CP{.name = "pid", .label = _t("PID"), .required = true},
        },
        .example = "taskkill /F /PID 1234", .danger = true, .platform = "windows"});
    c.push_back(CS{
        .name = _t("sc query — Dienst-Status"), .category = _t("System"),
        .description = _t("Zeigt den Status eines Windows-Dienstes."),
        .templateText = "sc query {service}",
        .params = {CP{.name = "service", .label = _t("Dienstname"), .required = true}},
        .example = "sc query wuauserv", .platform = "windows"});
    c.push_back(CS{
        .name = _t("systeminfo — Systeminfo"), .category = _t("System"),
        .description = _t("Zeigt OS-, Hardware- und Patch-Informationen."),
        .templateText = "systeminfo", .example = "systeminfo", .platform = "windows"});
    c.push_back(CS{
        .name = _t("ipconfig — Netzwerk"), .category = _t("Netzwerk"),
        .description = _t("Zeigt die IP-Konfiguration. /all für Details."),
        .templateText = "ipconfig {all}",
        .params = {CP{.name = "all", .label = _t("Details (/all)"), .kind = "flag", .defaultValue = "on", .flagValue = "/all"}},
        .example = "ipconfig /all", .platform = "windows"});
    c.push_back(CS{
        .name = _t("ping — Erreichbarkeit"), .category = _t("Netzwerk"),
        .description = _t("Pingt einen Host. -n Anzahl, -t dauerhaft."),
        // -t hinter -n: bei ping gewinnt die letzte Angabe.
        .templateText = "ping -n {count} {endless} {host}",
        .params = {
            CP{.name = "count", .label = _t("Anzahl"), .defaultValue = "4"},
            CP{.name = "endless", .label = _t("Dauerhaft (-t)"), .kind = "flag", .flagValue = "-t"},
            CP{.name = "host", .label = _t("Host/IP"), .required = true},
        },
        .example = "ping -n 4 8.8.8.8", .platform = "windows"});
    c.push_back(CS{
        .name = _t("ren — umbenennen"), .category = _t("Dateien"),
        .description = _t("Benennt eine Datei um."),
        .templateText = "ren {old} {new}",
        .params = {
            CP{.name = "old", .label = _t("Alt"), .required = true},
            CP{.name = "new", .label = _t("Neu"), .required = true},
        },
        .example = "ren a.txt b.txt", .platform = "windows"});
    c.push_back(CS{
        .name = _t("robocopy — kopieren/spiegeln"), .category = _t("Dateien"),
        .description = _t("Robustes Kopieren; /E Unterordner, /MIR spiegelt."),
        .templateText = "robocopy {src} {dst} {mirror}",
        .params = {
            CP{.name = "src", .label = _t("Quelle"), .required = true},
            CP{.name = "dst", .label = _t("Ziel"), .required = true},
            CP{.name = "mirror", .label = _t("Spiegeln (/MIR)"), .kind = "flag", .flagValue = "/MIR"},
        },
        .example = "robocopy C:\\a D:\\b /E", .platform = "windows"});
    c.push_back(CS{
        .name = _t("fc — Dateien vergleichen"), .category = _t("Dateien"),
        .description = _t("Vergleicht zwei Dateien."),
        .templateText = "fc {a} {b}",
        .params = {
            CP{.name = "a", .label = _t("Datei A"), .required = true},
            CP{.name = "b", .label = _t("Datei B"), .required = true},
        },
        .example = "fc alt.txt neu.txt", .platform = "windows"});
    c.push_back(CS{
        .name = _t("Get-Process (PowerShell)"), .category = _t("System"),
        .description = _t("Listet laufende Prozesse."),
        .templateText = "powershell -Command \"Get-Process {filter}\"",
        .params = {CP{.name = "filter", .label = _t("| Where/Sort …")}},
        .example = "powershell -Command \"Get-Process\"", .platform = "windows"});
    c.push_back(CS{
        .name = _t("Get-Service (PowerShell)"), .category = _t("System"),
        .description = _t("Listet Dienste."),
        .templateText = "powershell -Command \"Get-Service {filter}\"",
        .params = {CP{.name = "filter", .label = _t("Filter")}},
        .example = "powershell -Command \"Get-Service\"", .platform = "windows"});
    c.push_back(CS{
        .name = _t("net start — Dienst starten"), .category = _t("System"),
        .description = _t("Startet einen Windows-Dienst."),
        .templateText = "net start {service}",
        .params = {CP{.name = "service", .label = _t("Dienst"), .required = true}},
        .example = "net start spooler", .platform = "windows"});
    c.push_back(CS{
        .name = _t("shutdown — herunterfahren/neustart"), .category = _t("System"),
        .description = _t("/s herunterfahren, /r neustart, /a abbrechen; /t Verzögerung."),
        .templateText = "shutdown {mode} /t {delay}",
        .params = {
            CP{.name = "mode", .label = _t("Modus"), .kind = "choice", .defaultValue = "/r", .choices = {"/s", "/r", "/a"}},
            CP{.name = "delay", .label = _t("Sekunden"), .defaultValue = "0"},
        },
        .example = "shutdown /r /t 0", .danger = true, .platform = "windows"});
    c.push_back(CS{
        .name = _t("sfc — Systemdateien prüfen"), .category = _t("System"),
        .description = _t("Prüft/repariert Systemdateien."),
        .templateText = "sfc /scannow", .example = "sfc /scannow", .platform = "windows"});
    c.push_back(CS{
        .name = _t("tracert — Route"), .category = _t("Netzwerk"),
        .description = _t("Zeigt die Route zu einem Host."),
        .templateText = "tracert {host}",
        .params = {CP{.name = "host", .label = _t("Host"), .required = true}},
        .example = "tracert example.com", .platform = "windows"});
    c.push_back(CS{
        .name = _t("nslookup — DNS"), .category = _t("Netzwerk"),
        .description = _t("DNS-Abfrage."),
        .templateText = "nslookup {host}",
        .params = {CP{.name = "host", .label = _t("Host"), .required = true}},
        .example = "nslookup example.com", .platform = "windows"});
    c.push_back(CS{
        .name = _t("netstat — Verbindungen"), .category = _t("Netzwerk"),
        .description = _t("Aktive Verbindungen/Ports; -ano mit PID."),
        .templateText = "netstat -ano {filter}",
        .params = {CP{.name = "filter", .label = _t("| findstr …")}},
        .example = "netstat -ano | findstr :443", .platform = "windows"});
    c.push_back(CS{
        .name = _t("net user — Konten"), .category = _t("System"),
        .description = _t("Listet/zeigt Benutzerkonten."),
        .templateText = "net user {user}",
        .params = {CP{.name = "user", .label = _t("Benutzer (optional)")}},
        .example = "net user", .platform = "windows"});
}

// --- Windows: Rechte / Besitz + System / Wartung + Netzwerk (weitere) -------
void addWindowsAdmin(std::vector<CS> &c)
{
    c.push_back(CS{
        .name = _t("takeown — Besitz übernehmen"), .category = _t("Rechte"),
        .description = _t("Macht den aktuellen Benutzer zum Eigentümer. /R rekursiv, /A für Admin-Gruppe."),
        .templateText = "takeown /F {path} {recursive} {admin}",
        .params = {
            CP{.name = "path", .label = _t("Pfad"), .required = true},
            CP{.name = "recursive", .label = _t("Rekursiv (/R /D Y)"), .kind = "flag", .flagValue = "/R /D Y"},
            CP{.name = "admin", .label = _t("An Admins (/A)"), .kind = "flag", .flagValue = "/A"},
        },
        .example = "takeown /F C:\\daten /R /D Y", .danger = true, .platform = "windows"});
    c.push_back(CS{
        .name = _t("icacls — Rechte anzeigen/setzen"), .category = _t("Rechte"),
        .description = _t("Zeigt oder ändert NTFS-Rechte. /grant erteilt, /T rekursiv."),
        .templateText = "icacls {path} {grant} {recursive}",
        .params = {
            CP{.name = "path", .label = _t("Pfad"), .required = true},
            CP{.name = "grant", .label = _t("Erteilen (/grant Benutzer:(F))")},
            CP{.name = "recursive", .label = _t("Rekursiv (/T)"), .kind = "flag", .flagValue = "/T"},
        },
        .example = "icacls C:\\daten /grant Tobia:(F) /T", .platform = "windows"});
    c.push_back(CS{
        .name = _t("attrib — Attribute"), .category = _t("Rechte"),
        .description = _t("Setzt/entfernt Datei-Attribute (R=schreibgeschützt, H=versteckt, S=System)."),
        .templateText = "attrib {add} {remove} {path} {recursive}",
        .params = {
            CP{.name = "add", .label = _t("Setzen (+R +H …)")},
            CP{.name = "remove", .label = _t("Entfernen (-R -H …)")},
            CP{.name = "path", .label = _t("Pfad/Maske"), .defaultValue = "*.*", .required = true},
            CP{.name = "recursive", .label = _t("Rekursiv (/S)"), .kind = "flag", .flagValue = "/S"},
        },
        .example = "attrib -R -H -S C:\\daten\\*.* /S", .platform = "windows"});
    c.push_back(CS{
        .name = _t("cacls/whoami /priv — Rechte prüfen"), .category = _t("Rechte"),
        .description = _t("Zeigt die Privilegien des aktuellen Benutzers."),
        .templateText = "whoami /priv", .example = "whoami /priv", .platform = "windows"});

    // --- Windows: System / Wartung -----------------------------------------
    c.push_back(CS{
        .name = _t("whoami — aktueller Benutzer"), .category = _t("System"),
        .description = _t("Zeigt Domäne\\Benutzer; /groups Gruppen."),
        .templateText = "whoami {groups}",
        .params = {CP{.name = "groups", .label = _t("Gruppen (/groups)"), .kind = "flag", .flagValue = "/groups"}},
        .example = "whoami /groups", .platform = "windows"});
    c.push_back(CS{
        .name = _t("hostname — Rechnername"), .category = _t("System"),
        .description = _t("Zeigt den Computernamen."),
        .templateText = "hostname", .example = "hostname", .platform = "windows"});
    c.push_back(CS{
        .name = _t("where — Programm finden"), .category = _t("System"),
        .description = _t("Sucht eine ausführbare Datei im PATH."),
        .templateText = "where {name}",
        .params = {CP{.name = "name", .label = _t("Name"), .defaultValue = "python", .required = true}},
        .example = "where python", .platform = "windows"});
    c.push_back(CS{
        .name = _t("set / setx — Umgebungsvariablen"), .category = _t("System"),
        .description = _t("set zeigt/setzt temporär; setx dauerhaft (neue Sitzung)."),
        .templateText = "setx {name} {value}",
        .params = {
            CP{.name = "name", .label = _t("Variable"), .required = true},
            CP{.name = "value", .label = _t("Wert"), .required = true},
        },
        .example = "setx PATH \"%PATH%;C:\\tools\"", .platform = "windows"});
    c.push_back(CS{
        .name = _t("reg query — Registry lesen"), .category = _t("System"),
        .description = _t("Liest einen Registry-Schlüssel/Wert."),
        .templateText = "reg query {key} {value}",
        .params = {
            CP{.name = "key", .label = _t("Schlüssel"), .defaultValue = "HKCU\\Software", .required = true},
            CP{.name = "value", .label = _t("/v Wert (optional)")},
        },
        .example = "reg query HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion", .platform = "windows"});
    c.push_back(CS{
        .name = _t("schtasks — geplante Aufgaben"), .category = _t("System"),
        .description = _t("Listet/erstellt geplante Aufgaben."),
        .templateText = "schtasks /query {verbose}",
        .params = {CP{.name = "verbose", .label = _t("Ausführlich (/v /fo LIST)"), .kind = "flag", .flagValue = "/v /fo LIST"}},
        .example = "schtasks /query /v /fo LIST", .platform = "windows"});
    c.push_back(CS{
        .name = _t("chkdsk — Datenträger prüfen"), .category = _t("System"),
        .description = _t("Prüft ein Laufwerk; /F repariert, /R Sektoren."),
        .templateText = "chkdsk {drive} {fix} {recover}",
        .params = {
            CP{.name = "drive", .label = _t("Laufwerk"), .defaultValue = "C:"},
            CP{.name = "fix", .label = _t("Reparieren (/F)"), .kind = "flag", .flagValue = "/F"},
            CP{.name = "recover", .label = _t("Sektoren (/R)"), .kind = "flag", .flagValue = "/R"},
        },
        .example = "chkdsk C: /F", .danger = true, .platform = "windows"});
    c.push_back(CS{
        .name = _t("DISM — Abbild reparieren"), .category = _t("System"),
        .description = _t("Repariert das Windows-Komponentenabbild."),
        .templateText = "DISM /Online /Cleanup-Image /RestoreHealth",
        .example = "DISM /Online /Cleanup-Image /RestoreHealth", .platform = "windows"});
    c.push_back(CS{
        .name = _t("gpupdate — Richtlinien aktualisieren"), .category = _t("System"),
        .description = _t("Aktualisiert Gruppenrichtlinien; /force erzwingt."),
        .templateText = "gpupdate {force}",
        .params = {CP{.name = "force", .label = _t("Erzwingen (/force)"), .kind = "flag", .defaultValue = "on", .flagValue = "/force"}},
        .example = "gpupdate /force", .platform = "windows"});
    c.push_back(CS{
        .name = _t("powercfg — Energie"), .category = _t("System"),
        .description = _t("Energieberichte/-einstellungen; /batteryreport, /energy."),
        .templateText = "powercfg {report}",
        .params = {CP{.name = "report", .label = _t("Bericht"), .kind = "choice", .defaultValue = "/batteryreport", .choices = {"/batteryreport", "/energy", "/list"}}},
        .example = "powercfg /batteryreport", .platform = "windows"});
    c.push_back(CS{
        .name = _t("driverquery — Treiber"), .category = _t("System"),
        .description = _t("Listet installierte Gerätetreiber."),
        .templateText = "driverquery", .example = "driverquery", .platform = "windows"});
    c.push_back(CS{
        .name = _t("fsutil — Dateisystem"), .category = _t("System"),
        .description = _t("Dateisystem-Infos; z.B. Laufwerke auflisten."),
        .templateText = "fsutil fsinfo drives", .example = "fsutil fsinfo drives", .platform = "windows"});

    // --- Windows: Netzwerk (weitere) ---------------------------------------
    c.push_back(CS{
        .name = _t("netsh — Netzwerk konfigurieren"), .category = _t("Netzwerk"),
        .description = _t("Mächtiges Netzwerk-Tool (WLAN-Profile, Firewall, Interfaces)."),
        .templateText = "netsh {area}",
        .params = {CP{.name = "area", .label = _t("Bereich"), .kind = "choice", .defaultValue = "wlan show profiles",
                      .choices = {"wlan show profiles", "interface show interface",
                                  "advfirewall show allprofiles", "int ip show config"}}},
        .example = "netsh wlan show profiles", .platform = "windows"});
    c.push_back(CS{
        .name = _t("arp — ARP-Tabelle"), .category = _t("Netzwerk"),
        .description = _t("Zeigt die ARP-Zuordnung (IP↔MAC)."),
        .templateText = "arp -a", .example = "arp -a", .platform = "windows"});
    c.push_back(CS{
        .name = _t("getmac — MAC-Adressen"), .category = _t("Netzwerk"),
        .description = _t("Zeigt die MAC-Adressen der Adapter."),
        .templateText = "getmac", .example = "getmac", .platform = "windows"});
    c.push_back(CS{
        .name = _t("route print — Routing"), .category = _t("Netzwerk"),
        .description = _t("Zeigt die IP-Routing-Tabelle."),
        .templateText = "route print", .example = "route print", .platform = "windows"});
}

// --- POSIX: Rechte / Benutzer + System / Diagnose + Netzwerk / Security -----
void addPosixAdmin(std::vector<CS> &c)
{
    c.push_back(CS{
        .name = _t("chgrp — Gruppe ändern"), .category = _t("Rechte"),
        .description = _t("Ändert die Gruppe; -R rekursiv."),
        .templateText = "chgrp {recursive} {group} {path}",
        .params = {
            CP{.name = "recursive", .label = _t("Rekursiv (-R)"), .kind = "flag", .flagValue = "-R"},
            CP{.name = "group", .label = _t("Gruppe"), .required = true},
            CP{.name = "path", .label = _t("Pfad"), .required = true},
        },
        .example = "chgrp -R www-data /var/www"});
    c.push_back(CS{
        .name = _t("chattr — Datei-Attribute"), .category = _t("Rechte"),
        .description = _t("Setzt erweiterte Attribute; +i macht unveränderbar (immutable)."),
        .templateText = "sudo chattr {op}{attr} {path}",
        .params = {
            CP{.name = "op", .label = _t("Operation"), .kind = "choice", .defaultValue = "+", .choices = {"+", "-"}},
            CP{.name = "attr", .label = _t("Attribut"), .kind = "choice", .defaultValue = "i", .choices = {"i", "a"}},
            CP{.name = "path", .label = _t("Pfad"), .required = true},
        },
        .example = "sudo chattr +i wichtig.conf"});
    c.push_back(CS{
        .name = _t("useradd — Benutzer anlegen"), .category = _t("System"),
        .description = _t("Legt einen Benutzer an; -m Home, -s Shell."),
        .templateText = "sudo useradd {home} -s {shell} {user}",
        .params = {
            CP{.name = "home", .label = _t("Home anlegen (-m)"), .kind = "flag", .defaultValue = "on", .flagValue = "-m"},
            CP{.name = "shell", .label = _t("Shell"), .defaultValue = "/bin/bash"},
            CP{.name = "user", .label = _t("Benutzer"), .required = true},
        },
        .example = "sudo useradd -m -s /bin/bash deploy"});
    c.push_back(CS{
        .name = _t("passwd — Passwort ändern"), .category = _t("System"),
        .description = _t("Ändert das Passwort eines Benutzers."),
        .templateText = "sudo passwd {user}",
        .params = {CP{.name = "user", .label = _t("Benutzer")}},
        .example = "sudo passwd deploy"});
    c.push_back(CS{
        .name = _t("usermod — Benutzer ändern"), .category = _t("System"),
        .description = _t("Ändert einen Benutzer; -aG fügt zu Gruppen hinzu."),
        .templateText = "sudo usermod -aG {groups} {user}",
        .params = {
            CP{.name = "groups", .label = _t("Gruppen (kommagetrennt)"), .defaultValue = "sudo", .required = true},
            CP{.name = "user", .label = _t("Benutzer"), .required = true},
        },
        .example = "sudo usermod -aG docker deploy"});

    // --- POSIX: System / Diagnose (weitere) --------------------------------
    c.push_back(CS{
        .name = _t("top — Prozess-Monitor"), .category = _t("System"),
        .description = _t("Interaktiver Prozess-/Last-Monitor (q beendet)."),
        .templateText = "top {batch}",
        .params = {CP{.name = "batch", .label = _t("Einmal (-b -n1)"), .kind = "flag", .flagValue = "-b -n1"}},
        .example = "top -b -n1"});
    c.push_back(CS{
        .name = _t("htop — Prozess-Monitor"), .category = _t("System"),
        .description = _t("Komfortabler Prozess-Monitor (falls installiert)."),
        .templateText = "htop", .example = "htop"});
    c.push_back(CS{
        .name = _t("lsof — offene Dateien"), .category = _t("System"),
        .description = _t("Listet offene Dateien/Sockets; -i Netzwerk, -p PID."),
        .templateText = "sudo lsof {net} {pid} {filter}",
        .params = {
            CP{.name = "net", .label = _t("Nur Netzwerk (-i)"), .kind = "flag", .flagValue = "-i"},
            // -a verknuepft mit -i per UND (sonst listet lsof Netzwerk ODER PID).
            CP{.name = "pid", .label = _t("PID (-p)"), .description = _t("Leer = alle Prozesse"), .flagValue = "-a -p"},
            CP{.name = "filter", .label = _t("Filter (| grep …)")},
        },
        .example = "sudo lsof -i"});
    c.push_back(CS{
        .name = _t("dmesg — Kernel-Log"), .category = _t("System"),
        .description = _t("Kernel-Ringpuffer; -T mit Zeitstempeln."),
        .templateText = "dmesg {human} {tail}",
        .params = {
            CP{.name = "human", .label = _t("Zeitstempel (-T)"), .kind = "flag", .defaultValue = "on", .flagValue = "-T"},
            CP{.name = "tail", .label = _t("Letzte Zeilen (| tail -50)"), .kind = "flag", .flagValue = "| tail -50"},
        },
        .example = "dmesg -T | tail -50"});
    c.push_back(CS{
        .name = _t("who / w — Anmeldungen"), .category = _t("System"),
        .description = _t("Zeigt angemeldete Benutzer."),
        .templateText = "who", .example = "who"});
    c.push_back(CS{
        .name = _t("last — Login-Historie"), .category = _t("System"),
        .description = _t("Zeigt vergangene Anmeldungen."),
        .templateText = "last {count}",
        .params = {CP{.name = "count", .label = _t("-n Anzahl"), .defaultValue = "20"}},
        .example = "last -n 20"});
    c.push_back(CS{
        .name = _t("mount — Dateisysteme"), .category = _t("System"),
        .description = _t("Zeigt eingehängte Dateisysteme."),
        .templateText = "mount {filter}",
        .params = {CP{.name = "filter", .label = _t("| grep …")}},
        .example = "mount"});

    // --- POSIX: Netzwerk / Security (weitere) ------------------------------
    c.push_back(CS{
        .name = _t("ufw — Firewall (Ubuntu)"), .category = _t("Netzwerk"),
        .description = _t("Einfache Firewall; status/enable/allow/deny."),
        .templateText = "sudo ufw {action} {target}",
        .params = {
            CP{.name = "action", .label = _t("Aktion"), .kind = "choice", .defaultValue = "status", .choices = {"status", "enable", "disable", "allow", "deny"}},
            CP{.name = "target", .label = _t("Port/Dienst (z.B. 22/tcp)")},
        },
        .example = "sudo ufw allow 22/tcp"});
    c.push_back(CS{
        .name = _t("openssl — Zertifikat prüfen"), .category = _t("Netzwerk"),
        .description = _t("Zeigt ein Server-Zertifikat / dessen Ablaufdatum."),
        .templateText = "echo | openssl s_client -connect {host}:{port} 2>/dev/null | openssl x509 -noout -dates",
        .params = {
            CP{.name = "host", .label = _t("Host"), .required = true},
            CP{.name = "port", .label = _t("Port"), .defaultValue = "443"},
        },
        .example = "openssl s_client -connect example.com:443"});
    c.push_back(CS{
        .name = _t("ssh-keygen — Schlüssel erzeugen"), .category = _t("Netzwerk"),
        .description = _t("Erzeugt ein SSH-Schlüsselpaar; -t Typ, -C Kommentar."),
        .templateText = "ssh-keygen -t {type} -C {comment}",
        .params = {
            CP{.name = "type", .label = _t("Typ"), .kind = "choice", .defaultValue = "ed25519", .choices = {"ed25519", "rsa", "ecdsa"}},
            CP{.name = "comment", .label = _t("Kommentar"), .defaultValue = "\"mein-key\""},
        },
        .example = "ssh-keygen -t ed25519 -C \"laptop\""});
    c.push_back(CS{
        .name = _t("nginx -t — Konfig testen"), .category = _t("System"),
        .description = _t("Testet die nginx-Konfiguration auf Syntaxfehler."),
        .templateText = "sudo nginx -t", .example = "sudo nginx -t"});
}

// --- POSIX: weitere Befehle --------------------------------------------------
void addPosixMisc(std::vector<CS> &c)
{
    c.push_back(CS{
        .name = _t("pwd — aktuelles Verzeichnis"), .category = _t("System"),
        .description = _t("Zeigt das aktuelle Arbeitsverzeichnis."),
        .templateText = "pwd", .example = "pwd"});
    c.push_back(CS{
        .name = _t("date — Datum/Uhrzeit"), .category = _t("System"),
        .description = _t("Zeigt/formatiert Datum und Uhrzeit."),
        .templateText = "date {fmt}",
        .params = {CP{.name = "fmt", .label = _t("Format (z.B. +%Y-%m-%d)")}},
        .example = "date +%Y-%m-%d"});
    c.push_back(CS{
        .name = _t("printenv — Umgebungsvariablen"), .category = _t("System"),
        .description = _t("Zeigt Umgebungsvariablen."),
        .templateText = "printenv {name}",
        .params = {CP{.name = "name", .label = _t("Variable (optional)")}},
        .example = "printenv PATH"});
    c.push_back(CS{
        .name = _t("which — Programm finden"), .category = _t("System"),
        .description = _t("Zeigt den Pfad eines Programms im PATH."),
        .templateText = "which {name}",
        .params = {CP{.name = "name", .label = _t("Name"), .defaultValue = "python3", .required = true}},
        .example = "which python3"});
    c.push_back(CS{
        .name = _t("file — Dateityp bestimmen"), .category = _t("Dateien"),
        .description = _t("Bestimmt den Dateityp anhand des Inhalts."),
        .templateText = "file {path}",
        .params = {CP{.name = "path", .label = _t("Datei"), .required = true}},
        .example = "file bild.png"});
    c.push_back(CS{
        .name = _t("sha256sum — Prüfsumme"), .category = _t("Dateien"),
        .description = _t("Berechnet die SHA-256-Prüfsumme einer Datei."),
        .templateText = "sha256sum {path}",
        .params = {CP{.name = "path", .label = _t("Datei"), .required = true}},
        .example = "sha256sum datei.iso"});
    c.push_back(CS{
        .name = _t("gzip — komprimieren"), .category = _t("Archive"),
        .description = _t("Komprimiert eine Datei (.gz); -d entpackt, -k behält Original."),
        .templateText = "gzip {decompress} {keep} {path}",
        .params = {
            CP{.name = "decompress", .label = _t("Entpacken (-d)"), .kind = "flag", .flagValue = "-d"},
            CP{.name = "keep", .label = _t("Original behalten (-k)"), .kind = "flag", .defaultValue = "on", .flagValue = "-k"},
            CP{.name = "path", .label = _t("Datei"), .required = true},
        },
        .example = "gzip -k datei.log"});
    c.push_back(CS{
        .name = _t("ping — Erreichbarkeit"), .category = _t("Netzwerk"),
        .description = _t("Pingt einen Host; -c begrenzt die Anzahl."),
        .templateText = "ping -c {count} {host}",
        .params = {
            CP{.name = "count", .label = _t("Anzahl"), .defaultValue = "4"},
            CP{.name = "host", .label = _t("Host/IP"), .required = true},
        },
        .example = "ping -c 4 8.8.8.8"});
    c.push_back(CS{
        .name = _t("traceroute — Route"), .category = _t("Netzwerk"),
        .description = _t("Zeigt die Hops zu einem Host."),
        .templateText = "traceroute {host}",
        .params = {CP{.name = "host", .label = _t("Host"), .required = true}},
        .example = "traceroute example.com"});
    c.push_back(CS{
        .name = _t("nc — Port testen"), .category = _t("Netzwerk"),
        .description = _t("Netcat; -z nur prüfen, -v ausführlich."),
        .templateText = "nc {zero} {verbose} {host} {port}",
        .params = {
            CP{.name = "zero", .label = _t("Nur prüfen (-z)"), .kind = "flag", .defaultValue = "on", .flagValue = "-z"},
            CP{.name = "verbose", .label = _t("Ausführlich (-v)"), .kind = "flag", .defaultValue = "on", .flagValue = "-v"},
            CP{.name = "host", .label = _t("Host"), .required = true},
            CP{.name = "port", .label = _t("Port"), .required = true},
        },
        .example = "nc -z -v host 22"});
    c.push_back(CS{
        .name = _t("nmap — Portscan"), .category = _t("Netzwerk"),
        .description = _t("Scannt offene Ports; -F schneller Scan."),
        .templateText = "nmap {fast} {host}",
        .params = {
            CP{.name = "fast", .label = _t("Schnell (-F)"), .kind = "flag", .defaultValue = "on", .flagValue = "-F"},
            CP{.name = "host", .label = _t("Host/Netz"), .required = true},
        },
        .example = "nmap -F 192.168.1.0/24"});
    c.push_back(CS{
        .name = _t("host — DNS auflösen"), .category = _t("Netzwerk"),
        .description = _t("Löst einen Namen/eine IP über DNS auf."),
        .templateText = "host {name}",
        .params = {CP{.name = "name", .label = _t("Host/IP"), .required = true}},
        .example = "host example.com"});
    c.push_back(CS{
        .name = _t("ssh — verbinden"), .category = _t("Netzwerk"),
        .description = _t("Baut eine SSH-Verbindung auf; -p Port."),
        .templateText = "ssh -p {port} {target}",
        .params = {
            CP{.name = "port", .label = _t("Port"), .defaultValue = "22"},
            CP{.name = "target", .label = _t("user@host"), .required = true},
        },
        .example = "ssh -p 22 user@host"});
    c.push_back(CS{
        .name = _t("ssh-copy-id — Schlüssel kopieren"), .category = _t("Netzwerk"),
        .description = _t("Kopiert den eigenen Public-Key auf den Server."),
        .templateText = "ssh-copy-id {target}",
        .params = {CP{.name = "target", .label = _t("user@host"), .required = true}},
        .example = "ssh-copy-id user@host"});
    c.push_back(CS{
        .name = _t("cut — Spalten ausschneiden"), .category = _t("Text"),
        .description = _t("Schneidet Felder aus; -d Trenner, -f Felder."),
        .templateText = "cut -d {delim} -f {fields} {path}",
        .params = {
            CP{.name = "delim", .label = _t("Trenner"), .defaultValue = "':'"},
            CP{.name = "fields", .label = _t("Felder"), .defaultValue = "1"},
            CP{.name = "path", .label = _t("Datei")},
        },
        .example = "cut -d ':' -f 1 /etc/passwd"});
    c.push_back(CS{
        .name = _t("uniq — Duplikate"), .category = _t("Text"),
        .description = _t("Entfernt/zählt aufeinanderfolgende Duplikate; -c zählt."),
        .templateText = "uniq {count} {path}",
        .params = {
            CP{.name = "count", .label = _t("Zählen (-c)"), .kind = "flag", .flagValue = "-c"},
            CP{.name = "path", .label = _t("Datei")},
        },
        .example = "sort datei | uniq -c"});
    c.push_back(CS{
        .name = _t("tr — Zeichen ersetzen"), .category = _t("Text"),
        .description = _t("Ersetzt/entfernt Zeichen aus der Standardeingabe."),
        .templateText = "tr {set1} {set2}",
        .params = {
            CP{.name = "set1", .label = _t("Von"), .defaultValue = "'a-z'", .required = true},
            CP{.name = "set2", .label = _t("Nach"), .defaultValue = "'A-Z'"},
        },
        .example = "echo hi | tr 'a-z' 'A-Z'"});
    c.push_back(CS{
        .name = _t("xargs — Argumente übergeben"), .category = _t("Text"),
        .description = _t("Baut aus stdin Argumente für einen Befehl."),
        .templateText = "xargs {cmd}",
        .params = {CP{.name = "cmd", .label = _t("Befehl"), .defaultValue = "rm", .required = true}},
        .example = "find . -name '*.tmp' | xargs rm"});
    c.push_back(CS{
        .name = _t("tee — Ausgabe duplizieren"), .category = _t("Text"),
        .description = _t("Schreibt stdin in Datei und stdout; -a hängt an."),
        .templateText = "tee {append} {path}",
        .params = {
            CP{.name = "append", .label = _t("Anhängen (-a)"), .kind = "flag", .flagValue = "-a"},
            CP{.name = "path", .label = _t("Datei"), .required = true},
        },
        .example = "echo x | tee -a log.txt"});
    c.push_back(CS{
        .name = _t("watch — wiederholt ausführen"), .category = _t("System"),
        .description = _t("Führt einen Befehl periodisch aus; -n Intervall."),
        .templateText = "watch -n {interval} {cmd}",
        .params = {
            CP{.name = "interval", .label = _t("Sekunden"), .defaultValue = "2"},
            CP{.name = "cmd", .label = _t("Befehl"), .defaultValue = "df -h", .required = true},
        },
        .example = "watch -n 2 df -h"});
    c.push_back(CS{
        .name = _t("nohup — im Hintergrund"), .category = _t("System"),
        .description = _t("Startet einen Prozess abgekoppelt vom Terminal."),
        .templateText = "nohup {cmd} &",
        .params = {CP{.name = "cmd", .label = _t("Befehl"), .required = true}},
        .example = "nohup ./server &"});
    c.push_back(CS{
        .name = _t("sysctl — Kernel-Parameter"), .category = _t("System"),
        .description = _t("Liest/setzt Kernel-Parameter; -a zeigt alle."),
        .templateText = "sysctl {param}",
        .params = {CP{.name = "param", .label = _t("Parameter (-a = alle)"), .defaultValue = "-a"}},
        .example = "sysctl net.ipv4.ip_forward"});
    c.push_back(CS{
        .name = _t("hostnamectl — Host/OS"), .category = _t("System"),
        .description = _t("Zeigt Hostname, Kernel und Betriebssystem."),
        .templateText = "hostnamectl", .example = "hostnamectl"});
    c.push_back(CS{
        .name = _t("timedatectl — Zeit/Zeitzone"), .category = _t("System"),
        .description = _t("Zeigt/setzt Zeit und Zeitzone."),
        .templateText = "timedatectl", .example = "timedatectl"});
    c.push_back(CS{
        .name = _t("iptables — Firewall-Regeln"), .category = _t("Netzwerk"),
        .description = _t("Zeigt die Firewall-Regeln; -L listet, -n numerisch."),
        .templateText = "sudo iptables {list} {numeric}",
        .params = {
            CP{.name = "list", .label = _t("Liste (-L)"), .kind = "flag", .defaultValue = "on", .flagValue = "-L"},
            CP{.name = "numeric", .label = _t("Numerisch (-n)"), .kind = "flag", .defaultValue = "on", .flagValue = "-n"},
        },
        .example = "sudo iptables -L -n"});
    c.push_back(CS{
        .name = _t("locate — Datei finden"), .category = _t("Suche"),
        .description = _t("Sucht Dateien schnell über die mlocate-Datenbank."),
        .templateText = "locate {pattern}",
        .params = {CP{.name = "pattern", .label = _t("Muster"), .required = true}},
        .example = "locate nginx.conf"});
    c.push_back(CS{
        .name = _t("lspci — PCI-Geräte"), .category = _t("System"),
        .description = _t("Listet PCI-Geräte (Grafik, Netzwerk …)."),
        .templateText = "lspci", .example = "lspci"});
    c.push_back(CS{
        .name = _t("lsusb — USB-Geräte"), .category = _t("System"),
        .description = _t("Listet angeschlossene USB-Geräte."),
        .templateText = "lsusb", .example = "lsusb"});
    c.push_back(CS{
        .name = _t("pip — Paket installieren"), .category = _t("Paket"),
        .description = _t("Installiert ein Python-Paket; --user ins Benutzerprofil."),
        .templateText = "pip install {user} {package}",
        .params = {
            CP{.name = "user", .label = _t("Benutzer (--user)"), .kind = "flag", .flagValue = "--user"},
            CP{.name = "package", .label = _t("Paket"), .required = true},
        },
        .example = "pip install requests", .platform = "any"});
    c.push_back(CS{
        .name = _t("python -m venv — Umgebung"), .category = _t("Paket"),
        .description = _t("Erstellt eine virtuelle Python-Umgebung."),
        .templateText = "python -m venv {path}",
        .params = {CP{.name = "path", .label = _t("Zielordner"), .defaultValue = ".venv", .required = true}},
        .example = "python -m venv .venv", .platform = "any"});
}

// --- POSIX: Server-Administration (Debian/Ubuntu + RHEL-Familie) ------------
// Bewusst nicht-interaktive Formen (--no-pager, -n …), da die Konsole kein
// volles Terminal sein muss. shutdown/reboot fehlen als eigene Befehle
// absichtlich (Neustart ueber systemctl): Katalog-Befehle gelten als
// "bekannt" und bekommen vom Parameter-Knopf ein "--help" (command_params).
void addPosixServer(std::vector<CS> &c)
{
    // --- Dienste (systemd) -------------------------------------------------
    c.push_back(CS{
        .name = _t("systemctl — Status mit Log"), .category = _t("Dienste"),
        .description = _t("Ausführlicher Dienststatus ohne Pager und ohne gekürzte Zeilen; -n legt die Zahl der Log-Zeilen fest."),
        .templateText = "systemctl status --no-pager --full {lines} {unit}",
        .params = {
            CP{.name = "lines", .label = _t("Log-Zeilen (-n)"), .defaultValue = "30", .flagValue = "-n"},
            CP{.name = "unit", .label = _t("Dienst"), .required = true},
        },
        .example = "systemctl status --no-pager --full -n 30 nginx"});
    c.push_back(CS{
        .name = _t("systemctl — Dienst neu laden"), .category = _t("Dienste"),
        .description = _t("Lädt die Konfiguration ohne Neustart neu; reload-or-restart startet neu, wenn der Dienst kein reload unterstützt."),
        .templateText = "sudo systemctl {action} {unit}",
        .params = {
            CP{.name = "action", .label = _t("Aktion"), .kind = "choice", .defaultValue = "reload", .choices = {"reload", "reload-or-restart"}},
            CP{.name = "unit", .label = _t("Dienst"), .required = true},
        },
        .example = "sudo systemctl reload nginx"});
    c.push_back(CS{
        .name = _t("systemctl — Autostart setzen"), .category = _t("Dienste"),
        .description = _t("Aktiviert/deaktiviert den Start beim Booten; --now startet bzw. stoppt den Dienst zusätzlich sofort."),
        .templateText = "sudo systemctl {action} {now} {unit}",
        .params = {
            CP{.name = "action", .label = _t("Aktion"), .kind = "choice", .defaultValue = "enable", .choices = {"enable", "disable"}},
            CP{.name = "now", .label = _t("Sofort (--now)"), .kind = "flag", .defaultValue = "on", .flagValue = "--now"},
            CP{.name = "unit", .label = _t("Dienst"), .required = true},
        },
        .example = "sudo systemctl enable --now nginx"});
    c.push_back(CS{
        .name = _t("systemctl — Units auflisten"), .category = _t("Dienste"),
        .description = _t("Listet systemd-Units eines Typs; --failed nur fehlgeschlagene, --state=running nur laufende, --all auch inaktive."),
        .templateText = "systemctl list-units --no-pager --type={type} {state}",
        .params = {
            CP{.name = "type", .label = _t("Typ"), .kind = "choice", .defaultValue = "service", .choices = {"service", "timer", "socket", "mount", "target"}},
            CP{.name = "state", .label = _t("Zustand"), .kind = "choice", .choices = {"", "--failed", "--state=running", "--all"}},
        },
        .example = "systemctl list-units --no-pager --type=service --failed"});
    c.push_back(CS{
        .name = _t("systemctl — Timer auflisten"), .category = _t("Dienste"),
        .description = _t("Zeigt systemd-Timer mit letzter und nächster Ausführung; --all auch inaktive."),
        .templateText = "systemctl list-timers --no-pager {all}",
        .params = {CP{.name = "all", .label = _t("Alle (--all)"), .kind = "flag", .flagValue = "--all"}},
        .example = "systemctl list-timers --no-pager"});
    c.push_back(CS{
        .name = _t("systemctl — Unit-Dateien neu einlesen"), .category = _t("Dienste"),
        .description = _t("daemon-reload: nötig nach dem Anlegen oder Ändern von .service- und .timer-Dateien."),
        .templateText = "sudo systemctl daemon-reload", .example = "sudo systemctl daemon-reload"});

    // --- Logs --------------------------------------------------------------
    c.push_back(CS{
        .name = _t("journalctl — Dienst-Logs (Zeitraum)"), .category = _t("Logs"),
        .description = _t("Logs eines Dienstes ohne Pager; --since/--until begrenzen den Zeitraum, -n die Zeilenzahl."),
        .templateText = "journalctl --no-pager -u {unit} {since} {until} {lines}",
        .params = {
            CP{.name = "unit", .label = _t("Dienst"), .required = true},
            CP{.name = "since", .label = _t("Seit (--since)"), .description = _t("z. B. today, yesterday, -2h oder '2024-05-01 12:00'"), .defaultValue = "today", .flagValue = "--since"},
            CP{.name = "until", .label = _t("Bis (--until)"), .description = _t("Leer = bis jetzt"), .flagValue = "--until"},
            CP{.name = "lines", .label = _t("Zeilen (-n)"), .description = _t("Leer = alle"), .flagValue = "-n"},
        },
        .example = "journalctl --no-pager -u nginx --since today -n 200"});
    c.push_back(CS{
        .name = _t("journalctl — nur Fehler"), .category = _t("Logs"),
        .description = _t("Meldungen ab einer Priorität (-p err zeigt err und schwerere); -b nur seit dem letzten Start."),
        .templateText = "journalctl --no-pager -p {prio} {boot} {since} {lines}",
        .params = {
            CP{.name = "prio", .label = _t("Priorität (-p)"), .kind = "choice", .defaultValue = "err", .choices = {"err", "warning", "crit"}},
            CP{.name = "boot", .label = _t("Nur aktueller Boot (-b)"), .kind = "flag", .defaultValue = "on", .flagValue = "-b"},
            CP{.name = "since", .label = _t("Seit (--since)"), .description = _t("z. B. today, yesterday, -2h oder '2024-05-01 12:00'"), .flagValue = "--since"},
            CP{.name = "lines", .label = _t("Zeilen (-n)"), .description = _t("Leer = alle"), .defaultValue = "100", .flagValue = "-n"},
        },
        .example = "journalctl --no-pager -p err -b -n 100"});
    c.push_back(CS{
        .name = _t("journalctl — Boot-Logs"), .category = _t("Logs"),
        .description = _t("Meldungen eines Systemstarts: 0 = aktueller, -1 = vorheriger (z. B. nach einem Absturz, braucht persistentes Journal); -k nur Kernel."),
        .templateText = "journalctl --no-pager -b {boot} {kernel} {lines}",
        .params = {
            CP{.name = "boot", .label = _t("Boot (-b)"), .kind = "choice", .defaultValue = "0", .choices = {"0", "-1", "-2"}},
            CP{.name = "kernel", .label = _t("Nur Kernel (-k)"), .kind = "flag", .flagValue = "-k"},
            CP{.name = "lines", .label = _t("Zeilen (-n)"), .description = _t("Leer = alle"), .defaultValue = "200", .flagValue = "-n"},
        },
        .example = "journalctl --no-pager -b -1 -n 200"});
    c.push_back(CS{
        .name = _t("journalctl — Journal verkleinern"), .category = _t("Logs"),
        .description = _t("Löscht archivierte Journal-Dateien bis zur Größen- bzw. Altersgrenze; die Belegung zeigt journalctl --disk-usage."),
        .templateText = "sudo journalctl --vacuum-{mode}={limit}",
        .params = {
            CP{.name = "mode", .label = _t("Grenze"), .kind = "choice", .defaultValue = "size", .choices = {"size", "time"}},
            CP{.name = "limit", .label = _t("Wert"), .description = _t("z. B. 500M bzw. 2weeks"), .defaultValue = "500M", .required = true},
        },
        .example = "sudo journalctl --vacuum-time=2weeks", .danger = true});
    c.push_back(CS{
        .name = _t("tail — Log filtern"), .category = _t("Logs"),
        .description = _t("Letzte Zeilen einer Datei, gefiltert nach Suchmuster; -f folgt live."),
        .templateText = "tail {follow} -n {lines} {path} | grep --line-buffered {ignorecase} {pattern}",
        .params = {
            CP{.name = "follow", .label = _t("Folgen (-f)"), .kind = "flag", .flagValue = "-f"},
            CP{.name = "lines", .label = _t("Zeilen"), .defaultValue = "500"},
            CP{.name = "path", .label = _t("Datei"), .required = true},
            CP{.name = "ignorecase", .label = _t("Groß/klein egal (-i)"), .kind = "flag", .defaultValue = "on", .flagValue = "-i"},
            CP{.name = "pattern", .label = _t("Suchmuster"), .defaultValue = "error", .required = true},
        },
        .example = "tail -f -n 500 /var/log/nginx/error.log | grep --line-buffered -i error"});
    c.push_back(CS{
        .name = _t("cut — häufigste Werte zählen"), .category = _t("Logs"),
        .description = _t("Zählt die Werte einer Spalte und zeigt die häufigsten zuerst, z. B. Client-IPs im Access-Log (Feld 1)."),
        .templateText = "cut -d ' ' -f {field} {path} | sort | uniq -c | sort -nr | head -n {count}",
        .params = {
            CP{.name = "field", .label = _t("Feld (-f)"), .defaultValue = "1", .required = true},
            CP{.name = "path", .label = _t("Datei"), .defaultValue = "/var/log/nginx/access.log", .required = true},
            CP{.name = "count", .label = _t("Anzahl"), .defaultValue = "20"},
        },
        .example = "cut -d ' ' -f 1 /var/log/nginx/access.log | sort | uniq -c | sort -nr | head -n 20"});
    c.push_back(CS{
        .name = _t("logrotate — Konfiguration testen"), .category = _t("Logs"),
        .description = _t("Probelauf (-d): zeigt, was rotiert würde, ohne Dateien zu ändern."),
        .templateText = "sudo logrotate -d {config}",
        .params = {CP{.name = "config", .label = _t("Konfigurationsdatei"), .defaultValue = "/etc/logrotate.conf", .required = true}},
        .example = "sudo logrotate -d /etc/logrotate.d/nginx"});
    c.push_back(CS{
        .name = _t("truncate — Datei leeren"), .category = _t("Logs"),
        .description = _t("Kürzt eine Datei auf 0 Byte (-s 0), etwa ein volles Log; die Datei bleibt bestehen und schreibende Prozesse laufen weiter."),
        .templateText = "sudo truncate -s 0 {path}",
        .params = {CP{.name = "path", .label = _t("Datei"), .required = true}},
        .example = "sudo truncate -s 0 /var/log/app/debug.log", .danger = true});

    // --- System: Prozesse / Auslastung / Cron / Benutzer -------------------
    c.push_back(CS{
        .name = _t("ps — Top-Prozesse"), .category = _t("System"),
        .description = _t("Prozesse sortiert nach CPU- oder Speicherverbrauch, größte zuerst."),
        .templateText = "ps aux --sort=-{sort} | head -n {count}",
        .params = {
            CP{.name = "sort", .label = _t("Sortierung"), .kind = "choice", .defaultValue = "%cpu", .choices = {"%cpu", "%mem"}},
            CP{.name = "count", .label = _t("Anzahl"), .defaultValue = "15"},
        },
        .example = "ps aux --sort=-%mem | head -n 15"});
    c.push_back(CS{
        .name = _t("pgrep — Prozesse suchen"), .category = _t("System"),
        .description = _t("Findet Prozesse per Name und zeigt PID und Befehlszeile (-a); -f durchsucht die ganze Befehlszeile."),
        .templateText = "pgrep -a {full} {pattern}",
        .params = {
            CP{.name = "full", .label = _t("Ganze Befehlszeile (-f)"), .kind = "flag", .flagValue = "-f"},
            CP{.name = "pattern", .label = _t("Muster"), .required = true},
        },
        .example = "pgrep -a -f gunicorn"});
    c.push_back(CS{
        .name = _t("kill — Signal senden"), .category = _t("System"),
        .description = _t("Sendet ein gewähltes Signal: TERM beendet sauber, HUP lädt bei vielen Diensten die Konfiguration neu, KILL erzwingt."),
        .templateText = "kill -s {signal} {pid}",
        .params = {
            CP{.name = "signal", .label = _t("Signal (-s)"), .kind = "choice", .defaultValue = "TERM", .choices = {"TERM", "HUP", "INT", "KILL", "USR1"}},
            CP{.name = "pid", .label = _t("PID"), .required = true},
        },
        .example = "kill -s HUP 1234", .danger = true});
    c.push_back(CS{
        .name = _t("vmstat — Auslastung im Verlauf"), .category = _t("System"),
        .description = _t("CPU, RAM, Swap und I/O alle N Sekunden; -w breite Ausgabe, -S M in MiB."),
        .templateText = "vmstat {wide} {mega} {interval} {count}",
        .params = {
            CP{.name = "wide", .label = _t("Breit (-w)"), .kind = "flag", .defaultValue = "on", .flagValue = "-w"},
            CP{.name = "mega", .label = _t("In MiB (-S M)"), .kind = "flag", .defaultValue = "on", .flagValue = "-S M"},
            CP{.name = "interval", .label = _t("Intervall (s)"), .defaultValue = "1", .required = true},
            CP{.name = "count", .label = _t("Anzahl"), .description = _t("Leer = endlos"), .defaultValue = "5"},
        },
        .example = "vmstat -w -S M 1 5"});
    c.push_back(CS{
        .name = _t("systemctl — Neustart/Herunterfahren"), .category = _t("System"),
        .description = _t("Startet den Server sofort neu bzw. schaltet ihn aus; die Verbindung bricht ab."),
        .templateText = "sudo systemctl {action}",
        .params = {CP{.name = "action", .label = _t("Aktion"), .kind = "choice", .defaultValue = "reboot", .choices = {"reboot", "poweroff"}}},
        .example = "sudo systemctl reboot", .danger = true});
    c.push_back(CS{
        .name = _t("crontab — bearbeiten"), .category = _t("System"),
        .description = _t("Öffnet die eigene Crontab im Editor (Terminal-Modus)."),
        .templateText = "crontab -e", .example = "crontab -e"});
    c.push_back(CS{
        .name = _t("cat /etc/crontab — System-Cronjobs"), .category = _t("System"),
        .description = _t("Zeigt /etc/crontab und die Skripte in den Cron-Verzeichnissen (cron.d, cron.daily …)."),
        .templateText = "cat /etc/crontab; ls -l /etc/cron.d /etc/cron.hourly /etc/cron.daily /etc/cron.weekly /etc/cron.monthly",
        .example = "cat /etc/crontab; ls -l /etc/cron.d"});
    c.push_back(CS{
        .name = _t("getent — Benutzer/Gruppen abfragen"), .category = _t("System"),
        .description = _t("Liest Einträge aus passwd, group oder hosts, auch aus LDAP/SSSD; ohne Name alle."),
        .templateText = "getent {db} {key}",
        .params = {
            CP{.name = "db", .label = _t("Datenbank"), .kind = "choice", .defaultValue = "passwd", .choices = {"passwd", "group", "hosts"}},
            CP{.name = "key", .label = _t("Name (optional)")},
        },
        .example = "getent group sudo"});
    c.push_back(CS{
        .name = _t("userdel — Benutzer löschen"), .category = _t("System"),
        .description = _t("Löscht ein Benutzerkonto; -r entfernt auch Home-Verzeichnis und Mail-Spool."),
        .templateText = "sudo userdel {home} {user}",
        .params = {
            CP{.name = "home", .label = _t("Home löschen (-r)"), .kind = "flag", .flagValue = "-r"},
            CP{.name = "user", .label = _t("Benutzer"), .required = true},
        },
        .example = "sudo userdel -r altuser", .danger = true});

    // --- Datenträger -------------------------------------------------------
    c.push_back(CS{
        .name = _t("df — Typ und Inodes"), .category = _t("Datenträger"),
        .description = _t("Plattenbelegung mit Dateisystemtyp (-T) oder Inode-Belegung (-i); -x blendet tmpfs aus."),
        .templateText = "df -h {type} {inodes} {notmp} {path}",
        .params = {
            CP{.name = "type", .label = _t("Typ (-T)"), .kind = "flag", .defaultValue = "on", .flagValue = "-T"},
            CP{.name = "inodes", .label = _t("Inodes (-i)"), .kind = "flag", .flagValue = "-i"},
            CP{.name = "notmp", .label = _t("Ohne tmpfs (-x)"), .kind = "flag", .defaultValue = "on", .flagValue = "-x tmpfs -x devtmpfs"},
            CP{.name = "path", .label = _t("Pfad")},
        },
        .example = "df -h -T -x tmpfs -x devtmpfs"});
    c.push_back(CS{
        .name = _t("du — größte Verzeichnisse"), .category = _t("Datenträger"),
        .description = _t("Größe der Unterverzeichnisse, aufsteigend sortiert (größte unten); -x bleibt im Dateisystem."),
        .templateText = "du -h --max-depth=1 {xdev} {path} 2>/dev/null | sort -h | tail -n {count}",
        .params = {
            CP{.name = "xdev", .label = _t("Nur dieses Dateisystem (-x)"), .kind = "flag", .defaultValue = "on", .flagValue = "-x"},
            CP{.name = "path", .label = _t("Pfad"), .defaultValue = "/var", .required = true},
            CP{.name = "count", .label = _t("Anzahl"), .defaultValue = "20"},
        },
        .example = "du -h --max-depth=1 -x /var 2>/dev/null | sort -h | tail -n 20"});
    c.push_back(CS{
        .name = _t("lsblk — Dateisysteme und UUIDs"), .category = _t("Datenträger"),
        .description = _t("Laufwerke und Partitionen mit Dateisystem, Label, UUID und Einhängepunkt (-f)."),
        .templateText = "lsblk -f {device}",
        .params = {CP{.name = "device", .label = _t("Gerät (optional)"), .description = _t("z. B. /dev/sda")}},
        .example = "lsblk -f"});
    c.push_back(CS{
        .name = _t("findmnt — fstab prüfen"), .category = _t("Datenträger"),
        .description = _t("Prüft /etc/fstab auf Fehler (Geräte, UUIDs, Optionen), bevor ein Neustart daran hängen bleibt."),
        .templateText = "sudo findmnt --verify {verbose}",
        .params = {CP{.name = "verbose", .label = _t("Ausführlich (--verbose)"), .kind = "flag", .flagValue = "--verbose"}},
        .example = "sudo findmnt --verify"});
    c.push_back(CS{
        .name = _t("mount — Datenträger einhängen"), .category = _t("Datenträger"),
        .description = _t("Hängt ein Gerät in ein Verzeichnis ein; -t Dateisystemtyp, -o Optionen (z. B. ro)."),
        .templateText = "sudo mount {fstype} {options} {device} {target}",
        .params = {
            CP{.name = "fstype", .label = _t("Typ (-t)"), .description = _t("Leer = automatisch"), .flagValue = "-t"},
            CP{.name = "options", .label = _t("Optionen (-o)"), .description = _t("z. B. ro oder noexec,nosuid"), .flagValue = "-o"},
            CP{.name = "device", .label = _t("Gerät"), .required = true},
            CP{.name = "target", .label = _t("Einhängepunkt"), .required = true},
        },
        .example = "sudo mount -o ro /dev/sdb1 /mnt"});
    c.push_back(CS{
        .name = _t("iostat — Datenträger-Auslastung"), .category = _t("Datenträger"),
        .description = _t("Erweiterte I/O-Statistik je Gerät (-x) ohne untätige Geräte (-z); aus dem Paket sysstat."),
        .templateText = "iostat -xz {interval} {count}",
        .params = {
            CP{.name = "interval", .label = _t("Intervall (s)"), .defaultValue = "1", .required = true},
            CP{.name = "count", .label = _t("Anzahl"), .description = _t("Leer = endlos"), .defaultValue = "3"},
        },
        .example = "iostat -xz 1 3"});

    // --- Netzwerk ----------------------------------------------------------
    c.push_back(CS{
        .name = _t("ip — Adressen kompakt"), .category = _t("Netzwerk"),
        .description = _t("Ein Interface pro Zeile mit Status und IP-Adressen (-br)."),
        .templateText = "ip -br {family} addr",
        .params = {CP{.name = "family", .label = _t("IP-Version"), .kind = "choice", .choices = {"", "-4", "-6"}}},
        .example = "ip -br -4 addr"});
    c.push_back(CS{
        .name = _t("ip — Routing-Tabelle"), .category = _t("Netzwerk"),
        .description = _t("Zeigt die Routen; mit Ziel zeigt get, über welches Interface und Gateway es erreicht wird."),
        .templateText = "ip {family} route {get}",
        .params = {
            CP{.name = "family", .label = _t("IP-Version"), .kind = "choice", .choices = {"", "-6"}},
            CP{.name = "get", .label = _t("Ziel (get)"), .description = _t("Leer = ganze Tabelle"), .flagValue = "get"},
        },
        .example = "ip route get 8.8.8.8"});
    c.push_back(CS{
        .name = _t("ss — aufgebaute Verbindungen"), .category = _t("Netzwerk"),
        .description = _t("Aktive TCP-Verbindungen mit Prozess (-p); mit sudo auch fremde Prozesse."),
        .templateText = "sudo ss -tnp state established {filter}",
        .params = {CP{.name = "filter", .label = _t("Filter (| grep …)")}},
        .example = "sudo ss -tnp state established | grep :443"});
    c.push_back(CS{
        .name = _t("lsof — Prozess an Port"), .category = _t("Netzwerk"),
        .description = _t("Zeigt, welcher Prozess einen Port belegt oder nutzt (-nP ohne Namensauflösung)."),
        .templateText = "sudo lsof -nP -i :{port}",
        .params = {CP{.name = "port", .label = _t("Port"), .required = true}},
        .example = "sudo lsof -nP -i :8080"});
    c.push_back(CS{
        .name = _t("curl — Antwortzeiten messen"), .category = _t("Netzwerk"),
        .description = _t("Misst DNS, Verbindungsaufbau, TLS, erste Antwort (TTFB) und Gesamtzeit eines Requests (-w)."),
        // Das Format steht im Vorgabewert: %{…} im Template wuerde render()
        // als Platzhalter lesen.
        .templateText = "curl -o /dev/null -sS -w {format} {url}",
        .params = {
            CP{.name = "format", .label = _t("Ausgabeformat (-w)"),
               .defaultValue = "'dns=%{time_namelookup}s connect=%{time_connect}s tls=%{time_appconnect}s ttfb=%{time_starttransfer}s total=%{time_total}s http=%{http_code}\\n'",
               .required = true},
            CP{.name = "url", .label = _t("URL"), .required = true},
        },
        .example = "curl -o /dev/null -sS -w 'total=%{time_total}s\\n' https://example.com"});
    c.push_back(CS{
        .name = _t("dig — bestimmten DNS-Server fragen"), .category = _t("Netzwerk"),
        .description = _t("Fragt einen Nameserver direkt (@server), z. B. um eine DNS-Änderung zu prüfen."),
        .templateText = "dig {short} @{server} {host} {type}",
        .params = {
            CP{.name = "short", .label = _t("Kurz (+short)"), .kind = "flag", .defaultValue = "on", .flagValue = "+short"},
            CP{.name = "server", .label = _t("DNS-Server"), .defaultValue = "1.1.1.1", .required = true},
            CP{.name = "host", .label = _t("Host"), .required = true},
            CP{.name = "type", .label = _t("Typ"), .kind = "choice", .defaultValue = "A", .choices = {"A", "AAAA", "MX", "TXT", "NS", "CNAME", "SOA"}},
        },
        .example = "dig +short @1.1.1.1 example.com MX"});
    c.push_back(CS{
        .name = _t("mtr — Route mit Paketverlust"), .category = _t("Netzwerk"),
        .description = _t("Kombiniert ping und traceroute: Verlust und Latenz je Hop als Bericht (--report, -c Durchläufe)."),
        .templateText = "mtr --report --report-wide {numeric} -c {count} {host}",
        .params = {
            CP{.name = "numeric", .label = _t("Ohne DNS (-n)"), .kind = "flag", .flagValue = "-n"},
            CP{.name = "count", .label = _t("Anzahl"), .defaultValue = "10"},
            CP{.name = "host", .label = _t("Host/IP"), .required = true},
        },
        .example = "mtr --report --report-wide -c 10 example.com"});
    c.push_back(CS{
        .name = _t("tcpdump — Pakete mitschneiden"), .category = _t("Netzwerk"),
        .description = _t("Schneidet Netzwerkverkehr mit; -c begrenzt die Paketzahl, -w schreibt eine pcap-Datei (z. B. für Wireshark)."),
        .templateText = "sudo tcpdump -i {iface} -nn {count} {write} {filter}",
        .params = {
            CP{.name = "iface", .label = _t("Interface (-i)"), .defaultValue = "any", .required = true},
            CP{.name = "count", .label = _t("Pakete (-c)"), .description = _t("Leer = bis Strg+C"), .defaultValue = "100", .flagValue = "-c"},
            CP{.name = "write", .label = _t("Datei (-w)"), .description = _t("Leer = Ausgabe in der Konsole"), .flagValue = "-w"},
            CP{.name = "filter", .label = _t("Filter"), .description = _t("z. B. port 443 oder host 10.0.0.5")},
        },
        .example = "sudo tcpdump -i any -nn -c 100 port 443"});

    // --- Sicherheit (Firewall / fail2ban / SSH) -----------------------------
    c.push_back(CS{
        .name = _t("ufw — Regel für Port/Quelle"), .category = _t("Sicherheit"),
        .description = _t("Erlaubt/sperrt einen Port gezielt für eine Quell-IP oder ein Netz; limit bremst Brute-Force (z. B. SSH)."),
        .templateText = "sudo ufw {action} from {source} to any port {port} proto {proto}",
        .params = {
            CP{.name = "action", .label = _t("Aktion"), .kind = "choice", .defaultValue = "allow", .choices = {"allow", "deny", "limit"}},
            CP{.name = "source", .label = _t("Quelle (IP/Netz)"), .description = _t("z. B. 203.0.113.0/24; any = alle"), .defaultValue = "any", .required = true},
            CP{.name = "port", .label = _t("Port"), .defaultValue = "22", .required = true},
            CP{.name = "proto", .label = _t("Protokoll (proto)"), .kind = "choice", .defaultValue = "tcp", .choices = {"tcp", "udp"}},
        },
        .example = "sudo ufw allow from 10.0.0.0/8 to any port 22 proto tcp"});
    c.push_back(CS{
        .name = _t("ufw — Regel löschen"), .category = _t("Sicherheit"),
        .description = _t("Löscht eine Regel per Nummer ohne Rückfrage (--force); die Nummern zeigt sudo ufw status numbered."),
        .templateText = "sudo ufw --force delete {num}",
        .params = {CP{.name = "num", .label = _t("Regelnummer"), .required = true}},
        .example = "sudo ufw --force delete 3", .danger = true});
    c.push_back(CS{
        .name = _t("firewall-cmd — Regeln anzeigen"), .category = _t("Sicherheit"),
        .description = _t("Zeigt Dienste, Ports und Regeln einer firewalld-Zone (RHEL/Fedora)."),
        .templateText = "sudo firewall-cmd {zone} --list-all",
        .params = {CP{.name = "zone", .label = _t("Zone (--zone)"), .description = _t("Leer = Standardzone"), .flagValue = "--zone"}},
        .example = "sudo firewall-cmd --list-all"});
    c.push_back(CS{
        .name = _t("firewall-cmd — Port/Dienst freigeben"), .category = _t("Sicherheit"),
        .description = _t("Gibt einen Port (z. B. 8080/tcp) oder Dienst (z. B. https) dauerhaft frei und lädt die Firewall neu."),
        .templateText = "sudo firewall-cmd --permanent {zone} --add-{kind}={value} && sudo firewall-cmd --reload",
        .params = {
            CP{.name = "kind", .label = _t("Art"), .kind = "choice", .defaultValue = "port", .choices = {"port", "service"}},
            CP{.name = "value", .label = _t("Port/Dienst"), .defaultValue = "8080/tcp", .required = true},
            CP{.name = "zone", .label = _t("Zone (--zone)"), .description = _t("Leer = Standardzone"), .flagValue = "--zone"},
        },
        .example = "sudo firewall-cmd --permanent --add-service=https && sudo firewall-cmd --reload"});
    c.push_back(CS{
        .name = _t("nft — Regelsatz anzeigen"), .category = _t("Sicherheit"),
        .description = _t("Zeigt den kompletten nftables-Regelsatz, auch die von firewalld oder iptables-nft erzeugten Regeln."),
        .templateText = "sudo nft list ruleset", .example = "sudo nft list ruleset"});
    c.push_back(CS{
        .name = _t("fail2ban-client — Status"), .category = _t("Sicherheit"),
        .description = _t("Zeigt die aktiven Jails; mit Jail-Name auch gesperrte IPs und Fehlversuche."),
        .templateText = "sudo fail2ban-client status {jail}",
        .params = {CP{.name = "jail", .label = _t("Jail (optional)"), .description = _t("z. B. sshd")}},
        .example = "sudo fail2ban-client status sshd"});
    c.push_back(CS{
        .name = _t("fail2ban-client — IP entsperren"), .category = _t("Sicherheit"),
        .description = _t("Hebt die Sperre einer IP-Adresse in einem Jail auf."),
        .templateText = "sudo fail2ban-client set {jail} unbanip {ip}",
        .params = {
            CP{.name = "jail", .label = _t("Jail"), .defaultValue = "sshd", .required = true},
            CP{.name = "ip", .label = _t("IP-Adresse"), .required = true},
        },
        .example = "sudo fail2ban-client set sshd unbanip 203.0.113.7"});
    c.push_back(CS{
        .name = _t("journalctl — fehlgeschlagene SSH-Logins"), .category = _t("Sicherheit"),
        .description = _t("Filtert das SSH-Log nach fehlgeschlagenen Anmeldungen; der Dienst heißt ssh (Debian/Ubuntu) bzw. sshd (RHEL)."),
        .templateText = "sudo journalctl -u {unit} --no-pager {since} | grep -E {pattern}",
        .params = {
            CP{.name = "unit", .label = _t("Dienst"), .kind = "choice", .defaultValue = "ssh", .choices = {"ssh", "sshd"}},
            CP{.name = "since", .label = _t("Seit (--since)"), .description = _t("z. B. today, yesterday, -2h oder '2024-05-01 12:00'"), .defaultValue = "today", .flagValue = "--since"},
            CP{.name = "pattern", .label = _t("Suchmuster"), .defaultValue = "'Failed password|Invalid user|authentication failure'", .required = true},
        },
        .example = "sudo journalctl -u ssh --no-pager --since today | grep -E 'Failed password|Invalid user'"});
    c.push_back(CS{
        .name = _t("sshd — Konfiguration testen"), .category = _t("Sicherheit"),
        .description = _t("-t prüft sshd_config auf Fehler (vor jedem Neustart von sshd!), -T zeigt die wirksame Konfiguration."),
        .templateText = "sudo sshd {mode}",
        .params = {CP{.name = "mode", .label = _t("Modus"), .kind = "choice", .defaultValue = "-t", .choices = {"-t", "-T"}}},
        .example = "sudo sshd -t"});

    // --- Paketverwaltung ---------------------------------------------------
    c.push_back(CS{
        .name = _t("apt — aktualisierbare Pakete"), .category = _t("Paket"),
        .description = _t("Listet Pakete, für die Updates bereitstehen (vorher apt update ausführen)."),
        .templateText = "apt list --upgradable", .example = "apt list --upgradable"});
    c.push_back(CS{
        .name = _t("apt — suchen/Infos"), .category = _t("Paket"),
        .description = _t("search sucht Pakete, show zeigt Details, policy installierte und verfügbare Versionen."),
        .templateText = "apt {action} {package}",
        .params = {
            CP{.name = "action", .label = _t("Aktion"), .kind = "choice", .defaultValue = "search", .choices = {"search", "show", "policy"}},
            CP{.name = "package", .label = _t("Paket/Suchbegriff"), .required = true},
        },
        .example = "apt policy nginx"});
    c.push_back(CS{
        .name = _t("apt — entfernen"), .category = _t("Paket"),
        .description = _t("Entfernt ein Paket; purge löscht zusätzlich seine Konfigurationsdateien."),
        .templateText = "sudo apt {mode} -y {package}",
        .params = {
            CP{.name = "mode", .label = _t("Modus"), .kind = "choice", .defaultValue = "remove", .choices = {"remove", "purge"}},
            CP{.name = "package", .label = _t("Paket"), .required = true},
        },
        .example = "sudo apt remove -y apache2", .danger = true});
    c.push_back(CS{
        .name = _t("apt — aufräumen"), .category = _t("Paket"),
        .description = _t("Entfernt nicht mehr benötigte Abhängigkeiten und alte Kernel (autoremove); --purge auch deren Konfiguration."),
        .templateText = "sudo apt autoremove {purge} -y",
        .params = {CP{.name = "purge", .label = _t("Mit Konfiguration (--purge)"), .kind = "flag", .flagValue = "--purge"}},
        .example = "sudo apt autoremove -y", .danger = true});
    c.push_back(CS{
        .name = _t("dpkg — installierte Pakete"), .category = _t("Paket"),
        .description = _t("Listet installierte Debian-Pakete mit Version, gefiltert nach Suchbegriff."),
        .templateText = "dpkg -l | grep -i {pattern}",
        .params = {CP{.name = "pattern", .label = _t("Suchbegriff"), .required = true}},
        .example = "dpkg -l | grep -i php"});
    c.push_back(CS{
        .name = _t("dnf — Updates prüfen/installieren"), .category = _t("Paket"),
        .description = _t("check-update listet verfügbare Updates (Exit-Code 100 = Updates vorhanden), upgrade installiert sie; ohne Paket alle."),
        .templateText = "sudo dnf {action} {package}",
        .params = {
            CP{.name = "action", .label = _t("Aktion"), .kind = "choice", .defaultValue = "check-update", .choices = {"check-update", "upgrade -y"}},
            CP{.name = "package", .label = _t("Paket (optional)")},
        },
        .example = "sudo dnf upgrade -y"});
    c.push_back(CS{
        .name = _t("dnf — suchen/Infos"), .category = _t("Paket"),
        .description = _t("search sucht Pakete, info zeigt Details, provides findet das Paket zu einer Datei."),
        .templateText = "dnf {action} {package}",
        .params = {
            CP{.name = "action", .label = _t("Aktion"), .kind = "choice", .defaultValue = "search", .choices = {"search", "info", "provides"}},
            CP{.name = "package", .label = _t("Paket/Suchbegriff"), .required = true},
        },
        .example = "dnf provides /usr/bin/dig"});
    c.push_back(CS{
        .name = _t("dnf — entfernen"), .category = _t("Paket"),
        .description = _t("Entfernt ein Paket samt nicht mehr benötigter Abhängigkeiten (RHEL/Fedora)."),
        .templateText = "sudo dnf remove -y {package}",
        .params = {CP{.name = "package", .label = _t("Paket"), .required = true}},
        .example = "sudo dnf remove -y httpd", .danger = true});
    c.push_back(CS{
        .name = _t("rpm — installierte Pakete"), .category = _t("Paket"),
        .description = _t("Listet installierte RPM-Pakete, gefiltert nach Suchbegriff (RHEL/Fedora/SUSE)."),
        .templateText = "rpm -qa | grep -i {pattern}",
        .params = {CP{.name = "pattern", .label = _t("Suchbegriff"), .required = true}},
        .example = "rpm -qa | grep -i kernel"});

    // --- Webserver / TLS ---------------------------------------------------
    c.push_back(CS{
        .name = _t("nginx — testen und neu laden"), .category = _t("Webserver"),
        .description = _t("Prüft die Konfiguration und lädt nginx nur bei Erfolg neu, ohne laufende Verbindungen zu trennen."),
        .templateText = "sudo nginx -t && sudo systemctl reload nginx",
        .example = "sudo nginx -t && sudo systemctl reload nginx"});
    c.push_back(CS{
        .name = _t("apache2ctl — Konfiguration prüfen (Debian/Ubuntu)"), .category = _t("Webserver"),
        .description = _t("configtest prüft die Syntax, -S zeigt die VirtualHosts, -M die geladenen Module."),
        .templateText = "sudo apache2ctl {action}",
        .params = {CP{.name = "action", .label = _t("Aktion"), .kind = "choice", .defaultValue = "configtest", .choices = {"configtest", "-S", "-M"}}},
        .example = "sudo apache2ctl -S"});
    c.push_back(CS{
        .name = _t("httpd — Konfiguration prüfen (RHEL)"), .category = _t("Webserver"),
        .description = _t("-t prüft die Syntax, -S zeigt die VirtualHosts, -M die geladenen Module."),
        .templateText = "sudo httpd {action}",
        .params = {CP{.name = "action", .label = _t("Aktion"), .kind = "choice", .defaultValue = "-t", .choices = {"-t", "-S", "-M"}}},
        .example = "sudo httpd -t"});
    c.push_back(CS{
        .name = _t("certbot — Zertifikate/Erneuerung"), .category = _t("Webserver"),
        .description = _t("certificates listet die Let's-Encrypt-Zertifikate mit Ablaufdatum, renew --dry-run testet die Erneuerung."),
        .templateText = "sudo certbot {action}",
        .params = {CP{.name = "action", .label = _t("Aktion"), .kind = "choice", .defaultValue = "certificates", .choices = {"certificates", "renew --dry-run"}}},
        .example = "sudo certbot renew --dry-run"});
    c.push_back(CS{
        .name = _t("openssl x509 — Zertifikatsdatei anzeigen"), .category = _t("Webserver"),
        .description = _t("Liest ein PEM-Zertifikat: Laufzeit, Inhaber/Aussteller, Domains (SAN) oder alles (-text)."),
        .templateText = "openssl x509 -in {path} -noout {mode}",
        .params = {
            CP{.name = "path", .label = _t("Datei"), .description = _t("z. B. /etc/letsencrypt/live/DOMAIN/cert.pem"), .required = true},
            CP{.name = "mode", .label = _t("Anzeige"), .kind = "choice", .defaultValue = "-dates", .choices = {"-dates", "-subject -issuer", "-ext subjectAltName", "-text"}},
        },
        .example = "openssl x509 -in cert.pem -noout -dates"});
    c.push_back(CS{
        .name = _t("openssl — Server-Zertifikat mit SNI"), .category = _t("Webserver"),
        .description = _t("Holt das Zertifikat eines Servers mit SNI (-servername, nötig bei mehreren Domains auf einer IP) und zeigt Laufzeit, Inhaber oder Domains."),
        .templateText = "echo | openssl s_client -connect {host}:{port} -servername {host} 2>/dev/null | openssl x509 -noout {mode}",
        .params = {
            CP{.name = "host", .label = _t("Host"), .required = true},
            CP{.name = "port", .label = _t("Port"), .defaultValue = "443"},
            CP{.name = "mode", .label = _t("Anzeige"), .kind = "choice", .defaultValue = "-dates", .choices = {"-dates", "-subject -issuer", "-ext subjectAltName", "-text"}},
        },
        .example = "echo | openssl s_client -connect example.com:443 -servername example.com 2>/dev/null | openssl x509 -noout -dates"});
    c.push_back(CS{
        .name = _t("curl — Server per IP testen"), .category = _t("Webserver"),
        .description = _t("Ruft eine Domain per HTTPS auf einer bestimmten IP ab, ohne DNS zu ändern (--resolve), z. B. vor einem Umzug; -k ignoriert Zertifikatsfehler."),
        .templateText = "curl -sS {head} {insecure} --resolve {host}:{port}:{ip} https://{host}:{port}/",
        .params = {
            CP{.name = "head", .label = _t("Nur Header (-I)"), .kind = "flag", .defaultValue = "on", .flagValue = "-I"},
            CP{.name = "insecure", .label = _t("Zertifikat ignorieren (-k)"), .kind = "flag", .flagValue = "-k"},
            CP{.name = "host", .label = _t("Host"), .required = true},
            CP{.name = "ip", .label = _t("IP-Adresse"), .required = true},
            CP{.name = "port", .label = _t("Port"), .defaultValue = "443", .required = true},
        },
        .example = "curl -sS -I --resolve example.com:443:203.0.113.10 https://example.com:443/"});

    // --- Datenbank ---------------------------------------------------------
    c.push_back(CS{
        .name = _t("mysqldump — Datenbank sichern"), .category = _t("Datenbank"),
        .description = _t("Schreibt einen SQL-Dump in eine Datei und überschreibt sie; --single-transaction sichert InnoDB ohne Tabellensperren."),
        .templateText = "mysqldump {single} {user} {password} {db} > {file}",
        .params = {
            CP{.name = "single", .label = _t("Ohne Sperren (--single-transaction)"), .kind = "flag", .defaultValue = "on", .flagValue = "--single-transaction"},
            CP{.name = "user", .label = _t("Benutzer (-u)"), .description = _t("Leer = aktueller Benutzer"), .flagValue = "-u"},
            CP{.name = "password", .label = _t("Passwort abfragen (-p, Terminal-Modus)"), .kind = "flag", .flagValue = "-p"},
            CP{.name = "db", .label = _t("Datenbank"), .required = true},
            CP{.name = "file", .label = _t("Zieldatei"), .defaultValue = "backup.sql", .required = true},
        },
        .example = "mysqldump --single-transaction -u root -p shop > shop.sql", .danger = true});
    c.push_back(CS{
        .name = _t("mysql — SQL ausführen"), .category = _t("Datenbank"),
        .description = _t("Führt eine SQL-Anweisung aus (-e) und beendet sich, etwa SHOW DATABASES oder SHOW PROCESSLIST."),
        .templateText = "mysql {user} {password} -e {query} {db}",
        .params = {
            CP{.name = "user", .label = _t("Benutzer (-u)"), .description = _t("Leer = aktueller Benutzer"), .flagValue = "-u"},
            CP{.name = "password", .label = _t("Passwort abfragen (-p, Terminal-Modus)"), .kind = "flag", .flagValue = "-p"},
            CP{.name = "query", .label = _t("SQL (-e)"), .defaultValue = "\"SHOW DATABASES;\"", .required = true},
            CP{.name = "db", .label = _t("Datenbank (optional)")},
        },
        .example = "mysql -u root -p -e \"SHOW PROCESSLIST;\""});
    c.push_back(CS{
        .name = _t("mysql — Dump einspielen"), .category = _t("Datenbank"),
        .description = _t("Spielt eine SQL-Datei in eine Datenbank ein; vorhandene Tabellen können dabei überschrieben werden."),
        .templateText = "mysql {user} {password} {db} < {file}",
        .params = {
            CP{.name = "user", .label = _t("Benutzer (-u)"), .description = _t("Leer = aktueller Benutzer"), .flagValue = "-u"},
            CP{.name = "password", .label = _t("Passwort abfragen (-p, Terminal-Modus)"), .kind = "flag", .flagValue = "-p"},
            CP{.name = "db", .label = _t("Datenbank"), .required = true},
            CP{.name = "file", .label = _t("SQL-Datei"), .required = true},
        },
        .example = "mysql -u root -p shop < shop.sql", .danger = true});
    c.push_back(CS{
        .name = _t("pg_dump — Datenbank sichern"), .category = _t("Datenbank"),
        .description = _t("Sichert eine PostgreSQL-Datenbank als Benutzer postgres in eine Datei und überschreibt sie; -Fc erzeugt das komprimierte Format für pg_restore."),
        .templateText = "sudo -u postgres pg_dump {format} {db} > {file}",
        .params = {
            CP{.name = "format", .label = _t("Format"), .kind = "choice", .choices = {"", "-Fc"}},
            CP{.name = "db", .label = _t("Datenbank"), .required = true},
            CP{.name = "file", .label = _t("Zieldatei"), .defaultValue = "backup.sql", .required = true},
        },
        .example = "sudo -u postgres pg_dump -Fc shop > shop.dump", .danger = true});
    c.push_back(CS{
        .name = _t("psql — SQL ausführen"), .category = _t("Datenbank"),
        .description = _t("Führt eine SQL-Anweisung als Benutzer postgres aus (-c), optional in einer bestimmten Datenbank (-d)."),
        .templateText = "sudo -u postgres psql {db} -c {query}",
        .params = {
            CP{.name = "db", .label = _t("Datenbank (-d)"), .description = _t("Leer = postgres"), .flagValue = "-d"},
            CP{.name = "query", .label = _t("SQL (-c)"), .defaultValue = "\"SELECT datname FROM pg_database;\"", .required = true},
        },
        .example = "sudo -u postgres psql -d shop -c \"SELECT count(*) FROM orders;\""});
    c.push_back(CS{
        .name = _t("redis-cli — Befehl senden"), .category = _t("Datenbank"),
        .description = _t("Sendet einen Befehl an Redis, z. B. ping, info memory oder dbsize; -h/-p für einen anderen Server."),
        .templateText = "redis-cli {host} {port} {cmd}",
        .params = {
            CP{.name = "host", .label = _t("Host (-h)"), .description = _t("Leer = localhost"), .flagValue = "-h"},
            CP{.name = "port", .label = _t("Port (-p)"), .description = _t("Leer = 6379"), .flagValue = "-p"},
            CP{.name = "cmd", .label = _t("Befehl"), .defaultValue = "ping", .required = true},
        },
        .example = "redis-cli info memory"});

    // --- Suche / Archive / Dateien (weitere) --------------------------------
    c.push_back(CS{
        .name = _t("find — große Dateien"), .category = _t("Suche"),
        .description = _t("Findet Dateien ab einer Mindestgröße (z. B. 100M, 1G), größte zuerst; -xdev bleibt im Dateisystem."),
        .templateText = "find {path} -xdev -type f -size +{size} -print0 2>/dev/null | du -h --files0-from=- | sort -rh | head -n {count}",
        .params = {
            CP{.name = "path", .label = _t("Startpfad"), .defaultValue = "/", .required = true},
            CP{.name = "size", .label = _t("Mindestgröße"), .defaultValue = "100M", .required = true},
            CP{.name = "count", .label = _t("Anzahl"), .defaultValue = "20"},
        },
        .example = "find /var -xdev -type f -size +100M -print0 2>/dev/null | du -h --files0-from=- | sort -rh | head -n 20"});
    c.push_back(CS{
        .name = _t("find — alte Dateien finden/löschen"), .category = _t("Suche"),
        .description = _t("Findet Dateien nach Muster, die älter als N Tage sind (-mtime +N); mit -delete werden sie gelöscht, daher erst ohne prüfen."),
        .templateText = "find {path} -type f -name {pattern} -mtime +{days} {delete}",
        .params = {
            CP{.name = "path", .label = _t("Startpfad"), .required = true},
            CP{.name = "pattern", .label = _t("Namensmuster"), .defaultValue = "\"*.log\"", .required = true},
            CP{.name = "days", .label = _t("Älter als (Tage)"), .defaultValue = "30", .required = true},
            CP{.name = "delete", .label = _t("Löschen (-delete)"), .kind = "flag", .flagValue = "-print -delete"},
        },
        .example = "find /var/backups -type f -name \"*.gz\" -mtime +30 -print -delete", .danger = true});
    c.push_back(CS{
        .name = _t("grep — rekursiv mit Dateifilter"), .category = _t("Suche"),
        .description = _t("Durchsucht nur passende Dateien (--include, z. B. '*.conf'); -l listet nur die Dateinamen."),
        .templateText = "grep -rn {ignorecase} {names} {include} {pattern} {path}",
        .params = {
            CP{.name = "ignorecase", .label = _t("Groß/klein egal (-i)"), .kind = "flag", .flagValue = "-i"},
            CP{.name = "names", .label = _t("Nur Dateinamen (-l)"), .kind = "flag", .flagValue = "-l"},
            CP{.name = "include", .label = _t("Dateimuster (--include)"), .defaultValue = "'*.conf'", .flagValue = "--include"},
            CP{.name = "pattern", .label = _t("Suchmuster"), .required = true},
            CP{.name = "path", .label = _t("Pfad"), .defaultValue = "/etc", .required = true},
        },
        .example = "grep -rn --include '*.conf' listen /etc/nginx"});
    c.push_back(CS{
        .name = _t("tar — Inhalt anzeigen"), .category = _t("Archive"),
        .description = _t("Listet den Inhalt eines Archivs ausführlich (-tvf); die Kompression wird automatisch erkannt."),
        .templateText = "tar -tvf {archive}",
        .params = {CP{.name = "archive", .label = _t("Archiv"), .required = true}},
        .example = "tar -tvf backup.tar.gz"});
    c.push_back(CS{
        .name = _t("tar — entpacken (automatisch)"), .category = _t("Archive"),
        .description = _t("Entpackt .tar, .tar.gz, .tar.xz oder .tar.bz2 mit automatisch erkannter Kompression; -C wählt den Zielordner."),
        .templateText = "tar -xf {archive} {dest}",
        .params = {
            CP{.name = "archive", .label = _t("Archiv"), .required = true},
            CP{.name = "dest", .label = _t("Zielordner (-C)"), .description = _t("Muss existieren; leer = aktuelles Verzeichnis"), .flagValue = "-C"},
        },
        .example = "tar -xf backup.tar.xz -C /tmp/restore"});
    c.push_back(CS{
        .name = _t("rsync — mit Probelauf und SSH-Port"), .category = _t("Dateien"),
        .description = _t("Synchronisiert über SSH mit eigenem Port; -n zeigt nur, was passieren würde, --delete löscht am Ziel, was in der Quelle fehlt."),
        .templateText = "rsync -avzh {dryrun} {progress} {delete} -e 'ssh -p {port}' {src} {dst}",
        .params = {
            CP{.name = "dryrun", .label = _t("Probelauf (-n)"), .kind = "flag", .defaultValue = "on", .flagValue = "-n"},
            CP{.name = "progress", .label = _t("Fortschritt (--progress)"), .kind = "flag", .flagValue = "--progress"},
            CP{.name = "delete", .label = _t("Spiegeln (--delete)"), .kind = "flag", .flagValue = "--delete"},
            CP{.name = "port", .label = _t("SSH-Port"), .defaultValue = "22", .required = true},
            CP{.name = "src", .label = _t("Quelle"), .required = true},
            CP{.name = "dst", .label = _t("Ziel (user@host:/pfad)"), .required = true},
        },
        .example = "rsync -avzh -n -e 'ssh -p 2222' ./site/ user@host:/var/www/site/", .danger = true});
}

// --- Windows: weitere Befehle (cmd) -----------------------------------------
void addWindowsCmdMore(std::vector<CS> &c)
{
    c.push_back(CS{
        .name = _t("copy — kopieren"), .category = _t("Dateien"),
        .description = _t("Kopiert eine oder mehrere Dateien."),
        .templateText = "copy {src} {dst}",
        .params = {
            CP{.name = "src", .label = _t("Quelle"), .required = true},
            CP{.name = "dst", .label = _t("Ziel"), .required = true},
        },
        .example = "copy a.txt D:\\backup\\", .platform = "windows"});
    c.push_back(CS{
        .name = _t("rd — Verzeichnis löschen"), .category = _t("Dateien"),
        .description = _t("Entfernt ein Verzeichnis; /S rekursiv, /Q ohne Nachfrage."),
        .templateText = "rd {recursive} {quiet} {path}",
        .params = {
            CP{.name = "recursive", .label = _t("Rekursiv (/S)"), .kind = "flag", .flagValue = "/S"},
            CP{.name = "quiet", .label = _t("Ohne Nachfrage (/Q)"), .kind = "flag", .flagValue = "/Q"},
            CP{.name = "path", .label = _t("Pfad"), .required = true},
        },
        .example = "rd /S /Q C:\\temp", .danger = true, .platform = "windows"});
    c.push_back(CS{
        .name = _t("more — seitenweise anzeigen"), .category = _t("Dateien"),
        .description = _t("Zeigt eine Datei seitenweise an."),
        .templateText = "more {path}",
        .params = {CP{.name = "path", .label = _t("Datei"), .required = true}},
        .example = "more log.txt", .platform = "windows"});
    c.push_back(CS{
        .name = _t("mklink — Verknüpfung"), .category = _t("Dateien"),
        .description = _t("Erstellt einen Link; /D Verzeichnis-Symlink, /H Hardlink, /J Junction."),
        .templateText = "mklink {mode} {link} {target}",
        .params = {
            CP{.name = "mode", .label = _t("Typ"), .kind = "choice", .defaultValue = "", .choices = {"", "/D", "/H", "/J"}},
            CP{.name = "link", .label = _t("Linkname"), .required = true},
            CP{.name = "target", .label = _t("Ziel"), .required = true},
        },
        .example = "mklink /D app C:\\apps\\app", .platform = "windows"});
    c.push_back(CS{
        .name = _t("certutil — Hash/Download"), .category = _t("Dateien"),
        .description = _t("Berechnet Hashwerte (-hashfile) oder lädt Dateien (-urlcache)."),
        .templateText = "certutil -hashfile {path} {algo}",
        .params = {
            CP{.name = "path", .label = _t("Datei"), .required = true},
            CP{.name = "algo", .label = _t("Algorithmus"), .kind = "choice", .defaultValue = "SHA256", .choices = {"SHA256", "SHA1", "MD5"}},
        },
        .example = "certutil -hashfile setup.exe SHA256", .platform = "windows"});
    c.push_back(CS{
        .name = _t("tar — entpacken/packen"), .category = _t("Archive"),
        .description = _t("Windows 10+ enthält tar; -x entpackt, -c packt."),
        .templateText = "tar {mode} -f {archive} {target}",
        .params = {
            CP{.name = "mode", .label = _t("Modus"), .kind = "choice", .defaultValue = "-xz", .choices = {"-xz", "-cz", "-x", "-c"}},
            CP{.name = "archive", .label = _t("Archiv"), .required = true},
            CP{.name = "target", .label = _t("Quelle/Ziel")},
        },
        .example = "tar -xz -f archiv.tar.gz", .platform = "windows"});
    c.push_back(CS{
        .name = _t("curl — HTTP-Request"), .category = _t("Netzwerk"),
        .description = _t("Windows 10+ enthält curl; -L folgt Redirects, -o speichert."),
        .templateText = "curl {follow} {url} {output}",
        .params = {
            CP{.name = "follow", .label = _t("Redirects (-L)"), .kind = "flag", .defaultValue = "on", .flagValue = "-L"},
            CP{.name = "url", .label = _t("URL"), .required = true},
            CP{.name = "output", .label = _t("-o datei")},
        },
        .example = "curl -L https://example.com", .platform = "windows"});
    c.push_back(CS{
        .name = _t("winget — Paket installieren"), .category = _t("Paket"),
        .description = _t("Installiert ein Paket über den Windows Package Manager."),
        .templateText = "winget install {package}",
        .params = {CP{.name = "package", .label = _t("Paket/ID"), .required = true}},
        .example = "winget install Git.Git", .platform = "windows"});
    c.push_back(CS{
        .name = _t("wmic — Systeminfo (WMI)"), .category = _t("System"),
        .description = _t("Fragt Systeminformationen ab (Seriennummer, Datenträger, CPU …)."),
        .templateText = "wmic {query}",
        .params = {CP{.name = "query", .label = _t("Abfrage"), .kind = "choice", .defaultValue = "bios get serialnumber",
                      .choices = {"bios get serialnumber", "diskdrive get model,size",
                                  "cpu get name", "os get caption,version", "process list brief"}}},
        .example = "wmic bios get serialnumber", .platform = "windows"});
    c.push_back(CS{
        .name = _t("forfiles — Dateien nach Alter"), .category = _t("Dateien"),
        .description = _t("Findet Dateien nach Alter; /D Datum, /M Maske."),
        .templateText = "forfiles /P {path} /M {mask} /D -{days} /C {cmd}",
        .params = {
            CP{.name = "path", .label = _t("Pfad"), .defaultValue = "C:\\logs", .required = true},
            CP{.name = "mask", .label = _t("Maske"), .defaultValue = "*.log"},
            CP{.name = "days", .label = _t("Älter als (Tage)"), .defaultValue = "30"},
            CP{.name = "cmd", .label = _t("/C Befehl"), .defaultValue = "\"cmd /c del @path\""},
        },
        .example = "forfiles /P C:\\logs /M *.log /D -30 /C \"cmd /c del @path\"",
        .danger = true, .platform = "windows"});
    c.push_back(CS{
        .name = _t("wevtutil — Ereignisprotokolle"), .category = _t("System"),
        .description = _t("Liest Windows-Ereignisprotokolle; qe Abfrage, /c Anzahl."),
        .templateText = "wevtutil qe {log} /c:{count} /rd:true /f:text",
        .params = {
            CP{.name = "log", .label = _t("Protokoll"), .kind = "choice", .defaultValue = "System", .choices = {"System", "Application", "Security"}},
            CP{.name = "count", .label = _t("Anzahl"), .defaultValue = "20"},
        },
        .example = "wevtutil qe System /c:20 /rd:true /f:text", .platform = "windows"});
    c.push_back(CS{
        .name = _t("net use — Netzlaufwerk"), .category = _t("Netzwerk"),
        .description = _t("Verbindet/trennt ein Netzlaufwerk."),
        .templateText = "net use {drive} {share}",
        .params = {
            CP{.name = "drive", .label = _t("Laufwerk"), .defaultValue = "Z:"},
            CP{.name = "share", .label = _t("Freigabe (\\\\server\\share)"), .required = true},
        },
        .example = "net use Z: \\\\server\\share", .platform = "windows"});
    c.push_back(CS{
        .name = _t("net share — Freigaben"), .category = _t("Netzwerk"),
        .description = _t("Listet/erstellt Dateifreigaben."),
        .templateText = "net share {name}",
        .params = {CP{.name = "name", .label = _t("Name (optional)")}},
        .example = "net share", .platform = "windows"});
    c.push_back(CS{
        .name = _t("net localgroup — Gruppen"), .category = _t("System"),
        .description = _t("Listet lokale Gruppen oder deren Mitglieder."),
        .templateText = "net localgroup {group}",
        .params = {CP{.name = "group", .label = _t("Gruppe (optional)")}},
        .example = "net localgroup Administratoren", .platform = "windows"});
    c.push_back(CS{
        .name = _t("net view — Netzwerkrechner"), .category = _t("Netzwerk"),
        .description = _t("Zeigt Rechner/Freigaben im Netzwerk."),
        .templateText = "net view {host}",
        .params = {CP{.name = "host", .label = _t("\\\\Host (optional)")}},
        .example = "net view", .platform = "windows"});
    c.push_back(CS{
        .name = _t("pathping — Route + Latenz"), .category = _t("Netzwerk"),
        .description = _t("Kombiniert tracert und ping (Paketverlust pro Hop)."),
        .templateText = "pathping {host}",
        .params = {CP{.name = "host", .label = _t("Host"), .required = true}},
        .example = "pathping example.com", .platform = "windows"});
    c.push_back(CS{
        .name = _t("quser — angemeldete Sitzungen"), .category = _t("System"),
        .description = _t("Zeigt angemeldete Benutzer-Sitzungen."),
        .templateText = "quser", .example = "quser", .platform = "windows"});
    c.push_back(CS{
        .name = _t("gpresult — Richtlinien"), .category = _t("System"),
        .description = _t("Zeigt angewandte Gruppenrichtlinien; /r Zusammenfassung."),
        .templateText = "gpresult /r", .example = "gpresult /r", .platform = "windows"});
    c.push_back(CS{
        .name = _t("timeout — warten"), .category = _t("System"),
        .description = _t("Wartet N Sekunden (oder bis Tastendruck)."),
        .templateText = "timeout /t {seconds} {nobreak}",
        .params = {
            CP{.name = "seconds", .label = _t("Sekunden"), .defaultValue = "5"},
            CP{.name = "nobreak", .label = _t("Kein Abbruch (/nobreak)"), .kind = "flag", .defaultValue = "on", .flagValue = "/nobreak"},
        },
        .example = "timeout /t 5 /nobreak", .platform = "windows"});
    c.push_back(CS{
        .name = _t("assoc — Dateizuordnung"), .category = _t("System"),
        .description = _t("Zeigt/ändert die Zuordnung einer Dateiendung."),
        .templateText = "assoc {ext}",
        .params = {CP{.name = "ext", .label = _t("Endung (z.B. .txt)")}},
        .example = "assoc .txt", .platform = "windows"});
    c.push_back(CS{
        .name = _t("cipher — verschlüsseln/löschen"), .category = _t("Rechte"),
        .description = _t("EFS-Verschlüsselung; /E verschlüsseln, /D entschlüsseln, /W löscht freien Platz sicher."),
        .templateText = "cipher {mode} {path}",
        .params = {
            CP{.name = "mode", .label = _t("Aktion"), .kind = "choice", .defaultValue = "/C", .choices = {"/E", "/D", "/C", "/W"}},
            CP{.name = "path", .label = _t("Pfad"), .required = true},
        },
        .example = "cipher /E C:\\geheim", .platform = "windows"});
    c.push_back(CS{
        .name = _t("reg add — Registry schreiben"), .category = _t("System"),
        .description = _t("Schreibt einen Registry-Wert. VORSICHT!"),
        .templateText = "reg add {key} /v {name} /t {type} /d {data} /f",
        .params = {
            CP{.name = "key", .label = _t("Schlüssel"), .required = true},
            CP{.name = "name", .label = _t("Wertname"), .required = true},
            CP{.name = "type", .label = _t("Typ"), .kind = "choice", .defaultValue = "REG_SZ", .choices = {"REG_SZ", "REG_DWORD", "REG_EXPAND_SZ"}},
            CP{.name = "data", .label = _t("Daten"), .required = true},
        },
        .example = "reg add HKCU\\Software\\App /v Mode /t REG_DWORD /d 1 /f",
        .danger = true, .platform = "windows"});
    c.push_back(CS{
        .name = _t("msiexec — MSI installieren"), .category = _t("Paket"),
        .description = _t("Installiert ein MSI-Paket; /i installiert, /qn ohne Oberfläche."),
        .templateText = "msiexec /i {package} {silent}",
        .params = {
            CP{.name = "package", .label = _t("MSI-Datei"), .required = true},
            CP{.name = "silent", .label = _t("Ohne UI (/qn)"), .kind = "flag", .flagValue = "/qn"},
        },
        .example = "msiexec /i setup.msi /qn", .platform = "windows"});
}

// --- Windows: weitere Befehle (PowerShell) ----------------------------------
void addWindowsPowerShell(std::vector<CS> &c)
{
    c.push_back(CS{
        .name = _t("Test-NetConnection (PowerShell)"), .category = _t("Netzwerk"),
        .description = _t("Prüft Erreichbarkeit/Port eines Hosts."),
        .templateText = "powershell -Command \"Test-NetConnection {host} -Port {port}\"",
        .params = {
            CP{.name = "host", .label = _t("Host"), .required = true},
            CP{.name = "port", .label = _t("Port"), .defaultValue = "443"},
        },
        .example = "powershell -Command \"Test-NetConnection example.com -Port 443\"", .platform = "windows"});
    c.push_back(CS{
        .name = _t("Get-FileHash (PowerShell)"), .category = _t("Dateien"),
        .description = _t("Berechnet den Hash einer Datei."),
        .templateText = "powershell -Command \"Get-FileHash {path} -Algorithm {algo}\"",
        .params = {
            CP{.name = "path", .label = _t("Datei"), .required = true},
            CP{.name = "algo", .label = _t("Algorithmus"), .kind = "choice", .defaultValue = "SHA256", .choices = {"SHA256", "SHA1", "MD5"}},
        },
        .example = "powershell -Command \"Get-FileHash setup.exe\"", .platform = "windows"});
    c.push_back(CS{
        .name = _t("Compress-Archive (PowerShell)"), .category = _t("Archive"),
        .description = _t("Erstellt ein ZIP-Archiv."),
        .templateText = "powershell -Command \"Compress-Archive -Path {src} -DestinationPath {dst}\"",
        .params = {
            CP{.name = "src", .label = _t("Quelle"), .required = true},
            CP{.name = "dst", .label = _t("ZIP-Datei"), .defaultValue = "archiv.zip", .required = true},
        },
        .example = "powershell -Command \"Compress-Archive -Path .\\ordner -DestinationPath a.zip\"",
        .platform = "windows"});
    c.push_back(CS{
        .name = _t("Expand-Archive (PowerShell)"), .category = _t("Archive"),
        .description = _t("Entpackt ein ZIP-Archiv."),
        .templateText = "powershell -Command \"Expand-Archive -Path {src} -DestinationPath {dst}\"",
        .params = {
            CP{.name = "src", .label = _t("ZIP-Datei"), .required = true},
            CP{.name = "dst", .label = _t("Zielordner"), .defaultValue = ".", .required = true},
        },
        .example = "powershell -Command \"Expand-Archive a.zip -DestinationPath .\"", .platform = "windows"});
    c.push_back(CS{
        .name = _t("Invoke-WebRequest (PowerShell)"), .category = _t("Netzwerk"),
        .description = _t("Lädt eine Datei/URL herunter."),
        .templateText = "powershell -Command \"Invoke-WebRequest {url} -OutFile {output}\"",
        .params = {
            CP{.name = "url", .label = _t("URL"), .required = true},
            CP{.name = "output", .label = _t("Zieldatei"), .defaultValue = "datei", .required = true},
        },
        .example = "powershell -Command \"Invoke-WebRequest https://example.com/f.zip -OutFile f.zip\"",
        .platform = "windows"});
    c.push_back(CS{
        .name = _t("Get-WinEvent (PowerShell)"), .category = _t("System"),
        .description = _t("Liest Ereignisprotokolle; -MaxEvents begrenzt."),
        .templateText = "powershell -Command \"Get-WinEvent -LogName {log} -MaxEvents {count}\"",
        .params = {
            CP{.name = "log", .label = _t("Protokoll"), .defaultValue = "System"},
            CP{.name = "count", .label = _t("Anzahl"), .defaultValue = "20"},
        },
        .example = "powershell -Command \"Get-WinEvent -LogName System -MaxEvents 20\"", .platform = "windows"});
    c.push_back(CS{
        .name = _t("Stop-Process (PowerShell)"), .category = _t("System"),
        .description = _t("Beendet Prozesse per Name; -Force erzwingt."),
        .templateText = "powershell -Command \"Stop-Process -Name {name} {force}\"",
        .params = {
            CP{.name = "name", .label = _t("Prozessname"), .required = true},
            CP{.name = "force", .label = _t("Erzwingen (-Force)"), .kind = "flag", .defaultValue = "on", .flagValue = "-Force"},
        },
        .example = "powershell -Command \"Stop-Process -Name notepad -Force\"", .danger = true, .platform = "windows"});
    c.push_back(CS{
        .name = _t("Restart-Service (PowerShell)"), .category = _t("System"),
        .description = _t("Startet einen Dienst neu."),
        .templateText = "powershell -Command \"Restart-Service {service}\"",
        .params = {CP{.name = "service", .label = _t("Dienst"), .required = true}},
        .example = "powershell -Command \"Restart-Service Spooler\"", .platform = "windows"});
    c.push_back(CS{
        .name = _t("Get-ChildItem (PowerShell)"), .category = _t("Dateien"),
        .description = _t("Listet Dateien/Ordner; -Recurse rekursiv."),
        .templateText = "powershell -Command \"Get-ChildItem {path} {recurse}\"",
        .params = {
            CP{.name = "path", .label = _t("Pfad"), .defaultValue = "."},
            CP{.name = "recurse", .label = _t("Rekursiv (-Recurse)"), .kind = "flag", .flagValue = "-Recurse"},
        },
        .example = "powershell -Command \"Get-ChildItem . -Recurse\"", .platform = "windows"});
}

std::vector<CS> buildCatalog()
{
    std::vector<CS> c;
    c.reserve(260);
    addPosixBase(c);
    addPosixFilesText(c);
    addPosixSystemNet(c);
    addGitDockerMore(c);
    addWindowsBase(c);
    addWindowsAdmin(c);
    addPosixAdmin(c);
    addPosixMisc(c);
    addPosixServer(c);
    addWindowsCmdMore(c);
    addWindowsPowerShell(c);
    return c;
}

} // namespace

const std::vector<CommandSpec> &catalog()
{
    // Je Sprache einmal uebersetzt und danach behalten: ausgegebene Referenzen
    // bleiben gueltig, und ein Sprachwechsel liefert den passenden Katalog
    // (vorher fror der erste Zugriff die Sprache fuer immer ein).
    static std::mutex mutex;
    static QHash<QString, const std::vector<CommandSpec> *> byLanguage;
    const std::lock_guard<std::mutex> lock(mutex);
    const QString language = currentLanguage();
    auto it = byLanguage.find(language);
    if (it == byLanguage.end())
        it = byLanguage.insert(language, new std::vector<CommandSpec>(buildCatalog()));
    return *it.value();
}

std::vector<CommandSpec> commandsFor(const QString &osType)
{
    std::vector<CommandSpec> out;
    for (const CommandSpec &s : catalog())
        if (s.platform == osType || s.platform == QLatin1String("any"))
            out.push_back(s);
    return out;
}

QStringList categories()
{
    QStringList seen;
    for (const CommandSpec &spec : catalog())
        if (!seen.contains(spec.category))
            seen.append(spec.category);
    return seen;
}

} // namespace ncssh::core
