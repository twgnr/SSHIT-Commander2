// Einzeldatei-Vergleich: erzeugt einen Unified-Diff (Zeile, Art).
#pragma once

#include <QString>
#include <utility>
#include <vector>

namespace ncssh::core {

// (zeile, art) mit art aus {hdr, hunk, add, del, ctx}.
using DiffRow = std::pair<QString, QString>;

// Speicher und Laufzeit sind gedeckelt (Myers-Diff mit Grenzen). Wird eine
// Grenze erreicht, faellt der Mittelteil auf "alles entfernt, alles neu"
// zurueck — dann wird *approximate (falls gesetzt) true. Aufwendig bei grossen
// Dateien: nicht im GUI-Thread aufrufen.
std::vector<DiffRow> unified(const QString &a, const QString &b,
                             const QString &nameA, const QString &nameB,
                             int context = 3, bool *approximate = nullptr);

} // namespace ncssh::core
