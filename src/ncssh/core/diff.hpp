// Verzeichnisvergleich (eine Ebene): Links/Rechts gegenueberstellen.
// (Port von core/diff.py)
#pragma once

#include "ncssh/core/filesystem.hpp"
#include "ncssh/core/models.hpp"

#include <QString>
#include <optional>
#include <vector>

namespace ncssh::core {

struct DiffEntry {
    QString name;
    QString status;  // left_only | right_only | newer_left | newer_right | same | dir
    std::optional<FileEntry> left;
    std::optional<FileEntry> right;
};

// Vergleich einer Ebene aus bereits gelisteten Eintraegen.
std::vector<DiffEntry> compare(const std::vector<FileEntry> &left,
                               const std::vector<FileEntry> &right);

// Rekursiver Vergleich (blockierend, im Worker); DiffEntry.name ist der
// relative Pfad.
std::vector<DiffEntry> compareRecursive(FileSystemProvider *lprov, const QString &lpath,
                                        FileSystemProvider *rprov, const QString &rpath,
                                        int limit = 5000);

} // namespace ncssh::core
