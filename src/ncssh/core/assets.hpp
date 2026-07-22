// Pfade zu mitgelieferten Ressourcen (Logos, Icons).
// Im C++-Port liegen die Assets im Qt-Ressourcensystem (":/assets/...").
// (Port von core/assets.py)
#pragma once

#include <QString>

namespace ncssh::core {

// Ressourcen-Pfad zu "ncssh/assets/<name>" oder leerer String, wenn nicht
// vorhanden. Rueckgabe ist ein Qt-Ressourcenpfad (":/assets/<name>"), der
// ueberall dort funktioniert, wo Qt Dateipfade akzeptiert (QPixmap, QFile...).
QString assetPath(const QString &name);

} // namespace ncssh::core
