// Einzeldatei-Vergleich: erzeugt einen Unified-Diff (Zeile, Art).
// (Port von core/filediff.py; difflib -> eigener LCS-basierter Unified-Diff)
#pragma once

#include <QString>
#include <utility>
#include <vector>

namespace ncssh::core {

// (zeile, art) mit art aus {hdr, hunk, add, del, ctx}.
using DiffRow = std::pair<QString, QString>;

std::vector<DiffRow> unified(const QString &a, const QString &b,
                             const QString &nameA, const QString &nameB,
                             int context = 3);

} // namespace ncssh::core
