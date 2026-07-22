#include "ncssh/core/assets.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace ncssh::core {

QString assetPath(const QString &name)
{
    // 1. Qt-Ressourcensystem (einkompiliert — der Normalfall).
    const QString res = QStringLiteral(":/assets/") + name;
    if (QFileInfo::exists(res))
        return res;
    // 2. Neben der EXE (entpackte Verteilung).
    const QString beside = QCoreApplication::applicationDirPath()
                           + QStringLiteral("/assets/") + name;
    if (QFileInfo::exists(beside))
        return beside;
    return {};
}

} // namespace ncssh::core
