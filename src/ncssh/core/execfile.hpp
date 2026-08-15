// Erkennung ausfuehrbarer Dateien fuer die optionale Farbmarkierung in der Pane.
// Zwei Signale, OS-uebergreifend kombiniert: das POSIX-Ausfuehrbar-Bit (greift
// bei lokalen Unix-Dateien und SFTP-Servern) und bekannte ausfuehrbare
// Endungen (v.a. unter Windows).
#pragma once

#include "ncssh/core/models.hpp"

namespace ncssh::core {

// True, wenn der Eintrag eine ausfuehrbare Datei ist (keine Ordner/Symlinks).
bool isExecutable(const FileEntry &entry);

} // namespace ncssh::core
