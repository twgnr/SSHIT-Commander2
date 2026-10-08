# SSHIT-Commander — Benutzerhandbuch

> Dieses Handbuch steht auch in der App unter *Hilfe → Hilfe* (`F1`).
> Die Datei wird mit `tools/gen_user_guide.py` aus dem eingebauten Handbuch
> erzeugt — bitte nicht von Hand bearbeiten, sondern die Themen in
> `src/ncssh/gui/help_dialog.cpp` ändern und das Skript erneut ausführen.
>
> English version: [user-guide.md](user-guide.md)

## Inhalt

- [Überblick](#überblick)
- [Die Oberfläche](#die-oberfläche)
- [Server-Verwaltung & Verbinden](#server-verwaltung--verbinden)
- [Dateien verwalten](#dateien-verwalten)
- [Filter & Sortierung](#filter--sortierung)
- [Ansehen & Bearbeiten](#ansehen--bearbeiten)
- [Terminal / Konsole](#terminal--konsole)
- [Parameter-Fenster & Pfad-Vervollständigung](#parameter-fenster--pfad-vervollständigung)
- [Übertragungen (Transfer-Queue)](#übertragungen-transfer-queue)
- [SFTP-Batch & geplante Aufgaben](#sftp-batch--geplante-aufgaben)
- [Befehlspalette & Assistent](#befehlspalette--assistent)
- [Datei-Suche (nach Name)](#datei-suche-nach-name)
- [Inhalts-Suche (grep)](#inhalts-suche-grep)
- [Massen-Umbenennen](#massen-umbenennen)
- [Datei-Encoding konvertieren](#datei-encoding-konvertieren)
- [Datei- & Verzeichnis-Vergleich](#datei---verzeichnis-vergleich)
- [venv verwalten](#venv-verwalten)
- [Makro-Manager](#makro-manager)
- [Plugins](#plugins)
- [Netzwerkscanner](#netzwerkscanner)
- [Alarm Trigger (Datei-Alarm)](#alarm-trigger-datei-alarm)
- [GitHub Repo Alarm](#github-repo-alarm)
- [Zwischenablage-Verwaltung](#zwischenablage-verwaltung)
- [Eigenschaften & Rechte (chmod)](#eigenschaften--rechte-chmod)
- [SSH-Schlüssel](#ssh-schlüssel)
- [SSH-Tunnel / Port-Weiterleitung](#ssh-tunnel--port-weiterleitung)
- [Server-Info](#server-info)
- [KI-Funktionen](#ki-funktionen)
- [Ansicht & Designs (Themes)](#ansicht--designs-themes)
- [Lesezeichen](#lesezeichen)
- [Einstellungen](#einstellungen)
- [Panes & Tabs](#panes--tabs)
- [sudo & andere Benutzer](#sudo--andere-benutzer)
- [Sicherheit](#sicherheit)
- [App-Sperre & Zwei-Faktor](#app-sperre--zwei-faktor)
- [Absturzberichte](#absturzberichte)
- [Tastenkürzel](#tastenkürzel)
- [Tastenkürzel-Übersicht](#tastenkürzel-übersicht)

## Überblick

**SSHIT-Commander** ist ein Dual-Pane-Dateimanager mit integriertem SSH/SFTP-Terminal.

- Links und rechts je eine **Pane** — lokal oder remote, gleiche Bedienung.
- Unter jeder Pane eine **Konsole** mit zwei Modi: *Befehle* und *Terminal*.
- Beliebig viele **Tabs**, jeder mit eigener Verbindung.
- Die gesamte Netzwerkarbeit läuft auf Hintergrund-Threads — das Fenster friert bei SSH-Operationen oder Transfers nie ein.

## Die Oberfläche

- **Menüleiste**: *Aktionen · Tools · Plugins · Clipboard · Panes · Ansicht · Hilfe*.
- **Tableiste**: ein Tab je Arbeitsbereich; der Titel zeigt die Verbindung.
- **Pane-Kopf**: Titel und Chips — *Filter* (Filter & Sortierung), bei Verbindung *ⓘ Info* (Server-Info), *⏏ Trennen* und *sudo*. Darunter die Pfadzeile: Laufwerksauswahl (lokal; der Tooltip zeigt den freien Platz), Zurück/Vor (Rechtsklick: zuletzt besuchte Ordner), Hoch, der Breadcrumb (Klick auf die freie Fläche: Pfad eintippen; Rechtsklick: Pfad kopieren oder bearbeiten), Lesezeichen und Neu laden.
- **Pane-Statuszeile**: Anzahl und Größe der Einträge, aktive Filter und die Markierung.
- **Konsolen-Kopf**: Symbole für *Parameter* (nur bei einem bekannten Befehl), *Befehlspalette* und *Verlauf*, dann *KI* (Ausgabe erklären), *+* (weiteres Terminal), *Terminal* (Modus umschalten) und `⤢` (in ein eigenes Fenster abdocken). Während eines Verbindungsaufbaus zusätzlich *Verbindung abbrechen*.
- **Statusleiste**: Meldungen, Transfer-Ergebnisse, Alarm-Ereignisse, Host-Key-Status und die Zahl offener Tunnel.

## Server-Verwaltung & Verbinden

`F9`, *Aktionen → SSH verbinden* oder das Symbol in der Werkzeugleiste öffnen die **Server-Verwaltung**. Links stehen die Verbindungen mit Filterfeld, *Neuer Server* und *Import (PuTTY/WinSCP/SSH)*, rechts das Formular.

**Felder**: *Anzeigename*, *Host*, *Port* (22), *Benutzername*, *Authentifizierung* (*SSH-Key*, *Passwort* oder *SSH-Agent* — Pageant bzw. OpenSSH-Agent), *Key-Pfad* (auch PuTTY-PPK, wird beim Verbinden umgewandelt; 🔑 öffnet den Schlüssel-Dialog), *Passwort* mit *Passwort/Passphrase sicher im OS-Keyring speichern*, *Host-Key-Prüfung* (siehe *Sicherheit*), *ProxyJump*, *Startverzeichnis* und *Tab-Farbe*.

**Feinsteuerung**: *Keepalive* (Sekunden, 0 = aus), *Verbindungs-Timeout*, *Chiffren* und *Schlüsseltausch* (bevorzugte Verfahren, kommagetrennt). *SSH-Kompression* komprimiert den Datenverkehr (zlib) — sinnvoll bei langsamen Verbindungen. *SSH-Agent weiterreichen* unterstützt die SSH-Bibliothek nicht.

- **ProxyJump**: `[benutzer@]host[:port]` — ein Sprung-Host. Er meldet sich mit dem Key des Ziels an (bei Key-Anmeldung), sonst über den Agenten.
- **Umgebungsvariablen**: eine Zuweisung `NAME=Wert` je Zeile, `#` leitet einen Kommentar ein. Sie gelten für Terminal und Konsole. Der Server übernimmt nur, was seine `AcceptEnv`-Liste erlaubt; abgelehnte Namen meldet das Terminal.
- **Import** übernimmt Sitzungen aus PuTTY, WinSCP und `~/.ssh/config`; *Aus Datei importieren …* liest exportierte `.reg`- und `.ini`-Dateien. Passwörter werden nicht übernommen; schon vorhandene Profile sind markiert und nicht vorausgewählt.
- *Tunnel (Auto-Start)* listet die Tunnel, die sich beim Verbinden automatisch öffnen (angelegt im Tunnel-Dialog); *Entfernen* löscht einen, wirksam mit *Speichern*.
- *Zuletzt* zeigt die letzte erfolgreiche Verbindung, *Erreichbarkeit testen* prüft nur, ob der Port antwortet.

**Verbinden**: *Verbinden* oder Doppelklick. Die Verbindung landet in der **aktiven** (blau umrandeten) Pane; fehlende Angaben wie Benutzer, Passwort oder Passphrase fragt die App ab. Ist die andere Pane des Tabs schon verbunden, fragt sie: *In neuem Tab verbinden* oder *Verbindung ersetzen*. Den Fortschritt zeigt die Konsole der Pane; *Verbindung abbrechen* im Konsolen-Kopf bricht ab, nach 30 s ohne Antwort ist ohnehin Schluss. Reißt eine Verbindung ab, verbindet die App automatisch neu.

**Trennen**: *⏏ Trennen* im Pane-Kopf.

## Dateien verwalten

- **Navigieren**: Doppelklick öffnet einen Ordner bzw. zeigt eine Datei an (wie `F3`); `Enter` öffnet Ordner und startet Dateien mit dem Standardprogramm (Remote-Dateien als lokale Kopie). `Backspace` geht hoch, `Alt+←`/`Alt+→` zurück und vor, `Tab` wechselt die Pane. Den Pfad kann man im Breadcrumb direkt eintippen; Tippen in der Liste springt zum passenden Eintrag.
- `F3` Ansehen · `F4` Bearbeiten · `F5` Kopieren · `F6` Umbenennen · `F7` Neuer Ordner · `F8` Löschen — unter *Tools → Einstellungen → Tastenkürzel* frei belegbar.
- **Markieren**: `Space` oder `Einfg`, Strg/Shift-Klick, `Num +`/`Num −` nach Muster, `Num *` kehrt um, `Strg+A` markiert alles. Die F-Tasten arbeiten auf der Markierung.
- **Kopieren und Verschieben** auch mit `Strg+C`/`Strg+X` und `Strg+V` oder über das Kontextmenü (*Verschieben → andere Pane*).
- **Spalten**: Rechtsklick auf den Spaltenkopf blendet Größe, Geändert, Erstellt, Zugriff, Typ, Endung, Rechte und Eigner ein oder aus. Ein Klick auf einen Spaltenkopf sortiert danach, ein erneuter Klick dreht die Richtung. Mehrstufig sortieren und filtern: siehe *Filter & Sortierung*.
- **Schnellfilter** `Strg+F`: blendet eine Zeile ein (`*.log` oder Teiltext); `Esc` leert und schließt sie.
- **Drag & Drop**: zwischen den Panes ziehen (auch remote) oder aus dem Explorer hineinfallen lassen.
- *Ansicht → Kachelansicht* (oder das Kontextmenü) wechselt zwischen Liste und Kacheln.
- **Farben**: Ausführbare Dateien werden hervorgehoben (abschaltbar, Farbe wählbar in den Einstellungen). In lokalen Git-Repositories färbt sich der Name nach dem Git-Status: orange = geändert, grün = neu (auch noch nicht hinzugefügte Dateien und Ordner — dann auch alles darin), rot = gelöscht oder Konflikt, blau = umbenannt oder kopiert. Ein Ordner zeigt Änderungen in seinem Inneren an; der Tooltip nennt den Status.

## Filter & Sortierung

Der Chip **Filter** im Pane-Kopf öffnet *Filter und Sortierung* für diese Pane.

**Filter**

- *Anzeigen*: Dateien und Ordner, nur Dateien oder nur Ordner.
- *Name* (mehrere Muster mit `;` getrennt, Platzhalter `*` und `?`), *Beginnt mit*, *Endet mit* und *Regulärer Ausdruck*; optional *Groß-/Kleinschreibung beachten*.
- *Dateiendungen*.
- *Datum*: geändert oder erstellt — im Zeitraum, älter als oder jünger als (Minuten bis Jahre).
- *Größe*: ab und bis, in B, KB, MB oder GB.
- Ordner betreffen nur die Namens- und Datumsregeln, und auch die nur mit *Namens- und Datumsregeln auch auf Ordner anwenden*.

**Sortierung**: *Sortieren nach* und bis zu drei weitere Stufen (*dann nach*), je auf- oder absteigend; *Ordner zuerst* hält Ordner oben.

*Anwenden* zeigt das Ergebnis sofort, *Zurücksetzen* hebt alles auf. Solange ein Filter aktiv ist, ist der Chip hervorgehoben und die Statuszeile nennt die ausgeblendeten Einträge. Filter und Sortierung gelten je Pane, bleiben beim Ordnerwechsel erhalten und werden nicht gespeichert. Der Schnellfilter `Strg+F` wirkt zusätzlich.

## Ansehen & Bearbeiten

- `F3` **Ansehen**: Text schreibgeschützt (die ersten 200 KB); Bilder (PNG/JPG/GIF/SVG …) als Vorschau mit Maßen und Dateigröße.
- `F4` **Bearbeiten** öffnet den Editor mit **Syntax-Highlighting** für 22 Sprachen (u. a. Shell, PowerShell, Batch, Python, C/C++, C#, Java/Kotlin, JavaScript/TypeScript, Go, Rust, PHP, SQL, JSON, XML/HTML, YAML, INI/TOML, Markdown, Dockerfile und Makefile), **Zeilennummern** und **Minimap**. Die Sprache wird an Endung, Shebang und Inhalt erkannt; eine Wahl im Syntax-Feld merkt sich der Editor je Endung.
- **Zeichensatz**: Der Editor erkennt die Kodierung (BOM, UTF-16, UTF-8, sonst Windows-1252) und speichert in derselben zurück. Bei einer unveränderten Datei liest ein Wechsel im Kodierungsfeld die Datei neu ein. Passen Zeichen nicht in die Kodierung, bietet das Speichern UTF-8 an. *Encoding konvertieren …* öffnet den Konverter.
- **Zeilenende**: LF oder CRLF wird erkannt und so gespeichert, wie im Feld *Zeilenende* gewählt. *Umbruch* bricht lange Zeilen nur in der Anzeige um.
- **Suchen & Ersetzen** in der Leiste unter dem Text (*Aa* = Groß/Klein beachten, *Ersetzen*, *Alle*). Treffer erscheinen farbig in der Minimap; ein Klick in die Minimap springt dorthin.
- `Strg+S` speichert, `Strg+Shift+S` speichert unter, `Strg+F` sucht, `Strg+G` springt zu einer Zeile.
- **Großdatei-Schutz**: Dateien über 5 MB öffnen nur lesend und zeigen den Anfang.
- Ändert sich eine lokale Datei von außen, lädt der Editor sie neu — bei ungespeicherten Änderungen fragt er vorher. Auch vor dem Schließen mit ungespeicherten Änderungen wird gefragt.
- **KI erklären** und **KI Fehleranalyse**: siehe *KI-Funktionen*.

## Terminal / Konsole

Jede Pane hat eine eigene Konsole mit zwei Modi, umgeschaltet mit **Terminal** im Konsolen-Kopf.

**Befehle**: Befehl → Ausgabe.

- `↑`/`↓` blättert durch die gespeicherte Historie.
- `Strg+C`, `Esc` oder **■** bricht den laufenden Befehl ab.
- `Strg+F` sucht in der Ausgabe.
- `Tab` ergänzt Pfade; das Symbol **Parameter** zeigt die Optionen des getippten Befehls (siehe *Parameter-Fenster & Pfad-Vervollständigung*).
- Ein `cd` synchronisiert die Pane, und ein Ordnerwechsel der Pane setzt das Arbeitsverzeichnis der Konsole.

**Terminal**: eine echte Shell — lokal über ConPTY mit PowerShell oder cmd (*Einstellungen → Allgemein → Lokale Shell*), remote über SSH — mit vollem VT100/xterm-Emulator: `vim`, `htop`, `tmux`, `mc`, `less`, Farben und die Tab-Vervollständigung der Shell funktionieren.

- **Kopieren**: `Strg+Shift+C` oder `Strg+Einfg`; `Strg+C` kopiert, wenn Text markiert ist, und bricht sonst ab.
- **Einfügen**: `Strg+Shift+V`, `Strg+V` oder `Shift+Einfg`.
- Auf entfernten Linux-Shells ist `Strg+Z` Rückgängig; `Strg+Shift+Z` sendet ein echtes `^Z` (Programm anhalten).
- `Shift+Bild↑/↓` blättert im Scrollback (10 000 Zeilen), `Strg+Shift+F` sucht darin (`F3`/`Shift+F3` nächster bzw. vorheriger Treffer).
- In Vollbild-Programmen gehen Mausklicks und Mausrad an das Programm; mit gedrückter `Shift`-Taste markiert man trotzdem Text.
- Rechtsklick: Kopieren, Einfügen, Alles kopieren, Link öffnen, Leeren sowie **Mitschnitt starten …** — schreibt die Sitzung ohne Steuerzeichen in eine Datei, bis *Mitschnitt beenden*.
- **+** öffnet weitere Terminals als Reiter (Doppelklick benennt um). Nach dem Ende einer Shell startet `Enter` sie neu.
- Schriftart und -größe: *Einstellungen → Allgemein*.

**In beiden Modi**:

- `Strg+Shift+K` (*Panes → Befehl an beide Konsolen …*) schickt einen Befehl an beide Konsolen des Tabs.
- `⤢` löst die Konsole in ein eigenes Fenster; die Pane füllt dann die ganze Spalte. *⤵ Andocken* oder das Schließen des Fensters dockt wieder an.

## Parameter-Fenster & Pfad-Vervollständigung

**Pfade ergänzen**: In der Befehlszeile der Konsole (Modus *Befehle*) und in den Feldern des Parameter-Fensters ergänzt `Tab` das Wort vor dem Cursor zu einem Datei- oder Ordnernamen — lokal oder auf dem verbundenen Server. Jedes weitere `Tab` springt zum nächsten Treffer (Ordner zuerst); Namen mit Leerzeichen werden gequotet, nach `cd` gibt es nur Ordner. Im Terminal-Modus ergänzt die Shell selbst.

**Parameter-Fenster**: Erkennt die Konsole den getippten Befehl (aus dem Befehlskatalog oder als gängiges Unix-Werkzeug), erscheint im Konsolen-Kopf das Symbol **Parameter**. Es öffnet ein Fenster, das neben der Arbeit offen bleiben kann:

- **Argumente (in der Reihenfolge des Befehls)** — je ein Feld, z. B. Quelle und Ziel bei `cp`; *Variante* wählt zwischen mehreren Formen des Befehls. *Wird angefügt* zeigt das Ergebnis, *Argumente anfügen* hängt es an.
- **Optionen (Doppelklick fügt an)** — mit Filterfeld. Optionen aus dem Katalog sind fett; braucht eine Option einen Wert, fragt das Fenster danach. Unter Linux/Unix liest die App zusätzlich die `--help`-Ausgabe des Befehls und ergänzt die Liste.

Angefügt wird an die Befehlszeile der Konsole, in beiden Modi.

## Übertragungen (Transfer-Queue)

`F5` öffnet *Kopieren / Übertragen*: Zielordner (vorbelegt mit dem Pfad der anderen Seite, editierbar oder per *Durchsuchen…*), bei genau einem Eintrag zusätzlich der **Name** — Kopieren und Umbenennen in einem Schritt. *Bei fertiger Übertragung benachrichtigen* meldet das Ende in der Statusleiste; mit *Nicht mehr fragen* kopiert `F5` künftig direkt in die andere Pane.

**Konflikte**: Existiert das Ziel schon, fragt die App: *Ja*, *Ja, alle*, *Nein*, *Nein, alle* oder *Abbrechen*. Das gilt für `F5`, Drag & Drop, Einfügen und Verschieben. Quelle = Ziel wird übersprungen, ein Ordner in sich selbst abgelehnt.

`Strg+T` öffnet **Übertragungen**: Richtung, Fortschritt, Größe, Tempo und Restzeit je Auftrag. Aufträge starten sofort und laufen parallel.

- *Pause / Fortsetzen* und *Wiederaufnehmen* machen an der schon übertragenen Größe weiter; fertige Dateien werden übersprungen.
- *Abbrechen* und *Abgeschlossene entfernen*.
- **Limit** (KB/s, 0 = unbegrenzt) drosselt jeden Auftrag einzeln; es gilt ab dem nächsten Start oder Fortsetzen.
- Nach der Übertragung wird die Größe geprüft (✓). Beim **Verschieben** wird die Quelle erst nach erfolgreicher Prüfung gelöscht.
- Dateien ab 8 MB und Ordner laufen über eine **eigene SSH-Verbindung**, damit das Navigieren flüssig bleibt; klappt deren Aufbau nicht, über die bestehende.

Der Ordner-Browser funktioniert auch auf entfernten Servern.

## SFTP-Batch & geplante Aufgaben

*Aktionen → SFTP-Batch / geplante Aufgaben …* (`Strg+Shift+B`) führt ein Skript über die Verbindung des aktuellen Tabs aus. Relative Pfade beginnen beim Ordner der Remote- bzw. der lokalen Pane.

Ein Befehl je Zeile; Pfade mit Leerzeichen stehen in `"…"`, `#` leitet einen Kommentar ein:

- `cd PFAD`, `lcd PFAD`, `pwd`, `lpwd` — Ordner wechseln bzw. anzeigen (remote/lokal).
- `put LOKAL [REMOTE]`, `get REMOTE [LOKAL]` — hoch- bzw. herunterladen; ist das Ziel ein Ordner, landet die Datei darin. Vorhandene Dateien werden ohne Rückfrage überschrieben; Platzhalter gibt es nicht.
- `mkdir PFAD`, `rm DATEI`, `rmdir PFAD` (**löscht rekursiv**), `rename ALT NEU` bzw. `mv`, `chmod OKTAL PFAD`, `ln ZIEL LINK` und `echo TEXT`.
- `set NAME WERT` legt eine eigene Variable an.

**Variablen** in jeder Zeile: `$heute` (`2026-10-08`), `$jetzt` (`2026-10-08_14-30-05`), `$zeit`, `$jahr`, `$monat`, `$tag` sowie eigene als `$NAME`. `${NAME}` grenzt den Namen ab, wenn direkt Buchstaben, Ziffern oder `_` folgen (z. B. `${tag}_log`); `$$` ergibt ein `$`. Alle Zeitvariablen beziehen sich auf den Start des Laufs. Eine unbekannte Variable ist ein Fehler der Zeile — z. B. `get logs/app.log app-$heute.log`.

*Ausführen* startet das Skript. Das Protokoll zeigt jede Zeile mit ✓ oder ✗ und am Ende eine Bilanz; *Stopp* bricht zwischen zwei Zeilen ab. Mit *Bei Fehler abbrechen* endet das Skript beim ersten Fehler, sonst läuft es weiter.

**Wiederholen alle** N Minuten führt das Skript regelmäßig aus — zum ersten Mal nach einem Intervall und nur, **solange der Dialog offen ist**. Skript und Intervall werden beim Ausführen gemerkt; *Laden …* und *Speichern …* arbeiten mit Skriptdateien.

## Befehlspalette & Assistent

`Strg+P` (oder das Symbol im Konsolen-Kopf) öffnet den **Befehlskatalog** — mit Suchfeld, sortierbar nach Befehl, Kategorie, Plattform und Beschreibung.

- **OS-Filter**: Aktuelles OS · Beide · Nur Linux/Unix · Nur Windows · Plattformübergreifend. Das Server-OS wird beim Verbinden erkannt.
- **Gefährliche Befehle** sind rot markiert.
- *Einfügen* schreibt die Befehlsvorlage in die Konsole.
- **Assistent …** (oder Doppelklick) öffnet den Parameter-Editor: Textfelder, Auswahllisten und Checkboxen mit **Live-Vorschau**, unter Linux zusätzlich **sudo** (optional als anderer Benutzer). *In Konsole einfügen* übernimmt den fertigen Befehl, *Ausführen* startet ihn sofort.

## Datei-Suche (nach Name)

`Strg+Shift+F` sucht nach Dateinamen.

- **Root**: Startordner; *…* wählt ihn, *⌂* setzt die Laufwerkswurzel.
- **Dateiname**: Platzhalter (`*.log`) oder **Regex**; *Groß/klein ignorieren* ist voreingestellt. *Datei-Filter* grenzt auf Muster ein (kommagetrennt).
- **Erweitert**: *Dateien ausschließen*, *Ordner ausschließen* (z. B. `.git,node_modules`), *Art* (Alles, nur Dateien, nur Ordner) und *Grenzen* — Max-Tiefe, Min-Größe und Geändert ≤ Tage.
- Treffer laufen live ein; *Stopp* bricht ab.
- Doppelklick auf einen Treffer springt in dessen Ordner.

## Inhalts-Suche (grep)

`Strg+Alt+F` durchsucht Datei-**Inhalte**.

- **Suchbegriff** wörtlich oder als **Regex**; *Groß/klein ignorieren* ist voreingestellt, *Ganzes Wort* grenzt auf Wortgrenzen ein.
- **Binärdateien** werden übersprungen, außer mit *Binärdateien einbeziehen*.
- *Datei-Filter* und unter **Erweitert** die Ausschlüsse und Grenzen wie bei der Datei-Suche, dazu *Nur Dateinamen*, *Zeilen ohne Treffer* (invertiert) und *Zeilen vor/nach jedem Treffer* (Kontext).
- Ergebniszeilen haben die Form `pfad:zeile:text`; Doppelklick öffnet den Ordner der Datei.

## Massen-Umbenennen

`Strg+Shift+R` benennt die markierten Dateien in einem Rutsch um. Der *Geltungsbereich* wählt Name, Endung oder den ganzen Namen.

**Suchen & Ersetzen** — Modus *Text (wörtlich)*, *Platzhalter* (`*` und `?`) oder *Regex*; optional *Groß/Klein egal* und *Alle Vorkommen ersetzen*. *Regex-Vorlage* bietet fertige Muster, z. B. Klammern samt Inhalt, führende Nummern, Ziffern oder Kopie-Suffixe entfernen oder Leer- und Sonderzeichen ersetzen.

**Entfernen & Einfügen** — vorne oder hinten kürzen, einen Text entfernen und einen Text an einer Zeichenposition einfügen (negativ zählt vom Ende).

**Text & Endung** — Präfix, Suffix, Schreibweise (klein, GROSS, Wortanfänge, Satzanfang), Leerzeichen (behalten, `_`, `-`, entfernen) und Endung (unverändert, klein, GROSS, setzen).

**Nummerierung** (*aktiv*) — Start, Schritt, Stellen, Trenner und Position (vorne, hinten, an einer Position) sowie die **Reihenfolge** der Nummernvergabe (Eingabe, Name, Name absteigend, natürlich `1,2,10`, Endung).

Die **Vorschau** zeigt Alt → Neu; geänderte Namen sind grün, Konflikte rot. *Konflikte automatisch nummerieren* hängt ` (1)`, ` (2)` … an. Ausgeführt wird in einer **gefahrlosen Reihenfolge** — auch Tausch (`a↔b`) und Ketten (`a→b→c`) funktionieren über temporäre Zwischennamen. Schlägt ein Schritt fehl, werden die bisherigen zurückgenommen; *Rückgängig* macht eine ausgeführte Umbenennung rückgängig.

## Datei-Encoding konvertieren

*Tools → Datei-Encoding konvertieren* wandelt Textdateien zwischen Zeichensätzen um — UTF-8/16/32, Windows-1250/1251/1252, ISO 8859-1/15, DOS/OEM 437/850, Mac Roman, KOI8-R, Shift-JIS, GBK, Big5 sowie **EBCDIC** (cp037, cp500, cp273, cp1140, cp1141, cp1047, cp875).

- Das **Quell-Encoding wird automatisch erkannt** und vorgewählt.
- Die **Vorschau** zeigt die Quelle mit dem gewählten Codec.
- **Fehlerstrategie**: streng (melden), ersetzen oder ignorieren.
- Ausgabe in eine neue Datei oder **Original überschreiben**.
- **Mit KI reparieren …** lässt beschädigten Text von der KI rekonstruieren; die Vorschau zeigt das Ergebnis, gespeichert wird es erst mit *Konvertieren*.

## Datei- & Verzeichnis-Vergleich

- `Strg+Shift+D` **Datei-Vergleich**: zwei markierte Dateien als farbiger Unified-Diff (grün = hinzugefügt, rot = entfernt), mit Zeilenbilanz.
- `Strg+D` **Verzeichnis-Vergleich**: stellt beide Panes gegenüber (*nur links*, *nur rechts*, *links neuer*, *rechts neuer*, *identisch*), optional **rekursiv**. *Nur Unterschiede* (voreingestellt) blendet Gleiches aus, *Aktualisieren* vergleicht neu.
- *→ Rechts angleichen* bzw. *← Links angleichen* überträgt die markierten Einträge über die Transfer-Queue — ohne Rückfrage, vorhandene Dateien werden überschrieben.

## venv verwalten

*Tools → venv verwalten* legt virtuelle Python-Umgebungen an.

- **Projektordner**, **venv-Pfad** (Standard `.venv`), gefundene **Python-Versionen** zur Auswahl und die **Installation** (automatisch vorgeschlagen, z. B. aus `requirements.txt`; läuft nach dem Aktivieren).
- *Abhängigkeiten ignorieren* hängt `--skip-lock` (pipenv) bzw. `--no-deps` (pip) an.
- Die **Befehlsvorschau** zeigt, was ausgeführt wird; *Erstellen & aktivieren* schickt es an die aktive Konsole.
- Darunter die **bekannten Umgebungen** mit Typ (venv/pipenv), Version, Projekt und Pfad. *Aktivieren* oder Doppelklick aktiviert eine Umgebung in der Konsole, *Umgebung löschen* entfernt sie samt Ordner. Eine *Notiz zur ausgewählten Umgebung* speichert *Info speichern*.

## Makro-Manager

*Tools → Makro-Manager* bietet ein Raster frei belegbarer Tasten.

- **Layer** (Seiten) links: anlegen, bearbeiten (Name, zugeordnetes Programm, Zeilen × Spalten), löschen. *Exportieren …* schreibt alle Layer als JSON, *Importieren …* übernimmt ausgewählte (gleiche Namen werden umbenannt).
- *Layer automatisch zum Programm wechseln* beobachtet das Vordergrund-Programm und schaltet passend um; *Aktives Programm übernehmen* trägt es beim Bearbeiten ein.
- **Bearbeiten-Modus**: Klick auf eine Taste öffnet den Editor. **Ausführen-Modus**: Klick löst die Aktion aus, langes Halten öffnet trotzdem den Editor. Rechtsklick: Bearbeiten, Ausführen, Leeren. Im Ausführen-Modus lässt sich das Fenster an einen Bildschirmrand andocken.

Im **Tasten-Editor**: Beschriftung samt Position, Icon (als Hintergrund), Schriftfarbe und -art, ein **globales Kürzel** und die **Aktion**. Je nach Aktionstyp erscheint der passende Editor — Text, Zahl, Layer, Fenster, SSH-Befehl, JSON oder mehrere Schritte.

**Globale Kürzel** wirken systemweit, auch wenn SSHIT-Commander nicht im Vordergrund ist und der Makro-Manager nie geöffnet wurde. Sie lösen erst nach dem Loslassen der Modifikatortasten aus und ruhen, solange der Tasten-Editor offen ist.

**Aktionen**: Programme und Befehle starten, Dateien/URLs öffnen, Bildschirmfoto, Bildschirm sperren · Text tippen oder einfügen, Tastenkürzel und Tasten drücken/halten · Maus bewegen, klicken, scrollen · Medien und Lautstärke, Audiogerät wählen · Fenster fokussieren, verwalten, durchschalten · Layer wechseln · HTTP-Anfragen, Befehle an die SSH-Konsole oder an alle Konsolen · Verzögerung, mehrere Aktionen (alle bei jedem Druck), Sequenz (bei jedem Druck der nächste Schritt, danach von vorn) · Zwischenablage, Mehrzustands-Taste und Befehlsauswahl.

## Plugins

*Plugins → Plugins verwalten …* bindet eigenständige Programme ein.

- **Programm** (relativ zum `plugins/`-Ordner oder absolut), **Parameter** mit Platzhalter `{path}` für das gewählte Element (ohne Platzhalter wird der Pfad angehängt), **Arbeitsverzeichnis** (Standard: Ordner des Programms).
- **Im Kontextmenü anzeigen** blendet das Plugin in der Pane ein; *Gilt für* schränkt auf Dateien, Ordner oder beides ein.
- **Testen** startet das Plugin sofort.
- Alle Plugins stehen auch direkt im Menü *Plugins*, ebenso *Plugin-Ordner öffnen*.
- Zentral bereitgestellte Plugins (aus `plugins/plugins.json`) sind schreibgeschützt und mit *(zentral)* markiert.

## Netzwerkscanner

*Tools → Netzwerkscanner …* durchsucht das lokale Netz.

- **IP-Range**: CIDR (`192.168.1.0/24`), Bereiche (`10.0.0.1-50`), Listen oder einzelne Namen. Die lokale /24 ist vorbelegt.
- **Ports**: Vorauswahl (Häufige Ports, Nur SMB, Web, Fernzugriff, Alle wichtigen) oder eigene (`22,80,8000-8100`).
- **Optionen**: Ping (ICMP) zusätzlich, nur antwortende Hosts anzeigen, Hostnamen auflösen, Freigaben erkennen (SMB), Geräte identifizieren (Banner, Web-Titel, OS, NetBIOS), MAC-Adresse + Hersteller, Parallelität, Timeout je Port und Auto-Rescan. *Letzten Scan laden* zeigt das letzte Ergebnis ohne neuen Scan.
- Die Tabelle zeigt IP, Name, MAC, **Hersteller** (aus der OUI-Tabelle), OS-Schätzung, offene Ports mit Dienstnamen sowie Weboberfläche und Freigaben. Doppelklick öffnet eine gefundene Weboberfläche.
- Mit *Schließen* werden die Hosts als **Dateisystem** (`net://`) in die unter *Ergebnisse in* gewählte Pane übernommen: Host → Freigabe → Dateien.

## Alarm Trigger (Datei-Alarm)

*Tools → Alarm Trigger …* (oder im Kontextmenü der Pane *Alarm Trigger für Verzeichnis setzen …*) überwacht Ordner auf Änderungen.

Je Alarm:

- *Anzeigename* (optional) und *Zu überwachender Ordner*.
- *Erkannte Änderungen*: Neu erstellt, Geändert, Gelöscht; dazu *Unterordner einbeziehen*, *Ordner mitüberwachen* und *aktiv*.
- *Nur diese Muster* und *Diese Muster ignorieren*: Platzhalter, mit `;` getrennt (z. B. `*.log;*.tmp`).
- **Remote**: Ein Alarm, der in einem verbundenen Tab angelegt wird, überwacht den Ordner auf diesem Server (Pfad eintippen). Ist der Server nicht verbunden, pausiert der Alarm.
- *Befehl bei Auslösung*: ein lokaler Befehl, einmal je Prüfdurchlauf. Platzhalter `{path}`, `{kind}` (created/modified/deleted), `{name}` (Name des Alarms) und `{count}` — auch als Umgebungsvariablen `ALARM_PATH`, `ALARM_KIND`, `ALARM_NAME` und `ALARM_COUNT`.

Für alle Alarme gilt *Bei Auslösung*: **Desktop-Benachrichtigung** (voreingestellt) und **Signalton**. Dazu erscheint eine Meldung in der Statusleiste; ein Klick darauf zeigt die Ereignisse. Geprüft wird alle paar Sekunden per Schnappschuss-Vergleich.

## GitHub Repo Alarm

*Tools → GitHub Repo Alarm …* meldet neue Pushes.

- **Repository** als `owner/repo` oder als GitHub-URL (auch `git@github.com:owner/repo.git`). Das Häkchen in der Spalte *Aktiv* schaltet die Überwachung eines Repositorys an oder aus.
- Ein optionales **Token** (*Token speichern*) erhöht das API-Limit, erlaubt private Repositories und liegt im Schlüsselbund, nicht im Klartext.
- Geprüft wird der Zeitstempel des letzten Pushs, im Intervall aus *Einstellungen → Allgemein* (Standard 15 Minuten); ändert er sich, meldet die Statusleiste neue Daten. *Jetzt prüfen* fragt sofort ab.
- **Lokaler Ordner** ordnet den lokalen Klon zu (*Ordner wählen …*, *Zuordnung entfernen*); eine lokale Pane erkennt den Klon auch selbst. Hat er Änderungen, sind in den Panes alle Ordner darüber bis zum Laufwerk farbig markiert — grün, wenn nur neue Dateien dazugekommen sind, sonst orange.

## Zwischenablage-Verwaltung

*Clipboard → Clipboard-Manager* führt eine Historie der kopierten Texte und Dateien (bis zu 100 Einträge, nur im Speicher).

- Doppelklick setzt einen Eintrag als **aktiven** Inhalt der Zwischenablage, fügt ihn in die aktive Konsole ein und schließt das Fenster.
- *Einfügen* schreibt den Eintrag in die aktive Konsole.
- *Eintrag löschen* und *Alle löschen* räumen die Liste auf.

## Eigenschaften & Rechte (chmod)

Kontextmenü → **Eigenschaften** zeigt Name, Pfad, Typ, Größe, Zeitstempel, Eigner/Gruppe und ein evtl. Symlink-Ziel. Bei lokalen Ordnern wird die Größe rekursiv berechnet.

Der **chmod-Editor** darunter hat rwx-Checkboxen für Eigner, Gruppe und Andere sowie ein **Oktal-Feld** — beide Darstellungen halten sich gegenseitig aktuell. *Übernehmen* schreibt die Rechte.

## SSH-Schlüssel

🔑 in der Server-Verwaltung oder *Tools → SSH-Schlüssel erzeugen / konvertieren …* öffnet den **Schlüssel-Dialog**.

- **Erzeugen**: Ed25519 (empfohlen), RSA 4096/3072 oder ECDSA nistp256, mit optionalem Kommentar. Erzeugt wird mit dem `ssh-keygen` von Windows, ohne Passphrase.
- Der **öffentliche Schlüssel** wird angezeigt und lässt sich kopieren — er gehört in `~/.ssh/authorized_keys` auf dem Server.
- **Schlüssel speichern …** legt Privat- und `.pub`-Datei ab; der Privatschlüssel erhält restriktive Rechte.
- **Konvertieren**: OpenSSH → PPK und PPK → OpenSSH.
- Nach dem Speichern bzw. nach PPK → OpenSSH wird der Pfad ins Profil übernommen.

## SSH-Tunnel / Port-Weiterleitung

`Strg+Shift+T` bzw. *Aktionen → SSH-Tunnel* öffnet die Port-Weiterleitungen (der Tab muss verbunden sein):

- **Lokal (-L)**: ein lokaler Port wird auf ein Ziel hinter dem Server geleitet — z. B. `127.0.0.1:8080` → `localhost:80` auf dem Server.
- **Remote (-R)**: ein Port auf dem Server zeigt auf ein lokales Ziel.
- **Dynamisch / SOCKS (-D)**: SOCKS5-Proxy über die SSH-Verbindung.

*Im Server-Profil speichern (Auto-Start beim Verbinden)* merkt den Tunnel im Profil; er öffnet sich dann bei jeder Verbindung mit diesem Server. Gespeicherte Tunnel stehen in der Server-Verwaltung unter *Tunnel (Auto-Start)* und lassen sich dort entfernen. Offene Tunnel stehen unter *Aktive Weiterleitungen* und lassen sich einzeln **stoppen**; die Statusleiste zeigt ihre Zahl. Beim Trennen oder Schließen des Tabs werden sie beendet.

## Server-Info

Der Chip **ⓘ Info** im Kopf einer verbundenen Pane öffnet die Server-Info — die wichtigsten Eckdaten eines Linux/Unix-Servers, ermittelt in einem einzigen SSH-Aufruf.

- **Kopf**: Hostname, System, Kernel, Laufzeit und Last sowie der angemeldete Benutzer.
- **Konfigurationsdateien**: wichtige Dateien nach Kategorie, mit Zugriff (*lesen & schreiben*, *nur lesen*, *kein Zugriff (sudo nötig)*), dazu gefundene `.env`- und docker-compose-Dateien unter `/home`, `/root`, `/var/www`, `/srv` und `/opt`. *Öffnen / Bearbeiten* oder Doppelklick öffnet die Datei im Editor — bei aktivem sudo-Chip mit dessen Rechten; *Pfad kopieren* kopiert den Pfad.
- **Benutzer** (Systemkonten auf Wunsch), **Ports**, **Dienste** und **Speicher**.

*Aktualisieren* fragt erneut ab. Für Windows-Server gibt es keine Server-Info.

## KI-Funktionen

Der KI-Assistent erklärt Ausgaben und Dateien. Er ist **rein beratend** und führt nichts aus.

**Einrichten** unter *Einstellungen → KI*: *KI-Assistent aktivieren*, dann den **Anbieter** wählen:

- **Ollama (lokal)** — das Modell läuft auf einem eigenen Ollama-Server (Standard: dieser Rechner); die Inhalte bleiben dort. *Modell laden* holt neue Modelle direkt in Ollama.
- **Anthropic Claude**, **OpenAI**, **Google Gemini** — Cloud-Dienste mit eigenem **API-Schlüssel**.
- **OpenAI-kompatibel** — z. B. LM Studio oder ein eigener Server; *Adresse* angeben, API-Schlüssel optional.

*Modelle laden* fragt die verfügbaren Modelle ab, *Verbindung testen* prüft die Einstellungen. API-Schlüssel liegen im Windows Credential Manager.

**Datenschutz**: Bevor zum ersten Mal Inhalte an einen Anbieter außer Ollama gehen, fragt die App nach — *Senden — nicht mehr fragen* oder *Abbrechen*. Die Zustimmung gilt je Anbieter; *Rückfrage vor dem Senden wieder einschalten* auf derselben Seite nimmt sie zurück.

**Aufrufen**:

- **KI** im Konsolen-Kopf erklärt die sichtbare Ausgabe bzw. einen Fehler.
- *Tools → KI*: Terminalausgabe erklären, Datei erklären / Frage zur Datei und KI-Fehleranalyse (Quellcode) für die markierte Datei.
- Im Editor: **KI erklären** (eine Markierung hat Vorrang vor der ganzen Datei) und **KI Fehleranalyse** (nur Fehler, mit Schweregrad und Korrekturvorschlag).
- Im Encoding-Konverter: **Mit KI reparieren …**.

Antworten erscheinen in einem Chat-Fenster mit **Folgefragen** (`Strg+Enter` sendet, *Stop* bricht ab).

## Ansicht & Designs (Themes)

*Ansicht → Theme* schaltet zwischen **Dunkel**, **Mitternacht**, **Hell**, **Hoher Kontrast** und eigenen Themes um; die Wahl wird gespeichert und gilt auch für das Terminal.

*Ansicht → Theme-Editor* erstellt eigene Farbschemata: eine Basis wählen, die Farbfelder anklicken (Hintergrund, Flächen, Rahmen, Text, Akzente, Scrollbalken, Terminal-Farben) und unter eigenem Namen speichern. Die **Vorschau** unten zeigt das Ergebnis sofort. Mitgelieferte Themes lassen sich nicht überschreiben oder löschen.

Außerdem im Menü *Ansicht*: **Versteckte Dateien** (`Strg+.`), **Kachelansicht** und das **Vorschau-Panel** (`Strg+F2`), das die markierte Datei schreibgeschützt zwischen Pane und Konsole anzeigt (Text oder Bild). Schriftarten und -größen: *Einstellungen → Allgemein*.

## Lesezeichen

Pfad-Lesezeichen werden **je Verbindung** getrennt geführt (Profilname bzw. `local`).

- Der Stern in der Pfadzeile merkt den aktuellen Pfad bzw. entfernt ihn wieder.
- Die Lesezeichen-Schaltfläche daneben öffnet ein Menü: ein Klick springt hin, dazu *★ Aktuellen Pfad merken* und *Verwalten…*.
- `Strg+B` bzw. *Panes → Lesezeichen …* öffnet die Verwaltung: *Anspringen*, *Entfernen*, *Exportieren …* und *Importieren …*.
- *Aktionen → Lesezeichen exportieren …* bzw. *importieren …* teilt Lesezeichen zwischen Rechnern.

## Einstellungen

`Strg+,` öffnet die Einstellungen mit vier Reitern. Die meisten Änderungen gelten nach *Speichern* sofort; ein Sprachwechsel braucht einen Neustart, den die App gleich anbietet.

**Allgemein** — Sprache, Theme (eigene Themes lassen sich löschen), Schriftgrößen für Editor, Terminal und Panes, Terminal-Schriftart, **Datumsformat** (Token wie `DD.MM.YYYY HH24:MI`), versteckte Dateien ausblenden, Programm-Logos und Bild-Vorschau als Icon, **natürliche Sortierung** (`1, 2, 10`), schlanke Ansicht, ausführbare Dateien farblich hervorheben (Farbe wählbar), Bestätigung vor dem Kopieren (`F5`) und Löschen (`F8`), **lokale Shell** (PowerShell oder cmd), Tabs beim Start wiederherstellen, beim Start zum letzten Server verbinden, Prüfintervall des GitHub-Alarms und Standard-Startpfad.

**KI** — Anbieter, Adresse, API-Schlüssel und Modell (siehe *KI-Funktionen*).

**Tastenkürzel** — alle Aktionen frei belegbar; beim Speichern werden **Dubletten** gemeldet, auch mit fest vergebenen Tasten. *Auf Standard zurücksetzen* stellt die Vorgaben wieder her.

**Sicherheit** — App-Passwort, Zwei-Faktor-Anmeldung und Wiederherstellungscodes (siehe *App-Sperre & Zwei-Faktor*); diese Änderungen gelten sofort.

**Konfiguration exportieren** bzw. **importieren** (unter den Reitern) sichert Einstellungen, Serverprofile, Lesezeichen, Tab-Favoriten und Verlauf in einer JSON-Datei. Nicht enthalten sind Passwörter, Tokens und API-Schlüssel, die App-Sperre, Makros, Plugins und Host-Keys. **Achtung:** Die Serverprofile stehen in der Datei unverschlüsselt, auch bei aktiver App-Sperre — die Datei sicher aufbewahren. Beim Import wählt man die Bereiche aus; danach sollte die App neu gestartet werden.

## Panes & Tabs

- **Neuer Tab**: `Strg+Shift+N` oder *Aktionen → Neuer Tab*. Jeder Tab hat eigene Panes, Konsolen und eine eigene SSH-Verbindung. `Strg+Shift+E` benennt den Tab um, `Strg+W` schließt ihn.
- Tabs lassen sich verschieben; der Titel zeigt die Verbindung, die *Tab-Farbe* des Profils färbt ihn.
- Die **aktive Pane** ist blau umrandet, `Tab` wechselt die Seite. Verbinden und andere Aktionen wirken auf sie.
- Menü *Panes*: **Nur Dateisystem anzeigen**, **Nur Terminal anzeigen**, **Panes untereinander anzeigen** (statt nebeneinander), **Panes tauschen** (`Strg+U`, Verbindung und Konsole wandern mit), **Panes synchronisieren** (`Strg+E`, die andere Pane springt in den Ordner der aktiven — nur bei gleichem Dateisystem), **Status anzeigen** (`Strg+F9`, folgt dem Cursor der anderen Pane) und **Befehl an beide Konsolen …** (`Strg+Shift+K`).
- **Tab-Favoriten** (*Aktionen → Tab-Favoriten*) sichern die aktuelle Tab-Konstellation unter einem Namen und stellen sie später wieder her.
- Mit *Tabs beim Start wiederherstellen* werden die offenen Tabs beim Beenden gesichert und beim Start wiederhergestellt.
- Der **sudo-Chip**: siehe *sudo & andere Benutzer*.

## sudo & andere Benutzer

Bei Linux/Unix-Servern zeigt der Pane-Kopf den Chip **sudo**.

- **Klick**: Die Pane arbeitet als **root**. Für root selbst und bei NOPASSWD ist kein Passwort nötig; sonst fragt die App nach dem eigenen sudo-Passwort — oder, wenn der Benutzer nicht in der Gruppe sudo, wheel oder admin ist, nach dem root-Passwort (`su`).
- **Rechtsklick**: *Pane ausführen als …* listet die Benutzer des Servers (Systemkonten im Untermenü). Ein anderer Benutzer läuft über `sudo -u` (eigenes Passwort) oder `su` (Passwort des Zielbenutzers).
- Als anderer Benutzer funktionieren Auflisten, Ansehen, Bearbeiten, Anlegen, Umbenennen, Löschen, Rechte und Übertragungen.
- Der Chip zeigt den Modus (*sudo*, *sudo: name*, *su: name*), die Pane ist orange umrandet. Ein erneuter Klick oder *(angemeldet)* im Menü kehrt zum eigenen Benutzer zurück.
- Die **Konsole** wechselt den Benutzer nicht — dort `sudo` oder `su` selbst eingeben.
- Passwörter bleiben **nur im Speicher** und stehen nie auf der Befehlszeile.

## Sicherheit

- Passwörter, Passphrasen, Tokens und API-Schlüssel liegen im **Windows Credential Manager**, nie im Klartext auf der Platte. Server-Passwörter nur, wenn im Profil *Passwort/Passphrase sicher im OS-Keyring speichern* angehakt ist.
- **Host-Key-Prüfung** je Profil: *Beim ersten Mal vertrauen (accept-new)*, *Strikt (nur bekannte)* oder *Ignorieren (unsicher)*.
- Ein **geänderter** Host-Key bricht die Verbindung **vor der Anmeldung** ab — es gehen keine Zugangsdaten an den Server. Bei *Strikt* gilt das auch für unbekannte Keys.
- Bei *accept-new* zeigt die App den Fingerprint eines **neuen** Servers **vor der Anmeldung**: *Vertrauen und speichern* merkt ihn, *Nur diesmal verbinden* gilt für diesen Tab (auch beim automatischen Neuverbinden), *Abbrechen* bricht ab. Erst nach der Bestätigung meldet sich die App an; das gilt auch für einen Sprung-Host (ProxyJump).
- Keys, denen das System-`ssh` bereits vertraut (`~/.ssh/known_hosts`), werden übernommen; bestätigte Keys trägt die App dort ebenfalls ein.
- Die Statusleiste zeigt, ob der Host-Key bekannt, neu oder ungeprüft ist. *Tools → Bekannte Host-Keys …* zeigt und bereinigt den Speicher der App — nötig, wenn ein Server neu aufgesetzt wurde.
- Das sudo-Passwort bleibt nur im Speicher und steht nie auf der Befehlszeile.
- Zum Ansehen oder Bearbeiten heruntergeladene Remote-Dateien werden beim Beenden gelöscht.
- *Tools → Sicherheits-Audit (CVE) …* prüft OS, Pakete, `sshd_config`, Firewall, offene Ports und Konten und gleicht Kernkomponenten mit **OSV.dev** ab.
- Die **App-Sperre** verschlüsselt zusätzlich die Serverprofile (siehe *App-Sperre & Zwei-Faktor*).
- Der KI-Assistent führt nichts aus; Cloud-Anbieter erhalten Inhalte erst nach Zustimmung.

## App-Sperre & Zwei-Faktor

*Einstellungen → Sicherheit* schützt die App mit einem Passwort, das beim Start abgefragt wird.

- **Passwort beim Start der App abfragen** legt das App-Passwort fest (mindestens 8 Zeichen). *Passwort ändern …* ändert es; Ausschalten verlangt das aktuelle Passwort.
- Solange die Sperre aktiv ist, sind die **Serverprofile verschlüsselt** (AES-256-GCM). Einstellungen, Verlauf, Lesezeichen und Tab-Favoriten bleiben unverschlüsselt; Server-Passwörter liegen ohnehin im Credential Manager.
- **Zwei-Faktor-Authentifizierung (Authenticator-App)**: den QR-Code mit einer Authenticator-App scannen (oder den Schlüssel abtippen), einen Bestätigungscode eingeben und *Aktivieren*.
- Danach erscheinen **8 Wiederherstellungscodes** — nur dieses eine Mal. Kopieren oder als Datei speichern und sicher aufbewahren. Jeder Code gilt einmal anstelle des Bestätigungscodes; *Neue Wiederherstellungscodes …* ersetzt alle.
- **Entsperren**: das Passwort und, falls aktiv, der 6-stellige Code oder ein Wiederherstellungscode. Nach wiederholten Fehlversuchen wächst die Wartezeit (bis 60 s).
- Das Geheimnis der Authenticator-App ist an das Windows-Konto gebunden; unter einem anderen Konto helfen nur die Wiederherstellungscodes.
- Gesperrt wird beim Start der App.

**Passwort vergessen?** Es lässt sich nicht zurücksetzen. Wird die Sperre durch Löschen von `applock.json` umgangen, legt die App die verschlüsselten Profile als `servers.json.locked-…` beiseite und startet mit leerer Profilliste.

## Absturzberichte

Stürzt die App ab, schreibt sie einen Bericht nach `%APPDATA%\ncssh\crashes`: eine Minidump-Datei (`.dmp`) und eine kurze Textdatei mit Fehlercode und Aufrufkette. Die Textdatei enthält nur Adressen, keine Datei- oder Sitzungsinhalte.

Beim nächsten Start weist die App einmal darauf hin; *Ordner öffnen* zeigt die Dateien. Abgefangene interne Fehler landen in `errors.log` im selben Ordner.

Für eine Fehlermeldung (z. B. als GitHub-Issue) beide Dateien des Absturzes anhängen.

## Tastenkürzel

Die vollständige, aktuell konfigurierte Liste steht im Reiter **Tastenkürzel** dieses Fensters (*Hilfe → Tastenkürzel*); ändern lässt sie sich unter *Einstellungen → Tastenkürzel*.

Fest vergeben (nicht konfigurierbar): `Strg+Q` (Beenden), `Strg+Shift+K` (Befehl an beide Konsolen), `Strg+F2` (Vorschau-Panel), `Backspace` (hoch), `Enter` (öffnen bzw. mit dem Standardprogramm starten), `Strg+F` (Schnellfilter der Pane bzw. Suche in der Konsolenausgabe), `Esc` (Filter leeren und schließen), `Strg+Shift+C`/`Strg+Shift+V` (Terminal kopieren/einfügen), `Shift+Bild↑/↓` (Scrollback), `Strg+Shift+F` (im Terminal-Puffer suchen) sowie die Editor-Tasten `Strg+S`, `Strg+Shift+S`, `Strg+F` und `Strg+G`.

## Tastenkürzel-Übersicht

Standardbelegung. Unter *Tools → Einstellungen → Tastenkürzel* lässt sich jede Aktion dieser Tabellen neu belegen; „—“ heißt: ohne Standardkürzel.

### Aktionen

| Aktion | Kürzel |
|---|---|
| SSH verbinden | F9 |
| Befehlspalette | Strg+P |
| Verlauf & Favoriten | Strg+H |
| Übertragungen | Strg+T |
| SSH-Tunnel | Strg+Shift+T |
| SFTP-Batch / geplante Aufgaben | Strg+Shift+B |
| Neu laden | Strg+R |
| Verbindung trennen | — |

### Dateien

| Aktion | Kürzel |
|---|---|
| Ansehen | F3 |
| Bearbeiten | F4 |
| Kopieren | F5 |
| Umbenennen | F6 |
| Neuer Ordner | F7 |
| Löschen | F8 |
| Massen-Umbenennen | Strg+Shift+R |

### Werkzeuge

| Aktion | Kürzel |
|---|---|
| Datei-Suche (Name) | Strg+Shift+F |
| Inhalts-Suche (grep) | Strg+Alt+F |
| Verzeichnis-Vergleich | Strg+D |
| Datei-Vergleich | Strg+Shift+D |
| Datei-Encoding konvertieren | — |
| venv verwalten | — |
| Versteckte Dateien | Strg+. |
| Panes synchronisieren | Strg+E |
| Panes tauschen | Strg+U |
| Lesezeichen der aktiven Pane | Strg+B |
| Status anzeigen (folgt dem Cursor der anderen Pane) | Strg+F9 |

### Tabs & App

| Aktion | Kürzel |
|---|---|
| Tab-Favoriten | — |
| Neuer Tab | Strg+Shift+N |
| Tab umbenennen | Strg+Shift+E |
| Tab schließen | Strg+W |
| Einstellungen | Strg+, |
| Hilfe (Handbuch) | F1 |

### Feste Programmkürzel

Diese Tasten sind fest vergeben und nicht konfigurierbar.

| Aktion | Kürzel |
|---|---|
| Beenden | Strg+Q |
| Befehl an beide Konsolen … | Strg+Shift+K |
| Vorschau-Panel | Strg+F2 |

### Maus & feste Tasten

| Bereich | Aktion | Kürzel |
|---|---|---|
| Navigation | Ordner öffnen / Datei ansehen | Doppelklick |
| Navigation | Ordner öffnen / Datei mit dem Standardprogramm öffnen | Enter |
| Navigation | Zurück / Vor | Alt+←  /  Alt+→ |
| Navigation | Übergeordneter Ordner | Backspace |
| Navigation | Pane wechseln | Tab |
| Navigation | Pane filtern | Strg+F |
| Navigation | Panes synchronisieren / tauschen | Menü Panes |
| Dateien | Markieren | Space / Einfg |
| Dateien | Nach Muster markieren / aufheben | Num +  /  Num − |
| Dateien | Auswahl umkehren | Num * |
| Dateien | Alles markieren (Strg+A) | Strg+A |
| Dateien | Kopieren / Einfügen (in diese Pane) | Strg+C  /  Strg+V |
| Dateien | Kontextmenü (Rechte, Eigenschaften, …) | Rechtsklick |
| Dateien | Ausführen — mit OS-Standardprogramm öffnen | Enter / Kontextmenü |
| Dateien | Übertragen | Ziehen + loslassen |
| Konsole / Terminal | Historie | ↑  /  ↓ |
| Konsole / Terminal | Scrollback | Shift+Bild↑ / ↓ |
| Konsole / Terminal | Auswahl kopieren | Strg+Shift+C / Strg+Einfg / Strg+C bei Auswahl |
| Konsole / Terminal | Einfügen | Strg+Shift+V / Strg+V / Shift+Einfg |
| Konsole / Terminal | Wort markieren | Doppelklick |
| Konsole / Terminal | Im Puffer suchen | Strg+Shift+F |
| Konsole / Terminal | In der Ausgabe suchen (Befehle-Modus) | Strg+F |
| Konsole / Terminal | Laufenden Befehl abbrechen (Befehle-Modus) | Strg+C / Esc |
