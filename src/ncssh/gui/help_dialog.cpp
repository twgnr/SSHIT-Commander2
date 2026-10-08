#include "ncssh/gui/help_dialog.hpp"

#include "ncssh/core/i18n.hpp"
#include "ncssh/core/markdown.hpp"
#include "ncssh/core/shortcuts.hpp"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <vector>

namespace ncssh::gui {

using core::_t;

namespace {
struct Topic {
    QString title;
    QString body;   // Markdown
};

// Handbuch-Themen (feldgenaue Bedienungsdoku).
const std::vector<Topic> &topics()
{
    static const std::vector<Topic> list = {
        {_t("Überblick"),
         _t("**SSHIT-Commander** ist ein Dual-Pane-Dateimanager mit integriertem "
            "SSH/SFTP-Terminal.\n\n"
            "- Links und rechts je eine **Pane** — lokal oder remote, gleiche Bedienung.\n"
            "- Unter jeder Pane eine **Konsole** mit zwei Modi: *Befehle* und *Terminal*.\n"
            "- Beliebig viele **Tabs**, jeder mit eigener Verbindung.\n"
            "- Die gesamte Netzwerkarbeit läuft auf Hintergrund-Threads — das Fenster "
            "friert bei SSH-Operationen oder Transfers nie ein.")},
        {_t("Die Oberfläche"),
         _t("- **Menüleiste**: *Aktionen · Tools · Plugins · Clipboard · Panes · Ansicht "
            "· Hilfe*.\n"
            "- **Tableiste**: ein Tab je Arbeitsbereich; der Titel zeigt die Verbindung.\n"
            "- **Pane-Kopf**: Titel und Chips — *Filter* (Filter & Sortierung), bei "
            "Verbindung *ⓘ Info* (Server-Info), *⏏ Trennen* und *sudo*. Darunter die "
            "Pfadzeile: Laufwerksauswahl (lokal; der Tooltip zeigt den freien Platz), "
            "Zurück/Vor (Rechtsklick: zuletzt besuchte Ordner), Hoch, der Breadcrumb "
            "(Klick auf die freie Fläche: Pfad eintippen; Rechtsklick: Pfad kopieren oder "
            "bearbeiten), Lesezeichen und Neu laden.\n"
            "- **Pane-Statuszeile**: Anzahl und Größe der Einträge, aktive Filter und die "
            "Markierung.\n"
            "- **Konsolen-Kopf**: Symbole für *Parameter* (nur bei einem bekannten "
            "Befehl), *Befehlspalette* und *Verlauf*, dann *KI* (Ausgabe erklären), *+* "
            "(weiteres Terminal), *Terminal* (Modus umschalten) und `⤢` (in ein eigenes "
            "Fenster abdocken). Während eines Verbindungsaufbaus zusätzlich *Verbindung "
            "abbrechen*.\n"
            "- **Statusleiste**: Meldungen, Transfer-Ergebnisse, Alarm-Ereignisse, "
            "Host-Key-Status und die Zahl offener Tunnel.")},
        {_t("Server-Verwaltung & Verbinden"),
         _t("`F9`, *Aktionen → SSH verbinden* oder das Symbol in der Werkzeugleiste "
            "öffnen die **Server-Verwaltung**. Links stehen die Verbindungen mit "
            "Filterfeld, *Neuer Server* und *Import (PuTTY/WinSCP/SSH)*, rechts das "
            "Formular.\n"
            "\n"
            "**Felder**: *Anzeigename*, *Host*, *Port* (22), *Benutzername*, "
            "*Authentifizierung* (*SSH-Key*, *Passwort* oder *SSH-Agent* — Pageant bzw. "
            "OpenSSH-Agent), *Key-Pfad* (auch PuTTY-PPK, wird beim Verbinden umgewandelt; "
            "🔑 öffnet den Schlüssel-Dialog), *Passwort* mit *Passwort/Passphrase sicher "
            "im OS-Keyring speichern*, *Host-Key-Prüfung* (siehe *Sicherheit*), "
            "*ProxyJump*, *Startverzeichnis* und *Tab-Farbe*.\n"
            "\n"
            "**Feinsteuerung**: *Keepalive* (Sekunden, 0 = aus), *Verbindungs-Timeout*, "
            "*Chiffren* und *Schlüsseltausch* (bevorzugte Verfahren, kommagetrennt). "
            "*SSH-Kompression* komprimiert den Datenverkehr (zlib) — sinnvoll bei "
            "langsamen Verbindungen. *SSH-Agent weiterreichen* unterstützt die "
            "SSH-Bibliothek nicht.\n"
            "\n"
            "- **ProxyJump**: `[benutzer@]host[:port]` — ein Sprung-Host. Er meldet sich "
            "mit dem Key des Ziels an (bei Key-Anmeldung), sonst über den Agenten.\n"
            "- **Umgebungsvariablen**: eine Zuweisung `NAME=Wert` je Zeile, `#` leitet "
            "einen Kommentar ein. Sie gelten für Terminal und Konsole. Der Server "
            "übernimmt nur, was seine `AcceptEnv`-Liste erlaubt; abgelehnte Namen meldet "
            "das Terminal.\n"
            "- **Import** übernimmt Sitzungen aus PuTTY, WinSCP und `~/.ssh/config`; *Aus "
            "Datei importieren …* liest exportierte `.reg`- und `.ini`-Dateien. "
            "Passwörter werden nicht übernommen; schon vorhandene Profile sind markiert "
            "und nicht vorausgewählt.\n"
            "- *Tunnel (Auto-Start)* listet die Tunnel, die sich beim Verbinden "
            "automatisch öffnen (angelegt im Tunnel-Dialog); *Entfernen* löscht einen, "
            "wirksam mit *Speichern*.\n"
            "- *Zuletzt* zeigt die letzte erfolgreiche Verbindung, *Erreichbarkeit "
            "testen* prüft nur, ob der Port antwortet.\n"
            "\n"
            "**Verbinden**: *Verbinden* oder Doppelklick. Die Verbindung landet in der "
            "**aktiven** (blau umrandeten) Pane; fehlende Angaben wie Benutzer, Passwort "
            "oder Passphrase fragt die App ab. Ist die andere Pane des Tabs schon "
            "verbunden, fragt sie: *In neuem Tab verbinden* oder *Verbindung ersetzen*. "
            "Den Fortschritt zeigt die Konsole der Pane; *Verbindung abbrechen* im "
            "Konsolen-Kopf bricht ab, nach 30 s ohne Antwort ist ohnehin Schluss. Reißt "
            "eine Verbindung ab, verbindet die App automatisch neu.\n"
            "\n"
            "**Trennen**: *⏏ Trennen* im Pane-Kopf.")},
        {_t("Dateien verwalten"),
         _t("- **Navigieren**: Doppelklick öffnet einen Ordner bzw. zeigt eine Datei an "
            "(wie `F3`); `Enter` öffnet Ordner und startet Dateien mit dem "
            "Standardprogramm (Remote-Dateien als lokale Kopie). `Backspace` geht hoch, "
            "`Alt+←`/`Alt+→` zurück und vor, `Tab` wechselt die Pane. Den Pfad kann man "
            "im Breadcrumb direkt eintippen; Tippen in der Liste springt zum passenden "
            "Eintrag.\n"
            "- `F3` Ansehen · `F4` Bearbeiten · `F5` Kopieren · `F6` Umbenennen · `F7` "
            "Neuer Ordner · `F8` Löschen — unter *Tools → Einstellungen → Tastenkürzel* "
            "frei belegbar.\n"
            "- **Markieren**: `Space` oder `Einfg`, Strg/Shift-Klick, `Num +`/`Num −` "
            "nach Muster, `Num *` kehrt um, `Strg+A` markiert alles. Die F-Tasten "
            "arbeiten auf der Markierung.\n"
            "- **Kopieren und Verschieben** auch mit `Strg+C`/`Strg+X` und `Strg+V` oder "
            "über das Kontextmenü (*Verschieben → andere Pane*).\n"
            "- **Spalten**: Rechtsklick auf den Spaltenkopf blendet Größe, Geändert, "
            "Erstellt, Zugriff, Typ, Endung, Rechte und Eigner ein oder aus. Ein Klick "
            "auf einen Spaltenkopf sortiert danach, ein erneuter Klick dreht die "
            "Richtung. Mehrstufig sortieren und filtern: siehe *Filter & Sortierung*.\n"
            "- **Schnellfilter** `Strg+F`: blendet eine Zeile ein (`*.log` oder "
            "Teiltext); `Esc` leert und schließt sie.\n"
            "- **Drag & Drop**: zwischen den Panes ziehen (auch remote) oder aus dem "
            "Explorer hineinfallen lassen.\n"
            "- *Ansicht → Kachelansicht* (oder das Kontextmenü) wechselt zwischen Liste "
            "und Kacheln.\n"
            "- **Farben**: Ausführbare Dateien werden hervorgehoben (abschaltbar, Farbe "
            "wählbar in den Einstellungen). In lokalen Git-Repositories färbt sich der "
            "Name nach dem Git-Status: orange = geändert, grün = neu (auch noch nicht "
            "hinzugefügte Dateien und Ordner — dann auch alles darin), rot = gelöscht "
            "oder Konflikt, blau = umbenannt oder kopiert. Ein Ordner zeigt Änderungen in "
            "seinem Inneren an; der Tooltip nennt den Status.")},
        {_t("Filter & Sortierung"),
         _t("Der Chip **Filter** im Pane-Kopf öffnet *Filter und Sortierung* für diese "
            "Pane.\n"
            "\n"
            "**Filter**\n"
            "\n"
            "- *Anzeigen*: Dateien und Ordner, nur Dateien oder nur Ordner.\n"
            "- *Name* (mehrere Muster mit `;` getrennt, Platzhalter `*` und `?`), "
            "*Beginnt mit*, *Endet mit* und *Regulärer Ausdruck*; optional "
            "*Groß-/Kleinschreibung beachten*.\n"
            "- *Dateiendungen*.\n"
            "- *Datum*: geändert oder erstellt — im Zeitraum, älter als oder jünger als "
            "(Minuten bis Jahre).\n"
            "- *Größe*: ab und bis, in B, KB, MB oder GB.\n"
            "- Ordner betreffen nur die Namens- und Datumsregeln, und auch die nur mit "
            "*Namens- und Datumsregeln auch auf Ordner anwenden*.\n"
            "\n"
            "**Sortierung**: *Sortieren nach* und bis zu drei weitere Stufen (*dann "
            "nach*), je auf- oder absteigend; *Ordner zuerst* hält Ordner oben.\n"
            "\n"
            "*Anwenden* zeigt das Ergebnis sofort, *Zurücksetzen* hebt alles auf. Solange "
            "ein Filter aktiv ist, ist der Chip hervorgehoben und die Statuszeile nennt "
            "die ausgeblendeten Einträge. Filter und Sortierung gelten je Pane, bleiben "
            "beim Ordnerwechsel erhalten und werden nicht gespeichert. Der Schnellfilter "
            "`Strg+F` wirkt zusätzlich.")},
        {_t("Ansehen & Bearbeiten"),
         _t("- `F3` **Ansehen**: Text schreibgeschützt (die ersten 200 KB); Bilder "
            "(PNG/JPG/GIF/SVG …) als Vorschau mit Maßen und Dateigröße.\n"
            "- `F4` **Bearbeiten** öffnet den Editor mit **Syntax-Highlighting** für 22 "
            "Sprachen (u. a. Shell, PowerShell, Batch, Python, C/C++, C#, Java/Kotlin, "
            "JavaScript/TypeScript, Go, Rust, PHP, SQL, JSON, XML/HTML, YAML, INI/TOML, "
            "Markdown, Dockerfile und Makefile), **Zeilennummern** und **Minimap**. Die "
            "Sprache wird an Endung, Shebang und Inhalt erkannt; eine Wahl im Syntax-Feld "
            "merkt sich der Editor je Endung.\n"
            "- **Zeichensatz**: Der Editor erkennt die Kodierung (BOM, UTF-16, UTF-8, "
            "sonst Windows-1252) und speichert in derselben zurück. Bei einer "
            "unveränderten Datei liest ein Wechsel im Kodierungsfeld die Datei neu ein. "
            "Passen Zeichen nicht in die Kodierung, bietet das Speichern UTF-8 an. "
            "*Encoding konvertieren …* öffnet den Konverter.\n"
            "- **Zeilenende**: LF oder CRLF wird erkannt und so gespeichert, wie im Feld "
            "*Zeilenende* gewählt. *Umbruch* bricht lange Zeilen nur in der Anzeige um.\n"
            "- **Suchen & Ersetzen** in der Leiste unter dem Text (*Aa* = Groß/Klein "
            "beachten, *Ersetzen*, *Alle*). Treffer erscheinen farbig in der Minimap; ein "
            "Klick in die Minimap springt dorthin.\n"
            "- `Strg+S` speichert, `Strg+Shift+S` speichert unter, `Strg+F` sucht, "
            "`Strg+G` springt zu einer Zeile.\n"
            "- **Großdatei-Schutz**: Dateien über 5 MB öffnen nur lesend und zeigen den "
            "Anfang.\n"
            "- Ändert sich eine lokale Datei von außen, lädt der Editor sie neu — bei "
            "ungespeicherten Änderungen fragt er vorher. Auch vor dem Schließen mit "
            "ungespeicherten Änderungen wird gefragt.\n"
            "- **KI erklären** und **KI Fehleranalyse**: siehe *KI-Funktionen*.")},
        {_t("Terminal / Konsole"),
         _t("Jede Pane hat eine eigene Konsole mit zwei Modi, umgeschaltet mit "
            "**Terminal** im Konsolen-Kopf.\n"
            "\n"
            "**Befehle**: Befehl → Ausgabe.\n"
            "\n"
            "- `↑`/`↓` blättert durch die gespeicherte Historie.\n"
            "- `Strg+C`, `Esc` oder **■** bricht den laufenden Befehl ab.\n"
            "- `Strg+F` sucht in der Ausgabe.\n"
            "- `Tab` ergänzt Pfade; das Symbol **Parameter** zeigt die Optionen des "
            "getippten Befehls (siehe *Parameter-Fenster & Pfad-Vervollständigung*).\n"
            "- Ein `cd` synchronisiert die Pane, und ein Ordnerwechsel der Pane setzt das "
            "Arbeitsverzeichnis der Konsole.\n"
            "\n"
            "**Terminal**: eine echte Shell — lokal über ConPTY mit PowerShell oder cmd "
            "(*Einstellungen → Allgemein → Lokale Shell*), remote über SSH — mit vollem "
            "VT100/xterm-Emulator: `vim`, `htop`, `tmux`, `mc`, `less`, Farben und die "
            "Tab-Vervollständigung der Shell funktionieren.\n"
            "\n"
            "- **Kopieren**: `Strg+Shift+C` oder `Strg+Einfg`; `Strg+C` kopiert, wenn "
            "Text markiert ist, und bricht sonst ab.\n"
            "- **Einfügen**: `Strg+Shift+V`, `Strg+V` oder `Shift+Einfg`.\n"
            "- Auf entfernten Linux-Shells ist `Strg+Z` Rückgängig; `Strg+Shift+Z` sendet "
            "ein echtes `^Z` (Programm anhalten).\n"
            "- `Shift+Bild↑/↓` blättert im Scrollback (10 000 Zeilen), `Strg+Shift+F` "
            "sucht darin (`F3`/`Shift+F3` nächster bzw. vorheriger Treffer).\n"
            "- In Vollbild-Programmen gehen Mausklicks und Mausrad an das Programm; mit "
            "gedrückter `Shift`-Taste markiert man trotzdem Text.\n"
            "- Rechtsklick: Kopieren, Einfügen, Alles kopieren, Link öffnen, Leeren sowie "
            "**Mitschnitt starten …** — schreibt die Sitzung ohne Steuerzeichen in eine "
            "Datei, bis *Mitschnitt beenden*.\n"
            "- **+** öffnet weitere Terminals als Reiter (Doppelklick benennt um). Nach "
            "dem Ende einer Shell startet `Enter` sie neu.\n"
            "- Schriftart und -größe: *Einstellungen → Allgemein*.\n"
            "\n"
            "**In beiden Modi**:\n"
            "\n"
            "- `Strg+Shift+K` (*Panes → Befehl an beide Konsolen …*) schickt einen Befehl "
            "an beide Konsolen des Tabs.\n"
            "- `⤢` löst die Konsole in ein eigenes Fenster; die Pane füllt dann die ganze "
            "Spalte. *⤵ Andocken* oder das Schließen des Fensters dockt wieder an.")},
        {_t("Parameter-Fenster & Pfad-Vervollständigung"),
         _t("**Pfade ergänzen**: In der Befehlszeile der Konsole (Modus *Befehle*) und in "
            "den Feldern des Parameter-Fensters ergänzt `Tab` das Wort vor dem Cursor zu "
            "einem Datei- oder Ordnernamen — lokal oder auf dem verbundenen Server. Jedes "
            "weitere `Tab` springt zum nächsten Treffer (Ordner zuerst); Namen mit "
            "Leerzeichen werden gequotet, nach `cd` gibt es nur Ordner. Im Terminal-Modus "
            "ergänzt die Shell selbst.\n"
            "\n"
            "**Parameter-Fenster**: Erkennt die Konsole den getippten Befehl (aus dem "
            "Befehlskatalog oder als gängiges Unix-Werkzeug), erscheint im Konsolen-Kopf "
            "das Symbol **Parameter**. Es öffnet ein Fenster, das neben der Arbeit offen "
            "bleiben kann:\n"
            "\n"
            "- **Argumente (in der Reihenfolge des Befehls)** — je ein Feld, z. B. Quelle "
            "und Ziel bei `cp`; *Variante* wählt zwischen mehreren Formen des Befehls. "
            "*Wird angefügt* zeigt das Ergebnis, *Argumente anfügen* hängt es an.\n"
            "- **Optionen (Doppelklick fügt an)** — mit Filterfeld. Optionen aus dem "
            "Katalog sind fett; braucht eine Option einen Wert, fragt das Fenster danach. "
            "Unter Linux/Unix liest die App zusätzlich die `--help`-Ausgabe des Befehls "
            "und ergänzt die Liste.\n"
            "\n"
            "Angefügt wird an die Befehlszeile der Konsole, in beiden Modi.")},
        {_t("Übertragungen (Transfer-Queue)"),
         _t("`F5` öffnet *Kopieren / Übertragen*: Zielordner (vorbelegt mit dem Pfad der "
            "anderen Seite, editierbar oder per *Durchsuchen…*), bei genau einem Eintrag "
            "zusätzlich der **Name** — Kopieren und Umbenennen in einem Schritt. *Bei "
            "fertiger Übertragung benachrichtigen* meldet das Ende in der Statusleiste; "
            "mit *Nicht mehr fragen* kopiert `F5` künftig direkt in die andere Pane.\n"
            "\n"
            "**Konflikte**: Existiert das Ziel schon, fragt die App: *Ja*, *Ja, alle*, "
            "*Nein*, *Nein, alle* oder *Abbrechen*. Das gilt für `F5`, Drag & Drop, "
            "Einfügen und Verschieben. Quelle = Ziel wird übersprungen, ein Ordner in "
            "sich selbst abgelehnt.\n"
            "\n"
            "`Strg+T` öffnet **Übertragungen**: Richtung, Fortschritt, Größe, Tempo und "
            "Restzeit je Auftrag. Aufträge starten sofort und laufen parallel.\n"
            "\n"
            "- *Pause / Fortsetzen* und *Wiederaufnehmen* machen an der schon "
            "übertragenen Größe weiter; fertige Dateien werden übersprungen.\n"
            "- *Abbrechen* und *Abgeschlossene entfernen*.\n"
            "- **Limit** (KB/s, 0 = unbegrenzt) drosselt jeden Auftrag einzeln; es gilt "
            "ab dem nächsten Start oder Fortsetzen.\n"
            "- Nach der Übertragung wird die Größe geprüft (✓). Beim **Verschieben** wird "
            "die Quelle erst nach erfolgreicher Prüfung gelöscht.\n"
            "- Dateien ab 8 MB und Ordner laufen über eine **eigene SSH-Verbindung**, "
            "damit das Navigieren flüssig bleibt; klappt deren Aufbau nicht, über die "
            "bestehende.\n"
            "\n"
            "Der Ordner-Browser funktioniert auch auf entfernten Servern.")},
        {_t("SFTP-Batch & geplante Aufgaben"),
         _t("*Aktionen → SFTP-Batch / geplante Aufgaben …* (`Strg+Shift+B`) führt ein "
            "Skript über die Verbindung des aktuellen Tabs aus. Relative Pfade beginnen "
            "beim Ordner der Remote- bzw. der lokalen Pane.\n"
            "\n"
            "Ein Befehl je Zeile; Pfade mit Leerzeichen stehen in `\"…\"`, `#` leitet "
            "einen Kommentar ein:\n"
            "\n"
            "- `cd PFAD`, `lcd PFAD`, `pwd`, `lpwd` — Ordner wechseln bzw. anzeigen "
            "(remote/lokal).\n"
            "- `put LOKAL [REMOTE]`, `get REMOTE [LOKAL]` — hoch- bzw. herunterladen; ist "
            "das Ziel ein Ordner, landet die Datei darin. Vorhandene Dateien werden ohne "
            "Rückfrage überschrieben; Platzhalter gibt es nicht.\n"
            "- `mkdir PFAD`, `rm DATEI`, `rmdir PFAD` (**löscht rekursiv**), `rename ALT "
            "NEU` bzw. `mv`, `chmod OKTAL PFAD`, `ln ZIEL LINK` und `echo TEXT`.\n"
            "- `set NAME WERT` legt eine eigene Variable an.\n"
            "\n"
            "**Variablen** in jeder Zeile: `$heute` (`2026-10-08`), `$jetzt` "
            "(`2026-10-08_14-30-05`), `$zeit`, `$jahr`, `$monat`, `$tag` sowie eigene als "
            "`$NAME`. `${NAME}` grenzt den Namen ab, wenn direkt Buchstaben, Ziffern oder "
            "`_` folgen (z. B. `${tag}_log`); `$$` ergibt ein `$`. Alle Zeitvariablen "
            "beziehen sich auf den Start des Laufs. Eine unbekannte Variable ist ein "
            "Fehler der Zeile — z. B. `get logs/app.log app-$heute.log`.\n"
            "\n"
            "*Ausführen* startet das Skript. Das Protokoll zeigt jede Zeile mit ✓ oder ✗ "
            "und am Ende eine Bilanz; *Stopp* bricht zwischen zwei Zeilen ab. Mit *Bei "
            "Fehler abbrechen* endet das Skript beim ersten Fehler, sonst läuft es "
            "weiter.\n"
            "\n"
            "**Wiederholen alle** N Minuten führt das Skript regelmäßig aus — zum ersten "
            "Mal nach einem Intervall und nur, **solange der Dialog offen ist**. Skript "
            "und Intervall werden beim Ausführen gemerkt; *Laden …* und *Speichern …* "
            "arbeiten mit Skriptdateien.")},
        {_t("Befehlspalette & Assistent"),
         _t("`Strg+P` (oder das Symbol im Konsolen-Kopf) öffnet den **Befehlskatalog** — "
            "mit Suchfeld, sortierbar nach Befehl, Kategorie, Plattform und Beschreibung.\n"
            "\n"
            "- **OS-Filter**: Aktuelles OS · Beide · Nur Linux/Unix · Nur Windows · "
            "Plattformübergreifend. Das Server-OS wird beim Verbinden erkannt.\n"
            "- **Gefährliche Befehle** sind rot markiert.\n"
            "- *Einfügen* schreibt die Befehlsvorlage in die Konsole.\n"
            "- **Assistent …** (oder Doppelklick) öffnet den Parameter-Editor: "
            "Textfelder, Auswahllisten und Checkboxen mit **Live-Vorschau**, unter Linux "
            "zusätzlich **sudo** (optional als anderer Benutzer). *In Konsole einfügen* "
            "übernimmt den fertigen Befehl, *Ausführen* startet ihn sofort.")},
        {_t("Datei-Suche (nach Name)"),
         _t("`Strg+Shift+F` sucht nach Dateinamen.\n"
            "\n"
            "- **Root**: Startordner; *…* wählt ihn, *⌂* setzt die Laufwerkswurzel.\n"
            "- **Dateiname**: Platzhalter (`*.log`) oder **Regex**; *Groß/klein "
            "ignorieren* ist voreingestellt. *Datei-Filter* grenzt auf Muster ein "
            "(kommagetrennt).\n"
            "- **Erweitert**: *Dateien ausschließen*, *Ordner ausschließen* (z. B. "
            "`.git,node_modules`), *Art* (Alles, nur Dateien, nur Ordner) und *Grenzen* — "
            "Max-Tiefe, Min-Größe und Geändert ≤ Tage.\n"
            "- Treffer laufen live ein; *Stopp* bricht ab.\n"
            "- Doppelklick auf einen Treffer springt in dessen Ordner.")},
        {_t("Inhalts-Suche (grep)"),
         _t("`Strg+Alt+F` durchsucht Datei-**Inhalte**.\n"
            "\n"
            "- **Suchbegriff** wörtlich oder als **Regex**; *Groß/klein ignorieren* ist "
            "voreingestellt, *Ganzes Wort* grenzt auf Wortgrenzen ein.\n"
            "- **Binärdateien** werden übersprungen, außer mit *Binärdateien "
            "einbeziehen*.\n"
            "- *Datei-Filter* und unter **Erweitert** die Ausschlüsse und Grenzen wie bei "
            "der Datei-Suche, dazu *Nur Dateinamen*, *Zeilen ohne Treffer* (invertiert) "
            "und *Zeilen vor/nach jedem Treffer* (Kontext).\n"
            "- Ergebniszeilen haben die Form `pfad:zeile:text`; Doppelklick öffnet den "
            "Ordner der Datei.")},
        {_t("Massen-Umbenennen"),
         _t("`Strg+Shift+R` benennt die markierten Dateien in einem Rutsch um. Der "
            "*Geltungsbereich* wählt Name, Endung oder den ganzen Namen.\n"
            "\n"
            "**Suchen & Ersetzen** — Modus *Text (wörtlich)*, *Platzhalter* (`*` und `?`) "
            "oder *Regex*; optional *Groß/Klein egal* und *Alle Vorkommen ersetzen*. "
            "*Regex-Vorlage* bietet fertige Muster, z. B. Klammern samt Inhalt, führende "
            "Nummern, Ziffern oder Kopie-Suffixe entfernen oder Leer- und Sonderzeichen "
            "ersetzen.\n"
            "\n"
            "**Entfernen & Einfügen** — vorne oder hinten kürzen, einen Text entfernen "
            "und einen Text an einer Zeichenposition einfügen (negativ zählt vom Ende).\n"
            "\n"
            "**Text & Endung** — Präfix, Suffix, Schreibweise (klein, GROSS, Wortanfänge, "
            "Satzanfang), Leerzeichen (behalten, `_`, `-`, entfernen) und Endung "
            "(unverändert, klein, GROSS, setzen).\n"
            "\n"
            "**Nummerierung** (*aktiv*) — Start, Schritt, Stellen, Trenner und Position "
            "(vorne, hinten, an einer Position) sowie die **Reihenfolge** der "
            "Nummernvergabe (Eingabe, Name, Name absteigend, natürlich `1,2,10`, Endung).\n"
            "\n"
            "Die **Vorschau** zeigt Alt → Neu; geänderte Namen sind grün, Konflikte rot. "
            "*Konflikte automatisch nummerieren* hängt ` (1)`, ` (2)` … an. Ausgeführt "
            "wird in einer **gefahrlosen Reihenfolge** — auch Tausch (`a↔b`) und Ketten "
            "(`a→b→c`) funktionieren über temporäre Zwischennamen. Schlägt ein Schritt "
            "fehl, werden die bisherigen zurückgenommen; *Rückgängig* macht eine "
            "ausgeführte Umbenennung rückgängig.")},
        {_t("Datei-Encoding konvertieren"),
         _t("*Tools → Datei-Encoding konvertieren* wandelt Textdateien zwischen "
            "Zeichensätzen um — UTF-8/16/32, Windows-1250/1251/1252, ISO 8859-1/15, "
            "DOS/OEM 437/850, Mac Roman, KOI8-R, Shift-JIS, GBK, Big5 sowie **EBCDIC** "
            "(cp037, cp500, cp273, cp1140, cp1141, cp1047, cp875).\n"
            "\n"
            "- Das **Quell-Encoding wird automatisch erkannt** und vorgewählt.\n"
            "- Die **Vorschau** zeigt die Quelle mit dem gewählten Codec.\n"
            "- **Fehlerstrategie**: streng (melden), ersetzen oder ignorieren.\n"
            "- Ausgabe in eine neue Datei oder **Original überschreiben**.\n"
            "- **Mit KI reparieren …** lässt beschädigten Text von der KI rekonstruieren; "
            "die Vorschau zeigt das Ergebnis, gespeichert wird es erst mit "
            "*Konvertieren*.")},
        {_t("Datei- & Verzeichnis-Vergleich"),
         _t("- `Strg+Shift+D` **Datei-Vergleich**: zwei markierte Dateien als farbiger "
            "Unified-Diff (grün = hinzugefügt, rot = entfernt), mit Zeilenbilanz.\n"
            "- `Strg+D` **Verzeichnis-Vergleich**: stellt beide Panes gegenüber (*nur "
            "links*, *nur rechts*, *links neuer*, *rechts neuer*, *identisch*), optional "
            "**rekursiv**. *Nur Unterschiede* (voreingestellt) blendet Gleiches aus, "
            "*Aktualisieren* vergleicht neu.\n"
            "- *→ Rechts angleichen* bzw. *← Links angleichen* überträgt die markierten "
            "Einträge über die Transfer-Queue — ohne Rückfrage, vorhandene Dateien werden "
            "überschrieben.")},
        {_t("venv verwalten"),
         _t("*Tools → venv verwalten* legt virtuelle Python-Umgebungen an.\n"
            "\n"
            "- **Projektordner**, **venv-Pfad** (Standard `.venv`), gefundene "
            "**Python-Versionen** zur Auswahl und die **Installation** (automatisch "
            "vorgeschlagen, z. B. aus `requirements.txt`; läuft nach dem Aktivieren).\n"
            "- *Abhängigkeiten ignorieren* hängt `--skip-lock` (pipenv) bzw. `--no-deps` "
            "(pip) an.\n"
            "- Die **Befehlsvorschau** zeigt, was ausgeführt wird; *Erstellen & "
            "aktivieren* schickt es an die aktive Konsole.\n"
            "- Darunter die **bekannten Umgebungen** mit Typ (venv/pipenv), Version, "
            "Projekt und Pfad. *Aktivieren* oder Doppelklick aktiviert eine Umgebung in "
            "der Konsole, *Umgebung löschen* entfernt sie samt Ordner. Eine *Notiz zur "
            "ausgewählten Umgebung* speichert *Info speichern*.")},
        {_t("Makro-Manager"),
         _t("*Tools → Makro-Manager* bietet ein Raster frei belegbarer Tasten.\n"
            "\n"
            "- **Layer** (Seiten) links: anlegen, bearbeiten (Name, zugeordnetes "
            "Programm, Zeilen × Spalten), löschen. *Exportieren …* schreibt alle Layer "
            "als JSON, *Importieren …* übernimmt ausgewählte (gleiche Namen werden "
            "umbenannt).\n"
            "- *Layer automatisch zum Programm wechseln* beobachtet das "
            "Vordergrund-Programm und schaltet passend um; *Aktives Programm übernehmen* "
            "trägt es beim Bearbeiten ein.\n"
            "- **Bearbeiten-Modus**: Klick auf eine Taste öffnet den Editor. "
            "**Ausführen-Modus**: Klick löst die Aktion aus, langes Halten öffnet "
            "trotzdem den Editor. Rechtsklick: Bearbeiten, Ausführen, Leeren. Im "
            "Ausführen-Modus lässt sich das Fenster an einen Bildschirmrand andocken.\n"
            "\n"
            "Im **Tasten-Editor**: Beschriftung samt Position, Icon (als Hintergrund), "
            "Schriftfarbe und -art, ein **globales Kürzel** und die **Aktion**. Je nach "
            "Aktionstyp erscheint der passende Editor — Text, Zahl, Layer, Fenster, "
            "SSH-Befehl, JSON oder mehrere Schritte.\n"
            "\n"
            "**Globale Kürzel** wirken systemweit, auch wenn SSHIT-Commander nicht im "
            "Vordergrund ist und der Makro-Manager nie geöffnet wurde. Sie lösen erst "
            "nach dem Loslassen der Modifikatortasten aus und ruhen, solange der "
            "Tasten-Editor offen ist.\n"
            "\n"
            "**Aktionen**: Programme und Befehle starten, Dateien/URLs öffnen, "
            "Bildschirmfoto, Bildschirm sperren · Text tippen oder einfügen, Tastenkürzel "
            "und Tasten drücken/halten · Maus bewegen, klicken, scrollen · Medien und "
            "Lautstärke, Audiogerät wählen · Fenster fokussieren, verwalten, "
            "durchschalten · Layer wechseln · HTTP-Anfragen, Befehle an die SSH-Konsole "
            "oder an alle Konsolen · Verzögerung, mehrere Aktionen (alle bei jedem "
            "Druck), Sequenz (bei jedem Druck der nächste Schritt, danach von vorn) · "
            "Zwischenablage, Mehrzustands-Taste und Befehlsauswahl.")},
        {_t("Plugins"),
         _t("*Plugins → Plugins verwalten …* bindet eigenständige Programme ein.\n"
            "\n"
            "- **Programm** (relativ zum `plugins/`-Ordner oder absolut), **Parameter** "
            "mit Platzhalter `{path}` für das gewählte Element (ohne Platzhalter wird der "
            "Pfad angehängt), **Arbeitsverzeichnis** (Standard: Ordner des Programms).\n"
            "- **Im Kontextmenü anzeigen** blendet das Plugin in der Pane ein; *Gilt für* "
            "schränkt auf Dateien, Ordner oder beides ein.\n"
            "- **Testen** startet das Plugin sofort.\n"
            "- Alle Plugins stehen auch direkt im Menü *Plugins*, ebenso *Plugin-Ordner "
            "öffnen*.\n"
            "- Zentral bereitgestellte Plugins (aus `plugins/plugins.json`) sind "
            "schreibgeschützt und mit *(zentral)* markiert.")},
        {_t("Netzwerkscanner"),
         _t("*Tools → Netzwerkscanner …* durchsucht das lokale Netz.\n"
            "\n"
            "- **IP-Range**: CIDR (`192.168.1.0/24`), Bereiche (`10.0.0.1-50`), Listen "
            "oder einzelne Namen. Die lokale /24 ist vorbelegt.\n"
            "- **Ports**: Vorauswahl (Häufige Ports, Nur SMB, Web, Fernzugriff, Alle "
            "wichtigen) oder eigene (`22,80,8000-8100`).\n"
            "- **Optionen**: Ping (ICMP) zusätzlich, nur antwortende Hosts anzeigen, "
            "Hostnamen auflösen, Freigaben erkennen (SMB), Geräte identifizieren (Banner, "
            "Web-Titel, OS, NetBIOS), MAC-Adresse + Hersteller, Parallelität, Timeout je "
            "Port und Auto-Rescan. *Letzten Scan laden* zeigt das letzte Ergebnis ohne "
            "neuen Scan.\n"
            "- Die Tabelle zeigt IP, Name, MAC, **Hersteller** (aus der OUI-Tabelle), "
            "OS-Schätzung, offene Ports mit Dienstnamen sowie Weboberfläche und "
            "Freigaben. Doppelklick öffnet eine gefundene Weboberfläche.\n"
            "- Mit *Schließen* werden die Hosts als **Dateisystem** (`net://`) in die "
            "unter *Ergebnisse in* gewählte Pane übernommen: Host → Freigabe → Dateien.")},
        {_t("Alarm Trigger (Datei-Alarm)"),
         _t("*Tools → Alarm Trigger …* (oder im Kontextmenü der Pane *Alarm Trigger für "
            "Verzeichnis setzen …*) überwacht Ordner auf Änderungen.\n"
            "\n"
            "Je Alarm:\n"
            "\n"
            "- *Anzeigename* (optional) und *Zu überwachender Ordner*.\n"
            "- *Erkannte Änderungen*: Neu erstellt, Geändert, Gelöscht; dazu *Unterordner "
            "einbeziehen*, *Ordner mitüberwachen* und *aktiv*.\n"
            "- *Nur diese Muster* und *Diese Muster ignorieren*: Platzhalter, mit `;` "
            "getrennt (z. B. `*.log;*.tmp`).\n"
            "- **Remote**: Ein Alarm, der in einem verbundenen Tab angelegt wird, "
            "überwacht den Ordner auf diesem Server (Pfad eintippen). Ist der Server "
            "nicht verbunden, pausiert der Alarm.\n"
            "- *Befehl bei Auslösung*: ein lokaler Befehl, einmal je Prüfdurchlauf. "
            "Platzhalter `{path}`, `{kind}` (created/modified/deleted), `{name}` (Name "
            "des Alarms) und `{count}` — auch als Umgebungsvariablen `ALARM_PATH`, "
            "`ALARM_KIND`, `ALARM_NAME` und `ALARM_COUNT`.\n"
            "\n"
            "Für alle Alarme gilt *Bei Auslösung*: **Desktop-Benachrichtigung** "
            "(voreingestellt) und **Signalton**. Dazu erscheint eine Meldung in der "
            "Statusleiste; ein Klick darauf zeigt die Ereignisse. Geprüft wird alle paar "
            "Sekunden per Schnappschuss-Vergleich.")},
        {_t("GitHub Repo Alarm"),
         _t("*Tools → GitHub Repo Alarm …* meldet neue Pushes.\n"
            "\n"
            "- **Repository** als `owner/repo` oder als GitHub-URL (auch "
            "`git@github.com:owner/repo.git`). Das Häkchen in der Spalte *Aktiv* schaltet "
            "die Überwachung eines Repositorys an oder aus.\n"
            "- Ein optionales **Token** (*Token speichern*) erhöht das API-Limit, erlaubt "
            "private Repositories und liegt im Schlüsselbund, nicht im Klartext.\n"
            "- Geprüft wird der Zeitstempel des letzten Pushs, im Intervall aus "
            "*Einstellungen → Allgemein* (Standard 15 Minuten); ändert er sich, meldet "
            "die Statusleiste neue Daten. *Jetzt prüfen* fragt sofort ab.\n"
            "- **Lokaler Ordner** ordnet den lokalen Klon zu (*Ordner wählen …*, "
            "*Zuordnung entfernen*); eine lokale Pane erkennt den Klon auch selbst. Hat "
            "er Änderungen, sind in den Panes alle Ordner darüber bis zum Laufwerk farbig "
            "markiert — grün, wenn nur neue Dateien dazugekommen sind, sonst orange.")},
        {_t("Zwischenablage-Verwaltung"),
         _t("*Clipboard → Clipboard-Manager* führt eine Historie der kopierten Texte und "
            "Dateien (bis zu 100 Einträge, nur im Speicher).\n"
            "\n"
            "- Doppelklick setzt einen Eintrag als **aktiven** Inhalt der Zwischenablage, "
            "fügt ihn in die aktive Konsole ein und schließt das Fenster.\n"
            "- *Einfügen* schreibt den Eintrag in die aktive Konsole.\n"
            "- *Eintrag löschen* und *Alle löschen* räumen die Liste auf.")},
        {_t("Eigenschaften & Rechte (chmod)"),
         _t("Kontextmenü → **Eigenschaften** zeigt Name, Pfad, Typ, Größe, Zeitstempel, "
            "Eigner/Gruppe und ein evtl. Symlink-Ziel. Bei lokalen Ordnern wird die "
            "Größe rekursiv berechnet.\n\n"
            "Der **chmod-Editor** darunter hat rwx-Checkboxen für Eigner, Gruppe und "
            "Andere sowie ein **Oktal-Feld** — beide Darstellungen halten sich "
            "gegenseitig aktuell. *Übernehmen* schreibt die Rechte.")},
        {_t("SSH-Schlüssel"),
         _t("🔑 in der Server-Verwaltung oder *Tools → SSH-Schlüssel erzeugen / "
            "konvertieren …* öffnet den **Schlüssel-Dialog**.\n"
            "\n"
            "- **Erzeugen**: Ed25519 (empfohlen), RSA 4096/3072 oder ECDSA nistp256, mit "
            "optionalem Kommentar. Erzeugt wird mit dem `ssh-keygen` von Windows, ohne "
            "Passphrase.\n"
            "- Der **öffentliche Schlüssel** wird angezeigt und lässt sich kopieren — er "
            "gehört in `~/.ssh/authorized_keys` auf dem Server.\n"
            "- **Schlüssel speichern …** legt Privat- und `.pub`-Datei ab; der "
            "Privatschlüssel erhält restriktive Rechte.\n"
            "- **Konvertieren**: OpenSSH → PPK und PPK → OpenSSH.\n"
            "- Nach dem Speichern bzw. nach PPK → OpenSSH wird der Pfad ins Profil "
            "übernommen.")},
        {_t("SSH-Tunnel / Port-Weiterleitung"),
         _t("`Strg+Shift+T` bzw. *Aktionen → SSH-Tunnel* öffnet die Port-Weiterleitungen "
            "(der Tab muss verbunden sein):\n"
            "\n"
            "- **Lokal (-L)**: ein lokaler Port wird auf ein Ziel hinter dem Server "
            "geleitet — z. B. `127.0.0.1:8080` → `localhost:80` auf dem Server.\n"
            "- **Remote (-R)**: ein Port auf dem Server zeigt auf ein lokales Ziel.\n"
            "- **Dynamisch / SOCKS (-D)**: SOCKS5-Proxy über die SSH-Verbindung.\n"
            "\n"
            "*Im Server-Profil speichern (Auto-Start beim Verbinden)* merkt den Tunnel im "
            "Profil; er öffnet sich dann bei jeder Verbindung mit diesem Server. "
            "Gespeicherte Tunnel stehen in der Server-Verwaltung unter *Tunnel "
            "(Auto-Start)* und lassen sich dort entfernen. Offene Tunnel stehen unter "
            "*Aktive Weiterleitungen* und lassen sich einzeln **stoppen**; die "
            "Statusleiste zeigt ihre Zahl. Beim Trennen oder Schließen des Tabs werden "
            "sie beendet.")},
        {_t("Server-Info"),
         _t("Der Chip **ⓘ Info** im Kopf einer verbundenen Pane öffnet die Server-Info — "
            "die wichtigsten Eckdaten eines Linux/Unix-Servers, ermittelt in einem "
            "einzigen SSH-Aufruf.\n"
            "\n"
            "- **Kopf**: Hostname, System, Kernel, Laufzeit und Last sowie der "
            "angemeldete Benutzer.\n"
            "- **Konfigurationsdateien**: wichtige Dateien nach Kategorie, mit Zugriff "
            "(*lesen & schreiben*, *nur lesen*, *kein Zugriff (sudo nötig)*), dazu "
            "gefundene `.env`- und docker-compose-Dateien unter `/home`, `/root`, "
            "`/var/www`, `/srv` und `/opt`. *Öffnen / Bearbeiten* oder Doppelklick öffnet "
            "die Datei im Editor — bei aktivem sudo-Chip mit dessen Rechten; *Pfad "
            "kopieren* kopiert den Pfad.\n"
            "- **Benutzer** (Systemkonten auf Wunsch), **Ports**, **Dienste** und "
            "**Speicher**.\n"
            "\n"
            "*Aktualisieren* fragt erneut ab. Für Windows-Server gibt es keine "
            "Server-Info.")},
        {_t("KI-Funktionen"),
         _t("Der KI-Assistent erklärt Ausgaben und Dateien. Er ist **rein beratend** und "
            "führt nichts aus.\n"
            "\n"
            "**Einrichten** unter *Einstellungen → KI*: *KI-Assistent aktivieren*, dann "
            "den **Anbieter** wählen:\n"
            "\n"
            "- **Ollama (lokal)** — das Modell läuft auf einem eigenen Ollama-Server "
            "(Standard: dieser Rechner); die Inhalte bleiben dort. *Modell laden* holt "
            "neue Modelle direkt in Ollama.\n"
            "- **Anthropic Claude**, **OpenAI**, **Google Gemini** — Cloud-Dienste mit "
            "eigenem **API-Schlüssel**.\n"
            "- **OpenAI-kompatibel** — z. B. LM Studio oder ein eigener Server; *Adresse* "
            "angeben, API-Schlüssel optional.\n"
            "\n"
            "*Modelle laden* fragt die verfügbaren Modelle ab, *Verbindung testen* prüft "
            "die Einstellungen. API-Schlüssel liegen im Windows Credential Manager.\n"
            "\n"
            "**Datenschutz**: Bevor zum ersten Mal Inhalte an einen Anbieter außer Ollama "
            "gehen, fragt die App nach — *Senden — nicht mehr fragen* oder *Abbrechen*. "
            "Die Zustimmung gilt je Anbieter; *Rückfrage vor dem Senden wieder "
            "einschalten* auf derselben Seite nimmt sie zurück.\n"
            "\n"
            "**Aufrufen**:\n"
            "\n"
            "- **KI** im Konsolen-Kopf erklärt die sichtbare Ausgabe bzw. einen Fehler.\n"
            "- *Tools → KI*: Terminalausgabe erklären, Datei erklären / Frage zur Datei "
            "und KI-Fehleranalyse (Quellcode) für die markierte Datei.\n"
            "- Im Editor: **KI erklären** (eine Markierung hat Vorrang vor der ganzen "
            "Datei) und **KI Fehleranalyse** (nur Fehler, mit Schweregrad und "
            "Korrekturvorschlag).\n"
            "- Im Encoding-Konverter: **Mit KI reparieren …**.\n"
            "\n"
            "Antworten erscheinen in einem Chat-Fenster mit **Folgefragen** (`Strg+Enter` "
            "sendet, *Stop* bricht ab).")},
        {_t("Ansicht & Designs (Themes)"),
         _t("*Ansicht → Theme* schaltet zwischen **Dunkel**, **Mitternacht**, **Hell**, "
            "**Hoher Kontrast** und eigenen Themes um; die Wahl wird gespeichert und gilt "
            "auch für das Terminal.\n"
            "\n"
            "*Ansicht → Theme-Editor* erstellt eigene Farbschemata: eine Basis wählen, "
            "die Farbfelder anklicken (Hintergrund, Flächen, Rahmen, Text, Akzente, "
            "Scrollbalken, Terminal-Farben) und unter eigenem Namen speichern. Die "
            "**Vorschau** unten zeigt das Ergebnis sofort. Mitgelieferte Themes lassen "
            "sich nicht überschreiben oder löschen.\n"
            "\n"
            "Außerdem im Menü *Ansicht*: **Versteckte Dateien** (`Strg+.`), "
            "**Kachelansicht** und das **Vorschau-Panel** (`Strg+F2`), das die markierte "
            "Datei schreibgeschützt zwischen Pane und Konsole anzeigt (Text oder Bild). "
            "Schriftarten und -größen: *Einstellungen → Allgemein*.")},
        {_t("Lesezeichen"),
         _t("Pfad-Lesezeichen werden **je Verbindung** getrennt geführt (Profilname bzw. "
            "`local`).\n"
            "\n"
            "- Der Stern in der Pfadzeile merkt den aktuellen Pfad bzw. entfernt ihn "
            "wieder.\n"
            "- Die Lesezeichen-Schaltfläche daneben öffnet ein Menü: ein Klick springt "
            "hin, dazu *★ Aktuellen Pfad merken* und *Verwalten…*.\n"
            "- `Strg+B` bzw. *Panes → Lesezeichen …* öffnet die Verwaltung: *Anspringen*, "
            "*Entfernen*, *Exportieren …* und *Importieren …*.\n"
            "- *Aktionen → Lesezeichen exportieren …* bzw. *importieren …* teilt "
            "Lesezeichen zwischen Rechnern.")},
        {_t("Einstellungen"),
         _t("`Strg+,` öffnet die Einstellungen mit vier Reitern. Die meisten Änderungen "
            "gelten nach *Speichern* sofort; ein Sprachwechsel braucht einen Neustart, "
            "den die App gleich anbietet.\n"
            "\n"
            "**Allgemein** — Sprache, Theme (eigene Themes lassen sich löschen), "
            "Schriftgrößen für Editor, Terminal und Panes, Terminal-Schriftart, "
            "**Datumsformat** (Token wie `DD.MM.YYYY HH24:MI`), versteckte Dateien "
            "ausblenden, Programm-Logos und Bild-Vorschau als Icon, **natürliche "
            "Sortierung** (`1, 2, 10`), schlanke Ansicht, ausführbare Dateien farblich "
            "hervorheben (Farbe wählbar), Bestätigung vor dem Kopieren (`F5`) und Löschen "
            "(`F8`), **lokale Shell** (PowerShell oder cmd), Tabs beim Start "
            "wiederherstellen, beim Start zum letzten Server verbinden, Prüfintervall des "
            "GitHub-Alarms und Standard-Startpfad.\n"
            "\n"
            "**KI** — Anbieter, Adresse, API-Schlüssel und Modell (siehe "
            "*KI-Funktionen*).\n"
            "\n"
            "**Tastenkürzel** — alle Aktionen frei belegbar; beim Speichern werden "
            "**Dubletten** gemeldet, auch mit fest vergebenen Tasten. *Auf Standard "
            "zurücksetzen* stellt die Vorgaben wieder her.\n"
            "\n"
            "**Sicherheit** — App-Passwort, Zwei-Faktor-Anmeldung und "
            "Wiederherstellungscodes (siehe *App-Sperre & Zwei-Faktor*); diese Änderungen "
            "gelten sofort.\n"
            "\n"
            "**Konfiguration exportieren** bzw. **importieren** (unter den Reitern) "
            "sichert Einstellungen, Serverprofile, Lesezeichen, Tab-Favoriten und Verlauf "
            "in einer JSON-Datei. Nicht enthalten sind Passwörter, Tokens und "
            "API-Schlüssel, die App-Sperre, Makros, Plugins und Host-Keys. **Achtung:** "
            "Die Serverprofile stehen in der Datei unverschlüsselt, auch bei aktiver "
            "App-Sperre — die Datei sicher aufbewahren. Beim Import wählt man die "
            "Bereiche aus; danach sollte die App neu gestartet werden.")},
        {_t("Panes & Tabs"),
         _t("- **Neuer Tab**: `Strg+Shift+N` oder *Aktionen → Neuer Tab*. Jeder Tab hat "
            "eigene Panes, Konsolen und eine eigene SSH-Verbindung. `Strg+Shift+E` "
            "benennt den Tab um, `Strg+W` schließt ihn.\n"
            "- Tabs lassen sich verschieben; der Titel zeigt die Verbindung, die "
            "*Tab-Farbe* des Profils färbt ihn.\n"
            "- Die **aktive Pane** ist blau umrandet, `Tab` wechselt die Seite. Verbinden "
            "und andere Aktionen wirken auf sie.\n"
            "- Menü *Panes*: **Nur Dateisystem anzeigen**, **Nur Terminal anzeigen**, "
            "**Panes untereinander anzeigen** (statt nebeneinander), **Panes tauschen** "
            "(`Strg+U`, Verbindung und Konsole wandern mit), **Panes synchronisieren** "
            "(`Strg+E`, die andere Pane springt in den Ordner der aktiven — nur bei "
            "gleichem Dateisystem), **Status anzeigen** (`Strg+F9`, folgt dem Cursor der "
            "anderen Pane) und **Befehl an beide Konsolen …** (`Strg+Shift+K`).\n"
            "- **Tab-Favoriten** (*Aktionen → Tab-Favoriten*) sichern die aktuelle "
            "Tab-Konstellation unter einem Namen und stellen sie später wieder her.\n"
            "- Mit *Tabs beim Start wiederherstellen* werden die offenen Tabs beim "
            "Beenden gesichert und beim Start wiederhergestellt.\n"
            "- Der **sudo-Chip**: siehe *sudo & andere Benutzer*.")},
        {_t("sudo & andere Benutzer"),
         _t("Bei Linux/Unix-Servern zeigt der Pane-Kopf den Chip **sudo**.\n"
            "\n"
            "- **Klick**: Die Pane arbeitet als **root**. Für root selbst und bei "
            "NOPASSWD ist kein Passwort nötig; sonst fragt die App nach dem eigenen "
            "sudo-Passwort — oder, wenn der Benutzer nicht in der Gruppe sudo, wheel oder "
            "admin ist, nach dem root-Passwort (`su`).\n"
            "- **Rechtsklick**: *Pane ausführen als …* listet die Benutzer des Servers "
            "(Systemkonten im Untermenü). Ein anderer Benutzer läuft über `sudo -u` "
            "(eigenes Passwort) oder `su` (Passwort des Zielbenutzers).\n"
            "- Als anderer Benutzer funktionieren Auflisten, Ansehen, Bearbeiten, "
            "Anlegen, Umbenennen, Löschen, Rechte und Übertragungen.\n"
            "- Der Chip zeigt den Modus (*sudo*, *sudo: name*, *su: name*), die Pane ist "
            "orange umrandet. Ein erneuter Klick oder *(angemeldet)* im Menü kehrt zum "
            "eigenen Benutzer zurück.\n"
            "- Die **Konsole** wechselt den Benutzer nicht — dort `sudo` oder `su` selbst "
            "eingeben.\n"
            "- Passwörter bleiben **nur im Speicher** und stehen nie auf der "
            "Befehlszeile.")},
        {_t("Sicherheit"),
         _t("- Passwörter, Passphrasen, Tokens und API-Schlüssel liegen im **Windows "
            "Credential Manager**, nie im Klartext auf der Platte. Server-Passwörter nur, "
            "wenn im Profil *Passwort/Passphrase sicher im OS-Keyring speichern* angehakt "
            "ist.\n"
            "- **Host-Key-Prüfung** je Profil: *Beim ersten Mal vertrauen (accept-new)*, "
            "*Strikt (nur bekannte)* oder *Ignorieren (unsicher)*.\n"
            "- Ein **geänderter** Host-Key bricht die Verbindung **vor der Anmeldung** ab "
            "— es gehen keine Zugangsdaten an den Server. Bei *Strikt* gilt das auch für "
            "unbekannte Keys.\n"
            "- Bei *accept-new* zeigt die App den Fingerprint eines **neuen** Servers "
            "**vor der Anmeldung**: *Vertrauen und speichern* merkt ihn, *Nur diesmal "
            "verbinden* gilt für diesen Tab (auch beim automatischen Neuverbinden), "
            "*Abbrechen* bricht ab. Erst nach der Bestätigung meldet sich die App an; das "
            "gilt auch für einen Sprung-Host (ProxyJump).\n"
            "- Keys, denen das System-`ssh` bereits vertraut (`~/.ssh/known_hosts`), "
            "werden übernommen; bestätigte Keys trägt die App dort ebenfalls ein.\n"
            "- Die Statusleiste zeigt, ob der Host-Key bekannt, neu oder ungeprüft ist. "
            "*Tools → Bekannte Host-Keys …* zeigt und bereinigt den Speicher der App — "
            "nötig, wenn ein Server neu aufgesetzt wurde.\n"
            "- Das sudo-Passwort bleibt nur im Speicher und steht nie auf der "
            "Befehlszeile.\n"
            "- Zum Ansehen oder Bearbeiten heruntergeladene Remote-Dateien werden beim "
            "Beenden gelöscht.\n"
            "- *Tools → Sicherheits-Audit (CVE) …* prüft OS, Pakete, `sshd_config`, "
            "Firewall, offene Ports und Konten und gleicht Kernkomponenten mit "
            "**OSV.dev** ab.\n"
            "- Die **App-Sperre** verschlüsselt zusätzlich die Serverprofile (siehe "
            "*App-Sperre & Zwei-Faktor*).\n"
            "- Der KI-Assistent führt nichts aus; Cloud-Anbieter erhalten Inhalte erst "
            "nach Zustimmung.")},
        {_t("App-Sperre & Zwei-Faktor"),
         _t("*Einstellungen → Sicherheit* schützt die App mit einem Passwort, das beim "
            "Start abgefragt wird.\n"
            "\n"
            "- **Passwort beim Start der App abfragen** legt das App-Passwort fest "
            "(mindestens 8 Zeichen). *Passwort ändern …* ändert es; Ausschalten verlangt "
            "das aktuelle Passwort.\n"
            "- Solange die Sperre aktiv ist, sind die **Serverprofile verschlüsselt** "
            "(AES-256-GCM). Einstellungen, Verlauf, Lesezeichen und Tab-Favoriten bleiben "
            "unverschlüsselt; Server-Passwörter liegen ohnehin im Credential Manager.\n"
            "- **Zwei-Faktor-Authentifizierung (Authenticator-App)**: den QR-Code mit "
            "einer Authenticator-App scannen (oder den Schlüssel abtippen), einen "
            "Bestätigungscode eingeben und *Aktivieren*.\n"
            "- Danach erscheinen **8 Wiederherstellungscodes** — nur dieses eine Mal. "
            "Kopieren oder als Datei speichern und sicher aufbewahren. Jeder Code gilt "
            "einmal anstelle des Bestätigungscodes; *Neue Wiederherstellungscodes …* "
            "ersetzt alle.\n"
            "- **Entsperren**: das Passwort und, falls aktiv, der 6-stellige Code oder "
            "ein Wiederherstellungscode. Nach wiederholten Fehlversuchen wächst die "
            "Wartezeit (bis 60 s).\n"
            "- Das Geheimnis der Authenticator-App ist an das Windows-Konto gebunden; "
            "unter einem anderen Konto helfen nur die Wiederherstellungscodes.\n"
            "- Gesperrt wird beim Start der App.\n"
            "\n"
            "**Passwort vergessen?** Es lässt sich nicht zurücksetzen. Wird die Sperre "
            "durch Löschen von `applock.json` umgangen, legt die App die verschlüsselten "
            "Profile als `servers.json.locked-…` beiseite und startet mit leerer "
            "Profilliste.")},
        {_t("Absturzberichte"),
         _t("Stürzt die App ab, schreibt sie einen Bericht nach "
            "`%APPDATA%\\ncssh\\crashes`: eine Minidump-Datei (`.dmp`) und eine kurze "
            "Textdatei mit Fehlercode und Aufrufkette. Die Textdatei enthält nur "
            "Adressen, keine Datei- oder Sitzungsinhalte.\n"
            "\n"
            "Beim nächsten Start weist die App einmal darauf hin; *Ordner öffnen* zeigt "
            "die Dateien. Abgefangene interne Fehler landen in `errors.log` im selben "
            "Ordner.\n"
            "\n"
            "Für eine Fehlermeldung (z. B. als GitHub-Issue) beide Dateien des Absturzes "
            "anhängen.")},
        {_t("Tastenkürzel"),
         _t("Die vollständige, aktuell konfigurierte Liste steht im Reiter "
            "**Tastenkürzel** dieses Fensters (*Hilfe → Tastenkürzel*); ändern lässt sie "
            "sich unter *Einstellungen → Tastenkürzel*.\n"
            "\n"
            "Fest vergeben (nicht konfigurierbar): `Strg+Q` (Beenden), `Strg+Shift+K` "
            "(Befehl an beide Konsolen), `Strg+F2` (Vorschau-Panel), `Backspace` (hoch), "
            "`Enter` (öffnen bzw. mit dem Standardprogramm starten), `Strg+F` "
            "(Schnellfilter der Pane bzw. Suche in der Konsolenausgabe), `Esc` (Filter "
            "leeren und schließen), `Strg+Shift+C`/`Strg+Shift+V` (Terminal "
            "kopieren/einfügen), `Shift+Bild↑/↓` (Scrollback), `Strg+Shift+F` (im "
            "Terminal-Puffer suchen) sowie die Editor-Tasten `Strg+S`, `Strg+Shift+S`, "
            "`Strg+F` und `Strg+G`.")},
    };
    return list;
}
} // namespace

HelpDialog::HelpDialog(int startTab, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(_t("Hilfe"));
    resize(900, 640);

    auto *layout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    tabs->addTab(buildShortcutsTab(), _t("Tastenkürzel"));
    tabs->addTab(buildGuideTab(), _t("Handbuch"));
    tabs->setCurrentIndex(startTab);
    layout->addWidget(tabs, 1);

    auto *closeBtn = new QPushButton(_t("Schließen"), this);
    closeBtn->setDefault(true);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    layout->addWidget(closeBtn);
}

QWidget *HelpDialog::buildShortcutsTab()
{
    m_shortcuts = new QTreeWidget(this);
    m_shortcuts->setHeaderLabels({_t("Aktion"), _t("Kürzel")});
    m_shortcuts->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_shortcuts->setAlternatingRowColors(true);

    const QHash<QString, QString> current = core::getShortcuts();
    QHash<QString, QTreeWidgetItem *> groups;
    for (const QString &group : core::groupOrder()) {
        auto *item = new QTreeWidgetItem(m_shortcuts, {group});
        item->setExpanded(true);
        groups.insert(group, item);
    }
    for (const core::ShortcutDef &def : core::shortcutDefs()) {
        QTreeWidgetItem *parent = groups.value(def.group, nullptr);
        if (!parent) {
            parent = new QTreeWidgetItem(m_shortcuts, {def.group});
            parent->setExpanded(true);
            groups.insert(def.group, parent);
        }
        new QTreeWidgetItem(parent, {def.label, current.value(def.id)});
    }

    // Feste Gesten und Tasten, die nicht ueber die Einstellungen belegbar sind —
    // sie stehen nirgends sonst, waeren aber genau das, was man hier sucht.
    struct FixedBinding {
        QString group;
        QString action;
        QString keys;
    };
    const FixedBinding fixed[] = {
        {_t("Navigation"), _t("Ordner öffnen / Datei ansehen"), _t("Doppelklick")},
        {_t("Navigation"), _t("Ordner öffnen / Datei mit dem Standardprogramm öffnen"),
         QStringLiteral("Enter")},
        {_t("Navigation"), _t("Zurück / Vor"), QStringLiteral("Alt+←  /  Alt+→")},
        {_t("Navigation"), _t("Übergeordneter Ordner"), QStringLiteral("Backspace")},
        {_t("Navigation"), _t("Pane wechseln"), QStringLiteral("Tab")},
        {_t("Navigation"), _t("Pane filtern"), QStringLiteral("Strg+F")},
        {_t("Navigation"), _t("Panes synchronisieren / tauschen"),
         _t("Menü Panes")},
        {_t("Dateien"), _t("Markieren"), _t("Space / Einfg")},
        {_t("Dateien"), _t("Nach Muster markieren / aufheben"), QStringLiteral("Num +  /  Num −")},
        {_t("Dateien"), _t("Auswahl umkehren"), QStringLiteral("Num *")},
        {_t("Dateien"), _t("Alles markieren (Strg+A)"), QStringLiteral("Strg+A")},
        {_t("Dateien"), _t("Kopieren / Einfügen (in diese Pane)"),
         QStringLiteral("Strg+C  /  Strg+V")},
        {_t("Dateien"), _t("Kontextmenü (Rechte, Eigenschaften, …)"), _t("Rechtsklick")},
        {_t("Dateien"), _t("Ausführen — mit OS-Standardprogramm öffnen"),
         _t("Enter / Kontextmenü")},
        {_t("Dateien"), _t("Übertragen"), _t("Ziehen + loslassen")},
        {_t("Konsole / Terminal"), _t("Historie"), QStringLiteral("↑  /  ↓")},
        {_t("Konsole / Terminal"), _t("Scrollback"), _t("Shift+Bild↑ / ↓")},
        {_t("Konsole / Terminal"), _t("Auswahl kopieren"),
         _t("Strg+Shift+C / Strg+Einfg / Strg+C bei Auswahl")},
        {_t("Konsole / Terminal"), _t("Einfügen"), _t("Strg+Shift+V / Strg+V / Shift+Einfg")},
        {_t("Konsole / Terminal"), _t("Wort markieren"), _t("Doppelklick")},
        {_t("Konsole / Terminal"), _t("Im Puffer suchen"), QStringLiteral("Strg+Shift+F")},
        {_t("Konsole / Terminal"), _t("In der Ausgabe suchen (Befehle-Modus)"),
         QStringLiteral("Strg+F")},
        {_t("Konsole / Terminal"), _t("Laufenden Befehl abbrechen (Befehle-Modus)"),
         _t("Strg+C / Esc")},
    };
    QHash<QString, QTreeWidgetItem *> fixedGroups;
    for (const FixedBinding &binding : fixed) {
        const QString &group = binding.group;
        QTreeWidgetItem *parent = fixedGroups.value(group, nullptr);
        if (!parent) {
            parent = new QTreeWidgetItem(m_shortcuts,
                                         {_t("Maus & feste Tasten") + QStringLiteral(" — ")
                                          + group});
            parent->setExpanded(true);
            fixedGroups.insert(group, parent);
        }
        new QTreeWidgetItem(parent, {binding.action, binding.keys});
    }
    return m_shortcuts;
}

QWidget *HelpDialog::buildGuideTab()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    m_search = new QLineEdit(page);
    m_search->setPlaceholderText(_t("Themen durchsuchen …"));
    connect(m_search, &QLineEdit::textChanged, this, &HelpDialog::filterTopics);
    layout->addWidget(m_search);

    auto *splitter = new QSplitter(Qt::Horizontal, page);
    m_topics = new QListWidget(splitter);
    connect(m_topics, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0)
            return;
        showTopic(m_topics->item(row)->data(Qt::UserRole).toInt());
    });
    m_guide = new QTextBrowser(splitter);
    m_guide->setOpenExternalLinks(true);
    splitter->addWidget(m_topics);
    splitter->addWidget(m_guide);
    splitter->setSizes({230, 640});
    layout->addWidget(splitter, 1);

    filterTopics(QString());
    if (m_topics->count() > 0)
        m_topics->setCurrentRow(0);
    return page;
}

void HelpDialog::filterTopics(const QString &needle)
{
    m_topics->clear();
    const QString lower = needle.trimmed().toLower();
    const auto &all = topics();
    for (int i = 0; i < int(all.size()); ++i) {
        if (!lower.isEmpty()
            && !all[i].title.toLower().contains(lower)
            && !all[i].body.toLower().contains(lower))
            continue;
        auto *item = new QListWidgetItem(all[i].title, m_topics);
        item->setData(Qt::UserRole, i);
    }
    if (m_topics->count() > 0)
        m_topics->setCurrentRow(0);
    else
        m_guide->setHtml(QStringLiteral("<i>%1</i>").arg(_t("Kein Treffer.")));
}

void HelpDialog::showTopic(int index)
{
    const auto &all = topics();
    if (index < 0 || index >= int(all.size()))
        return;
    m_guide->setHtml(QStringLiteral("<h2>%1</h2>%2")
                         .arg(all[index].title.toHtmlEscaped(),
                              core::mdToHtml(all[index].body)));
}

} // namespace ncssh::gui
