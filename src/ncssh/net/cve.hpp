// Online-CVE-Abgleich ueber OSV.dev (https://osv.dev) — frei, ohne API-Key.
//
// Beide Funktionen sind BLOCKIEREND (QNetworkAccessManager + QEventLoop im
// Worker) und laufen ueber die AsyncBridge auf Worker-Threads.
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace ncssh::net {

// queries: [{"package":{"name","ecosystem"},"version"}]. Liefert die
// OSV-Antwort.
//
// Die Batch-Antwort enthaelt pro Treffer nur die IDs (keine Beschreibung) —
// Details liefert osvGetVuln().
//
// Achtung: blockierend — ueber die AsyncBridge auf einem Worker-Thread
// aufrufen.
QJsonObject osvQuerybatch(const QJsonArray &queries, int timeoutMs = 20000);

// Holt die vollstaendigen Details zu einer OSV-/CVE-ID (Beschreibung,
// Schwere, Fix).
//
// Achtung: blockierend — ueber die AsyncBridge auf einem Worker-Thread
// aufrufen.
QJsonObject osvGetVuln(const QString &vulnId, int timeoutMs = 20000);

} // namespace ncssh::net
