#include "ncssh/core/execfile.hpp"

#include <QSet>

namespace ncssh::core {

// Endungen, die als "ausfuehrbar" gelten (klein geschrieben, mit Punkt).
static const QSet<QString> kExecExtensions = {
    QStringLiteral(".exe"), QStringLiteral(".com"), QStringLiteral(".bat"),
    QStringLiteral(".cmd"), QStringLiteral(".msi"), QStringLiteral(".ps1"),
    QStringLiteral(".psm1"), QStringLiteral(".vbs"), QStringLiteral(".scr"),
    QStringLiteral(".sh"), QStringLiteral(".bash"), QStringLiteral(".zsh"),
    QStringLiteral(".fish"), QStringLiteral(".ksh"), QStringLiteral(".run"),
    QStringLiteral(".bin"), QStringLiteral(".appimage"),
};

bool isExecutable(const FileEntry &entry)
{
    if (entry.type != EntryType::File)
        return false;
    if (entry.permissions & 0111)  // x-Bit fuer owner/group/other
        return true;
    const QString name = entry.name.toLower();
    for (const QString &ext : kExecExtensions) {
        if (name.endsWith(ext))
            return true;
    }
    return false;
}

} // namespace ncssh::core
