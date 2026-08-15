// Leichtgewichtige Mehrsprachigkeit — der deutsche Quelltext ist der Schluessel.
// Die Funktion heisst _t statt tr, um QObject::tr nicht zu verdecken.
// Kataloge liegen als flaches JSON unter :/i18n/<code>.json.
#pragma once

#include <QString>
#include <QStringList>

namespace ncssh::core {

QStringList availableLanguages();
QString languageName(const QString &code);
void setLanguage(const QString &code);
QString currentLanguage();

// Uebersetzt text (deutscher Quelltext) in die aktuelle Sprache.
// Nur String-Literale uebergeben; variablen Text ueber Platzhalter:
//   _t("%1 Dateien").arg(n)
QString _t(const char *text);
QString _t(const QString &text);

} // namespace ncssh::core
