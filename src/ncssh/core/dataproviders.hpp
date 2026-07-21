// Live-Datenquellen fuer *dynamische* Makro-Tasten.
//
// Eine dynamische Taste zeigt statt einer festen Beschriftung einen laufend
// aktualisierten Wert (Uhrzeit, Datum, CPU-/RAM-Auslastung ...). Uhr/Datum
// kommen mit QtCore aus; System-Werte nutzen die WinAPI (das Original nutzte
// optional psutil) und liefern bei Nichtverfuegbarkeit einen Hinweis statt
// eines Absturzes.
// (Port von core/dataproviders.py)
#pragma once

#include <QString>
#include <QStringList>

namespace ncssh::core::dataproviders {

// Provider-Namen in Anzeigereihenfolge (wie das PROVIDERS-Dict im Original).
QStringList providerNames();

// Beschriftung im Editor; "?" wenn unbekannt.
QString providerLabel(const QString &name);

// Braucht der Provider System-Statistiken? (Im Original: "braucht psutil?".)
bool needsPsutil(const QString &name);

// Aktuellen Wert eines Providers holen (nie werfen).
// "" bei unbekanntem Provider, Gedankenstrich bei Fehler/Nichtverfuegbarkeit.
QString value(const QString &name);

// Sind System-Statistiken verfuegbar? (Im Original: ist psutil importierbar?)
bool psutilAvailable();

} // namespace ncssh::core::dataproviders
