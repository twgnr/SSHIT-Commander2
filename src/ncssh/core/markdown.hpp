// Winziger Markdown->HTML-Renderer fuer die KI-Antwortanzeige (kein Dependency).
//
// Deckt das ab, was lokale LLMs typischerweise ausgeben und was fuer Config-/
// Code-Antworten zaehlt: eingezaeunte Code-Bloecke, Inline-Code, fett/kursiv,
// Ueberschriften, Aufzaehlungen und Absaetze. Das Ergebnis ist fuer
// QTextBrowser::setHtml gedacht; Styles sind inline, damit es themenunabhaengig
// funktioniert.
//
// Sicherheit: Der gesamte Modell-Output wird zuerst HTML-escaped — kein vom
// Modell erzeugtes Markup landet ungefiltert in der Anzeige.
#pragma once

#include <QString>

namespace ncssh::core {

// Rendert ein Markdown-Subset zu HTML fuer QTextBrowser::setHtml.
QString mdToHtml(const QString &text);

} // namespace ncssh::core
