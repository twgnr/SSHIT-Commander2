// Umschaltbare Themes (Fusion + QSS + Palette), inkl. benutzerdefinierter Themes.
#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <utility>
#include <vector>

class QApplication;

namespace ncssh::gui {

// Farbpalette eines Themes: Schluessel (z.B. "bg") -> Farbwert (Hex/Farbname).
using ThemeColors = QHash<QString, QString>;

// Editierbarer Farbschluessel mit Anzeigename.
struct ColorKey {
    QString key;
    QString label;
};

// DEFAULT_THEME ("Dunkel").
const QString &defaultTheme();

// COLOR_KEYS: editierbare Farbschluessel mit Anzeigenamen
// (Reihenfolge = Editor-Reihenfolge).
const std::vector<ColorKey> &colorKeys();

// THEMES: die eingebauten Themes (Dunkel / Mitternacht / Hell / Hoher Kontrast).
const QHash<QString, ThemeColors> &builtinThemes();

// Aktuelle Terminal-Farben (vom Theme gesetzt) — vom TerminalWidget gelesen.
// WICHTIG: liefert (bg, fg) — in dieser Reihenfolge!
std::pair<QString, QString> terminalColors();

// Laedt benutzerdefinierte Themes aus den Einstellungen.
QHash<QString, ThemeColors> customThemes();

// Eingebaute + benutzerdefinierte Themes (eingebaute haben Vorrang beim Namen).
QHash<QString, ThemeColors> allThemes();

bool isBuiltin(const QString &name);

// Vollstaendige Farben eines Themes (zum Bearbeiten / als Basis).
ThemeColors themeColors(const QString &name);

void saveCustomTheme(const QString &name, const ThemeColors &colors);
void deleteCustomTheme(const QString &name);

// Alle Theme-Namen (eingebaute zuerst, dann benutzerdefinierte).
QStringList themeNames();

// QSS fuer eine (ggf. unvollstaendige) Farbpalette — fuer Live-Vorschau.
QString stylesheetFor(const ThemeColors &colors);

// Wendet das Theme (Fusion-Style, Palette, QSS) auf die Anwendung an.
void applyTheme(QApplication *app, const QString &name = defaultTheme());

} // namespace ncssh::gui
