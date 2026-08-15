// Massen-Umbenennen: aus Originalnamen + Regeln neue Namen berechnen (rein).
//
// Bewusst ohne I/O — voll testbar. Die GUI (Bulk-Rename-Dialog) sammelt die
// Dateien (inkl. optionaler Unterordner + Filter) und ruft computeRenames()
// fuer die Live-Vorschau und das eigentliche Umbenennen auf.
//
// Die Pipeline ist so gebaut, dass ein normaler Nutzer KEIN Regex braucht:
// Suchen/Ersetzen kennt die Modi "text" (woertlich) und "wildcard" (* / ?);
// "regex" ist nur die Profi-Option. Dazu kommen dedizierte, selbsterklaerende
// Schritte (Zuschneiden, Einfuegen, Gross/Klein, Leerzeichen, Nummerierung,
// Endung), die zusammen die allermeisten Wuensche ohne Regex abdecken.
#pragma once

#include <QSet>
#include <QString>
#include <QStringList>
#include <optional>
#include <utility>
#include <vector>

namespace ncssh::core {

// Reihenfolge der Schritte (zur Anzeige/Doku in der UI).
const QStringList &pipelineDoc();

// Ein (alt, neu)-Namenspaar.
using RenamePair = std::pair<QString, QString>;

// Vergleich fuer "natuerliche" Reihenfolge (file2 vor file10); Ergebnis wie
// strcmp (<0 / 0 / >0). Der Name wird in Text-/Zahl-Abschnitte zerlegt;
// Zahlen werden numerisch, Text klein-normalisiert verglichen. Da die
// Zerlegung immer mit einem (ggf. leeren) Textstueck beginnt, stehen an
// gleichen Listenpositionen stets gleiche Typen — der Vergleich ist sicher.
int naturalCompare(const QString &a, const QString &b);

// Liefert eine Permutation der Indizes fuer die gewuenschte Reihenfolge.
// mode: "none" (Eingabereihenfolge) - "name" / "name_desc" (alphabetisch) -
// "natural" (1,2,10) - "ext" (nach Endung, dann Name).
// Stabil — die GUI ordnet damit Dateiliste UND Vorschau identisch.
std::vector<int> sortIndices(const std::vector<QString> &names,
                             const QString &mode = QStringLiteral("none"));

// "a.txt" -> ("a", ".txt"); Endung inkl. Punkt (oder leer).
// Entspricht os.path.splitext fuer blosse Dateinamen.
std::pair<QString, QString> splitName(const QString &name);

// Macht Ziel-Namen innerhalb EINES Ordners eindeutig.
//
// Erwartet die vollstaendige Paarliste eines Ordners (auch unveraenderte,
// mit alt == neu). Unveraenderte Namen sind fix und werden zuerst belegt;
// kollidierende geaenderte Namen erhalten " (1)", " (2)" ... vor der Endung.
// Reine Funktion — die GUI ruft sie pro Zielordner auf.
std::vector<RenamePair> autoResolveCollisions(const std::vector<RenamePair> &pairs);

// Bringt Umbenennungen EINES Ordners in eine gefahrlose Reihenfolge.
//
// Erwartet nur die tatsaechlichen Aenderungen (alt != neu). Ein Schritt ist
// sicher, wenn sein Ziel nicht noch als Quelle einer ausstehenden Umbenennung
// dient. Zyklen (Tausch a<->b, Ketten a->b->c) werden ueber einen temporaeren
// Zwischennamen aufgebrochen. Gibt die auszufuehrenden Schritte als geordnete
// [(quelle, ziel)] zurueck (inkl. evtl. Temp-Schritten).
//
// Reine Funktion: rechnet nur auf Namen, fuehrt selbst kein I/O aus.
// existing: weitere im Ordner vorhandene Namen (fuer die Temp-Namenswahl).
std::vector<RenamePair> planSafeOrder(const std::vector<RenamePair> &pairs,
                                      const QSet<QString> &existing = {});

// Uebersetzt ein einfaches Platzhalter-Muster (* = beliebig, ? = ein Zeichen)
// in einen Regex. Alles andere wird woertlich genommen.
QString wildcardToRegex(const QString &pattern);

// Gibt eine Fehlermeldung zurueck, falls pattern kein gueltiger Regex ist,
// sonst nullopt.
std::optional<QString> validateRegex(const QString &pattern);

// Filter-Test fuer einen Dateinamen.
//
// glob       — Platzhalter-Muster ueber den ganzen Namen (z.B. "IMG_*").
// extensions — Liste erlaubter Endungen (mit/ohne Punkt, z.B. {"jpg","png"}).
// Leere Kriterien lassen alles durch.
bool nameMatches(const QString &name, const QString &glob = {},
                 const std::vector<QString> &extensions = {},
                 bool ignoreCase = true);

// Regeln fuer computeRenames().
struct RenameOptions {
    // Suchen & Ersetzen
    QString search;
    QString replace;
    QString matchMode = QStringLiteral("text");     // "text" | "wildcard" | "regex"
    bool ignoreCase = false;
    bool replaceAll = true;
    QString removeText;
    // Zuschneiden / Einfuegen
    int trimStart = 0;
    int trimEnd = 0;
    QString insertText;
    int insertPos = 0;                              // negativ = vom Ende
    // Gross/Klein & Leerzeichen
    QString caseMode = QStringLiteral("none");      // "none"|"lower"|"upper"|"title"|"sentence"
    QString spaceMode = QStringLiteral("none");     // "none"|"underscore"|"dash"|"remove"
    // Praefix / Suffix
    QString prefix;
    QString suffix;
    // Nummerierung
    bool numbering = false;
    int start = 1;
    int step = 1;
    int width = 2;
    QString numSep;
    QString numPosition = QStringLiteral("suffix"); // "suffix"|"prefix"|"at"|"at_replace"
    int numPos = 0;
    // Dateiendung
    QString extMode = QStringLiteral("none");       // "none"|"lower"|"upper"|"set"
    QString extValue;
    // Geltungsbereich der Kern-Schritte: "name" | "ext" | "full"
    QString scope = QStringLiteral("name");
    // Zaehler bei Wechsel des Gruppenschluessels neu starten (z.B. pro
    // Zielordner); leer = aus. Gleiche Laenge wie die Namensliste.
    std::vector<QString> groupKeys;
};

// Berechnet [(alt, neu)] fuer eine Liste von Dateinamen.
//
// Der Geltungsbereich scope steuert, worauf Suchen/Ersetzen, Zuschneiden,
// Einfuegen, Gross/Klein und Leerzeichen wirken (Name ohne Endung / nur
// Endung / ganzer Name). Praefix, Suffix und Nummerierung wirken immer auf
// den Namensteil; extMode aendert die Endung getrennt.
std::vector<RenamePair> computeRenames(const std::vector<QString> &names,
                                       const RenameOptions &options = {});

// Neue Namen, die mehrfach vorkommen (wuerden sich ueberschreiben).
QSet<QString> findCollisions(const std::vector<RenamePair> &pairs);

} // namespace ncssh::core
