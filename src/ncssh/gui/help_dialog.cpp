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

// Handbuch-Themen (feldgenaue Bedienungsdoku wie im Original).
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
         _t("- **Menüleiste**: *Aktionen · Tools · Clipboard · Ansicht · Hilfe*.\n"
            "- **Tableiste**: ein Tab je Arbeitsbereich; der Titel zeigt die Verbindung.\n"
            "- **Pane-Kopf**: Titel, `sudo`-Chip (nur remote/Linux), darunter die "
            "Pfadzeile mit ↑ (hoch), ☆ (Lesezeichen merken), ▾ (Lesezeichen-Liste) "
            "und ⟳ (neu laden).\n"
            "- **Konsolen-Kopf**: `KI` erklärt die Ausgabe, `Terminal` schaltet den "
            "Modus um, `⤢` dockt die Konsole in ein eigenes Fenster ab.\n"
            "- **Statusleiste**: Meldungen, Transfer-Ergebnisse, Alarm-Ereignisse.")},
        {_t("Server-Manager & Verbinden"),
         _t("`F9` bzw. *Aktionen → SSH verbinden* öffnet die **Server-Profile**.\n\n"
            "Felder: **Name** (frei), **Host**, **Port** (Standard 22), **Benutzer**, "
            "**Auth-Methode**, **Schlüssel**, **Passwort**, **Host-Key-Richtlinie** "
            "und **Startverzeichnis**.\n\n"
            "- **Auth-Methode** `key`: Schlüsseldatei wählen (auch PuTTY-PPK — wird "
            "automatisch konvertiert). `password`: Passwort abfragen. `agent`: "
            "Pageant bzw. OpenSSH-Agent.\n"
            "- 🔑 öffnet den **Schlüssel-Dialog** (erzeugen/konvertieren); der Pfad "
            "wird danach ins Profil übernommen.\n"
            "- **Import** übernimmt Sitzungen aus PuTTY (Registry), WinSCP und "
            "`~/.ssh/config`.\n"
            "- Doppelklick auf ein Profil verbindet direkt.")},
        {_t("Dateien verwalten"),
         _t("- **Navigieren**: Doppelklick öffnet, `Backspace` geht hoch, der Pfad "
            "lässt sich direkt eintippen.\n"
            "- `F3` Ansehen · `F4` Bearbeiten · `F5` Übertragen · `F6` Umbenennen · "
            "`F7` Neuer Ordner · `F8` Löschen.\n"
            "- **Mehrfachauswahl** mit Strg/Shift; alle F-Tasten arbeiten darauf.\n"
            "- **Spalten-Sortierung**: Klick auf den Kopf (Name/Größe/Datum/Rechte); "
            "erneuter Klick dreht die Richtung. Ordner bleiben oben.\n"
            "- **Filter** `Strg+F`: blendet eine Zeile ein (`*.log` oder Teiltext); "
            "`Esc` blendet sie wieder aus.\n"
            "- **Drag & Drop**: zwischen den Panes ziehen (auch remote) oder aus dem "
            "Explorer hineinfallen lassen.\n"
            "- Ausführbare Dateien werden farblich hervorgehoben.")},
        {_t("Ansehen & Bearbeiten"),
         _t("- `F3` **Ansehen**: Text schreibgeschützt; Bilder (PNG/JPG/GIF/SVG …) "
            "erscheinen als Vorschau mit Maßen und Dateigröße.\n"
            "- `F4` **Bearbeiten** öffnet den Editor mit **Syntax-Highlighting** "
            "(JSON, XML/HTML, YAML, Python, INI/TOML, Shell), **Zeilennummern** und "
            "**Minimap**.\n"
            "- `Strg+S` speichert, `Strg+Shift+S` speichert unter, `Strg+F` sucht, "
            "`Strg+G` springt zu einer Zeile.\n"
            "- Suchtreffer werden in der Minimap farbig markiert.\n"
            "- **Großdatei-Schutz**: sehr große Dateien öffnen nur lesend.\n"
            "- Bei ungespeicherten Änderungen wird vor dem Schließen gefragt.\n"
            "- **KI erklären** untersucht die Datei bzw. beantwortet eine Frage dazu.")},
        {_t("Terminal / Konsole"),
         _t("Jede Pane hat eine eigene Konsole mit zwei Modi:\n\n"
            "- **Befehle**: Befehl → Ausgabe. `↑`/`↓` blättert durch die Historie, "
            "`Strg+C` bricht den laufenden Befehl ab. Ein `cd` synchronisiert die Pane, "
            "und ein Verzeichniswechsel der Pane setzt das CWD der Konsole.\n"
            "- **Terminal**: echter PTY-Shell-Channel (lokal über ConPTY, remote über "
            "SSH). `top`, `htop`, `vim`, `nano`, Farben, Tab-Completion und `Strg-C` "
            "funktionieren.\n\n"
            "- `Strg+Shift+C` / `Strg+Shift+V` kopiert bzw. fügt im Terminal ein.\n"
            "- `Shift+Bild↑/↓` blättert im Scrollback.\n"
            "- Beim Verzeichniswechsel der Pane wird ein `cd` ins Terminal geschickt.\n"
            "- `⤢` löst die Konsole in ein eigenes Fenster; die Pane füllt dann die "
            "ganze Spalte. Fenster schließen dockt wieder an.")},
        {_t("Übertragungen (Transfer-Queue)"),
         _t("`F5` öffnet den **Bestätigungsdialog**: Zielordner (vorbelegt mit dem Pfad "
            "der anderen Seite, editierbar oder per *Durchsuchen…*), bei genau einer "
            "Datei zusätzlich der Zielname — Verschieben und Umbenennen in einem Schritt. "
            "Die Ziel-Spalte aktualisiert sich live.\n\n"
            "`Strg+T` öffnet die **Queue**: Fortschritt, Tempo und ETA je Auftrag, "
            "Größen-Verifikation (✓), **Abbrechen**, **Wiederholen** und *Fertige "
            "entfernen*.\n\n"
            "Der Ordner-Browser läuft über die Provider-Abstraktion und funktioniert "
            "deshalb auch auf entfernten Servern.")},
        {_t("Befehlspalette & Assistent"),
         _t("`Strg+P` öffnet den **Befehlskatalog** — sortierbar nach Befehl, Kategorie, "
            "Plattform und Beschreibung.\n\n"
            "- **OS-Filter**: Aktuelles OS · Alle · Nur Linux · Nur Windows · "
            "Plattformübergreifend. Das Server-OS wird beim Verbinden erkannt.\n"
            "- **Gefährliche Befehle** sind rot markiert.\n"
            "- **Assistent…** öffnet den Parameter-Editor: Textfelder, Dropdowns und "
            "Checkboxen mit **Live-Vorschau**. Unter Linux zusätzlich **sudo** "
            "(optional als anderer Benutzer).\n"
            "- *Einfügen* schreibt den Befehl in die Konsole, *Ausführen* startet ihn.")},
        {_t("Datei-Suche (nach Name)"),
         _t("`Strg+Shift+F` sucht nach Dateinamen.\n\n"
            "- **Muster**: Platzhalter (`*.log`) oder **Regex**.\n"
            "- **Nur Dateien (Globs)** und **Ordner ausschließen** (je kommagetrennt).\n"
            "- **Groß/Klein egal** ist voreingestellt.\n"
            "- Treffer laufen live ein; **Stopp** bricht ab.\n"
            "- Doppelklick auf einen Treffer springt in dessen Ordner.")},
        {_t("Inhalts-Suche (grep)"),
         _t("`Strg+Alt+F` durchsucht Datei-**Inhalte**.\n\n"
            "- **Suchbegriff** wörtlich oder als **Regex**.\n"
            "- **Ganzes Wort** grenzt auf Wortgrenzen ein.\n"
            "- **Binärdateien** werden standardmäßig übersprungen.\n"
            "- **Kontext** zeigt N Zeilen vor und nach dem Treffer.\n"
            "- Ergebniszeilen haben die Form `pfad:zeile:text`; Doppelklick öffnet den "
            "Ordner der Datei.")},
        {_t("Massen-Umbenennen"),
         _t("`Strg+Shift+R` benennt die markierten Dateien in einem Rutsch um.\n\n"
            "**Suchen & Ersetzen** — Modus *Text* (wörtlich), *Platzhalter* (`*`/`?`) "
            "oder *Regex*; optional Groß/Klein egal.\n\n"
            "**Text & Endung** — Präfix, Suffix, Schreibweise (klein/GROSS/Wortanfänge/"
            "Satzanfang), Leerzeichen (behalten/`_`/`-`/entfernen) und Endung "
            "(unverändert/klein/GROSS/setzen).\n\n"
            "**Nummerierung** — Start, Schritt, Stellen, Trenner, Position (vorne/hinten) "
            "sowie die **Reihenfolge** für die Nummernvergabe (Eingabe, Name, "
            "natürlich `1,2,10`, Endung).\n\n"
            "Die **Vorschau** zeigt Alt → Neu; geänderte Namen sind grün, Konflikte rot. "
            "*Konflikte automatisch nummerieren* hängt ` (1)`, ` (2)` … an. Ausgeführt "
            "wird in einer **gefahrlosen Reihenfolge** — auch Tausch (`a↔b`) und Ketten "
            "(`a→b→c`) funktionieren über temporäre Zwischennamen.")},
        {_t("Datei-Encoding konvertieren"),
         _t("*Tools → Datei-Encoding konvertieren* wandelt Textdateien zwischen "
            "Zeichensätzen um — UTF-8/16/32, Windows-1250/1251/1252, ISO 8859-1/15, "
            "DOS/OEM 437/850, Mac Roman, KOI8-R, Shift-JIS, GBK, Big5 sowie **EBCDIC** "
            "(cp037, cp500, cp273, cp1140, cp1141, cp1047, cp875).\n\n"
            "- Das **Quell-Encoding wird automatisch erkannt** und vorgewählt.\n"
            "- Die **Vorschau** zeigt die Quelle mit dem gewählten Codec.\n"
            "- **Fehlerstrategie**: streng (melden), ersetzen oder ignorieren.\n"
            "- Ausgabe in eine neue Datei oder **Original überschreiben**.")},
        {_t("Datei- & Verzeichnis-Vergleich"),
         _t("- `Strg+Shift+D` **Datei-Vergleich**: zwei markierte Dateien als farbiger "
            "Unified-Diff (grün = hinzugefügt, rot = entfernt), mit Zeilenbilanz.\n"
            "- `Strg+D` **Verzeichnis-Vergleich**: stellt beide Panes gegenüber "
            "(*nur links*, *nur rechts*, *links/rechts neuer*, *gleich*), optional "
            "**rekursiv**. *Nur Unterschiede* blendet Gleiches aus.\n"
            "- Markierte Einträge lassen sich per Klick **angleichen** — die Übertragung "
            "läuft über die Transfer-Queue.")},
        {_t("venv verwalten"),
         _t("*Tools → venv verwalten* legt virtuelle Python-Umgebungen an.\n\n"
            "- **Projektordner**, **venv-Ordner** (Standard `.venv`), gefundene "
            "**Python-Versionen** zur Auswahl und der **Installationsbefehl** "
            "(automatisch erkannt, z. B. aus `requirements.txt`).\n"
            "- `--skip-lock` / `--no-deps` hängt die passende Option an.\n"
            "- Die **Befehlsvorschau** zeigt, was ausgeführt wird.\n"
            "- Darunter die **bekannten Umgebungen** mit Typ (venv/pipenv), Version, "
            "Projekt und Pfad; Doppelklick aktiviert eine Umgebung in der Konsole, "
            "*Umgebung löschen* entfernt sie.")},
        {_t("Makro-Manager"),
         _t("*Tools → Makro-Manager* bietet ein Raster frei belegbarer Tasten.\n\n"
            "- **Layer** (Seiten) links: anlegen, bearbeiten (Name, zugeordnetes "
            "Programm, Zeilen × Spalten), löschen. Import/Export als JSON.\n"
            "- **Bearbeiten-Modus**: Klick auf eine Taste öffnet den Editor. "
            "**Ausführen-Modus**: Klick löst die Aktion aus, langes Halten öffnet "
            "trotzdem den Editor.\n"
            "- *Layer automatisch zum Programm wechseln* beobachtet das "
            "Vordergrund-Programm und schaltet passend um.\n\n"
            "Im **Tasten-Editor**: Beschriftung samt Position, Icon (als Hintergrund), "
            "Schriftfarbe/-art und die **Aktion**. Je nach Aktionstyp erscheint der "
            "passende Payload-Editor — Text, Zahl, Layer, Fenster, SSH-Befehl, JSON "
            "oder eine **Sequenz** mehrerer Schritte.\n\n"
            "Aktionen umfassen Programme starten, Tastatur/Maus simulieren, Fenster "
            "verwalten, Medien steuern, HTTP-Anfragen und Befehle an die Konsolen.")},
        {_t("Plugins"),
         _t("*Tools → Plugins* bindet eigenständige Programme ein.\n\n"
            "- **Programm** (relativ zum `plugins/`-Ordner oder absolut), **Parameter** "
            "mit Platzhalter `{path}` für das gewählte Element, **Arbeitsverzeichnis**.\n"
            "- **Im Kontextmenü anzeigen** blendet das Plugin in der Pane ein; "
            "*Gilt für* schränkt auf Dateien oder Ordner ein.\n"
            "- **Testen** startet das Plugin sofort.\n"
            "- Zentral bereitgestellte Plugins (aus `plugins/plugins.json`) sind "
            "schreibgeschützt und mit *(zentral)* markiert.")},
        {_t("Netzwerkscanner"),
         _t("*Tools → Netzwerk-Scanner* durchsucht das lokale Netz.\n\n"
            "- **Ziele**: CIDR (`192.168.1.0/24`), Bereiche (`10.0.0.1-50`), Listen "
            "oder einzelne Namen. Die lokale /24 ist vorbelegt.\n"
            "- **Ports**: Voreinstellungen (Gängige, SMB, Web, Fernzugriff, Alle) oder "
            "eigene (`22,80,8000-8100`).\n"
            "- Optionen: Ping, nur erreichbare, Namen auflösen, Freigaben, "
            "Dienste erkennen.\n"
            "- Die Tabelle zeigt IP, Name, MAC, **Hersteller** (aus der OUI-Tabelle), "
            "OS-Schätzung, offene Ports mit Dienstnamen sowie Weboberfläche und "
            "Freigaben. Doppelklick öffnet eine gefundene Weboberfläche.\n"
            "- Beim Schließen werden die Hosts als **`net://`-Dateisystem** in die "
            "aktive Pane übernommen: Host → Freigabe → Dateien.")},
        {_t("Alarm Trigger (Datei-Alarm)"),
         _t("*Tools → Datei-Alarm* überwacht Ordner auf Änderungen.\n\n"
            "- Je Alarm: **Name**, **Ordner**, überwachte **Ereignisse** (Neu, Geändert, "
            "Gelöscht), **Unterordner einbeziehen**, **Ordner mitzählen** und *aktiv*.\n"
            "- Die Überwachung läuft per Schnappschuss-Vergleich im Hintergrund; "
            "Ereignisse erscheinen in der Liste und in der Statusleiste.")},
        {_t("GitHub Repo Alarm"),
         _t("*Tools → GitHub-Alarm* meldet neue Pushes.\n\n"
            "- **Repository** als `owner/repo` oder als GitHub-URL (auch "
            "`git@github.com:owner/repo.git`).\n"
            "- Ein optionales **Token** erhöht das API-Limit und liegt im "
            "Schlüsselbund, nicht im Klartext.\n"
            "- Geprüft wird der Zeitstempel des letzten Pushs; ändert er sich, gibt es "
            "eine Meldung. *Jetzt prüfen* fragt sofort ab.")},
        {_t("Zwischenablage-Verwaltung"),
         _t("*Clipboard → Clipboard-Manager* führt eine Historie aller kopierten Texte.\n\n"
            "- Doppelklick setzt einen Eintrag als **aktiven** Inhalt der Zwischenablage.\n"
            "- *Einfügen* schreibt den Eintrag in die aktive Konsole.\n"
            "- Einzelne Einträge oder die komplette Liste lassen sich löschen.")},
        {_t("Eigenschaften & Rechte (chmod)"),
         _t("Kontextmenü → **Eigenschaften** zeigt Name, Pfad, Typ, Größe, Zeitstempel, "
            "Eigner/Gruppe und ein evtl. Symlink-Ziel. Bei lokalen Ordnern wird die "
            "Größe rekursiv berechnet.\n\n"
            "Der **chmod-Editor** darunter hat rwx-Checkboxen für Eigner, Gruppe und "
            "Andere sowie ein **Oktal-Feld** — beide Darstellungen halten sich "
            "gegenseitig aktuell. *Übernehmen* schreibt die Rechte.")},
        {_t("SSH-Schlüssel"),
         _t("Im Server-Manager öffnet 🔑 den **Schlüssel-Dialog**.\n\n"
            "- **Erzeugen**: Ed25519 (empfohlen), RSA 4096/3072 oder ECDSA nistp256, "
            "mit optionalem Kommentar.\n"
            "- Der **öffentliche Schlüssel** wird angezeigt und lässt sich kopieren — "
            "er gehört in `~/.ssh/authorized_keys` auf dem Server.\n"
            "- **Speichern** legt Privat- und `.pub`-Datei ab; der Privatschlüssel "
            "erhält restriktive Rechte.\n"
            "- **Konvertieren**: OpenSSH → PPK und PPK → OpenSSH.\n"
            "- Ein gespeicherter Pfad wird direkt ins Profil übernommen.")},
        {_t("SSH-Tunnel / Port-Weiterleitung"),
         _t("`Strg+Shift+T` öffnet Port-Weiterleitungen (Tab muss verbunden sein):\n\n"
            "- **Lokal (-L)**: ein lokaler Port wird auf ein Ziel hinter dem Server "
            "geleitet — z. B. `127.0.0.1:8080` → `localhost:80` auf dem Server.\n"
            "- **Remote (-R)**: ein Port auf dem Server zeigt auf ein lokales Ziel.\n"
            "- **Dynamisch (-D)**: SOCKS5-Proxy über die SSH-Verbindung.\n\n"
            "Offene Tunnel stehen in der Liste und lassen sich einzeln **stoppen**; "
            "beim Schließen des Tabs werden sie automatisch beendet.")},
        {_t("KI-Funktionen"),
         _t("Der Assistent nutzt ein **lokales Modell über Ollama** — Inhalte verlassen "
            "den Rechner nicht, und der Assistent ist **rein beratend**: er führt nichts "
            "aus.\n\n"
            "- Aktivieren in *Einstellungen → KI*: Adresse testen, Modelle laden, "
            "Modell wählen.\n"
            "- **KI** im Konsolen-Kopf erklärt die letzte Terminalausgabe bzw. einen "
            "Fehler.\n"
            "- **KI erklären** im Editor untersucht die geöffnete Datei oder beantwortet "
            "eine konkrete Frage dazu.\n"
            "- Antworten erscheinen in einem Chat-Panel mit **Folgefragen**.")},
        {_t("Ansicht & Designs (Themes)"),
         _t("*Ansicht → Theme* schaltet zwischen **Dunkel**, **Mitternacht**, **Hell** "
            "und **Hoher Kontrast** um; die Wahl wird gespeichert und gilt auch für das "
            "Terminal.\n\n"
            "*Ansicht → Theme-Editor* erstellt eigene Farbschemata: eine Basis wählen, "
            "die Farbfelder anklicken (Hintergrund, Flächen, Rahmen, Text, Akzente, "
            "Scrollbalken, Terminal-Farben) und unter eigenem Namen speichern. Die "
            "**Vorschau** unten zeigt das Ergebnis sofort.\n\n"
            "*Ansicht → Vorschau* blendet unter jeder Pane ein Vorschau-Panel ein, das "
            "die markierte Datei schreibgeschützt anzeigt (Text oder Bild).")},
        {_t("Lesezeichen"),
         _t("Pfad-Lesezeichen werden **je Verbindung** getrennt geführt (Profilname "
            "bzw. `local`).\n\n"
            "- ☆ in der Pfadzeile merkt den aktuellen Pfad (★ = bereits gemerkt).\n"
            "- ▾ öffnet die Liste: Doppelklick springt hin, Einträge lassen sich "
            "entfernen.\n"
            "- **Exportieren/Importieren** teilt Lesezeichen zwischen Rechnern.")},
        {_t("Einstellungen"),
         _t("`Strg+,` öffnet die Einstellungen mit drei Reitern:\n\n"
            "**Allgemein** — Sprache (DE/EN), Theme, Schriftgrößen für Editor, Terminal "
            "und Panes, **Datumsformat** (Token wie `DD.MM.YYYY HH24:MI`), versteckte "
            "Dateien ausblenden, **Programm-Logos vor Dateinamen**, Tab-Wiederherstellung "
            "und Standard-Startpfad.\n\n"
            "**KI** — Ollama-Adresse testen, Modelle laden und auswählen.\n\n"
            "**Tastenkürzel** — alle Aktionen frei belegbar; beim Speichern werden "
            "**Dubletten gemeldet**. *Auf Standard zurücksetzen* stellt die Vorgaben "
            "wieder her.\n\n"
            "Sprache und Schriftgrößen greifen nach einem Neustart.")},
        {_t("Panes & Tabs"),
         _t("- **Neuer Tab**: `Strg+Shift+N` oder *Aktionen → Neuer Tab*. Jeder Tab hat "
            "eigene Panes, Konsolen und eine eigene SSH-Verbindung.\n"
            "- Tabs lassen sich verschieben und schließen; der Titel zeigt die Verbindung.\n"
            "- **Tab-Favoriten** (*Aktionen → Tab-Favoriten*) sichern die aktuelle "
            "Tab-Konstellation unter einem Namen und stellen sie später wieder her.\n"
            "- Bei aktivierter Tab-Wiederherstellung werden die offenen Tabs beim "
            "Beenden gesichert und beim Start automatisch wiederhergestellt.\n"
            "- Der **sudo-Chip** im Pane-Kopf (nur remote/Linux) schaltet die Pane auf "
            "das sudo-Dateisystem um: Auflisten, Ansehen, Bearbeiten, Anlegen, "
            "Umbenennen, Löschen und Rechte laufen dann als root. NOPASSWD wird erkannt; "
            "sonst wird das Passwort einmal abgefragt und **nur im Speicher** gehalten.")},
        {_t("Sicherheit"),
         _t("- Passwörter und Passphrasen liegen im **Windows Credential Manager**, "
            "nie im Klartext auf der Platte.\n"
            "- Der **Host-Key wird nach dem Handshake, aber vor jeder Authentifizierung "
            "geprüft** — bei unbekanntem oder geändertem Key wird **kein Passwort "
            "gesendet**.\n"
            "- Richtlinien: `accept-new` (Trust-on-First-Use mit Nachfrage), `strict` "
            "(unbekannter Key = Abbruch), `ignore` (keine Prüfung).\n"
            "- *Tools → Bekannte Host-Keys* zeigt und bereinigt den TOFU-Speicher — "
            "nötig, wenn ein Server neu aufgesetzt wurde.\n"
            "- Das sudo-Passwort wird über einen separaten Kanal gesendet und teilt "
            "sich nie einen Datenstrom mit Dateiinhalten.\n"
            "- *Tools → Sicherheits-Audit* prüft OS, Pakete, `sshd_config`, Firewall, "
            "offene Ports und Konten und gleicht Kernkomponenten mit **OSV.dev** ab.\n"
            "- Der KI-Assistent läuft lokal und führt nichts aus.")},
        {_t("Tastenkürzel"),
         _t("Die vollständige, aktuell konfigurierte Liste steht im Reiter "
            "**Tastenkürzel** dieses Fensters.\n\n"
            "Fest verdrahtet (nicht konfigurierbar): `Backspace` (hoch), `Esc` "
            "(Filter schließen), `Strg+Shift+C`/`Strg+Shift+V` (Terminal kopieren/"
            "einfügen), `Shift+Bild↑/↓` (Scrollback) sowie die Editor-Tasten "
            "`Strg+S`, `Strg+Shift+S`, `Strg+F` und `Strg+G`.")},
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
