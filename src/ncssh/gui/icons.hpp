// Schlanke, theme-faehige Vektor-Icons (QPainter) fuer die Toolbar.
//
// Qt-Standard-Icons (SP_*) sind plattformabhaengig und fuer Aktionen wie
// "Uebertragungen / Kopieren / Bearbeiten" wenig aussagekraeftig. Die hier
// gezeichneten Glyphen sind eindeutig. Die Farbe wird uebergeben (i.d.R. die
// Textfarbe des aktuellen Themes), sodass die Icons in hellen wie dunklen
// Themes lesbar bleiben.
#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

namespace ncssh::gui {

// Gibt ein gezeichnetes Icon fuer kind zurueck (leeres Icon, wenn unbekannt).
// Enthaelt zusaetzlich ein gedaempftes Pixmap fuer den Disabled-Zustand.
QIcon icon(const QString &kind, const QColor &color, int size = 20);

// Icon in der Textfarbe des aktuellen Themes.
QIcon themedIcon(const QString &kind, int size = 20);

bool hasIcon(const QString &kind);

} // namespace ncssh::gui
