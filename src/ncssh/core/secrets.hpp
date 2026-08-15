// Sichere Ablage von Passwoertern/Passphrasen im OS-Keyring.
// Windows: Credential Manager (WinVault). Faellt der Keyring aus, schlaegt der
// Zugriff still fehl — es wird nichts im Klartext persistiert.
#pragma once

#include <QString>
#include <optional>

namespace ncssh::core {

void setSecret(const QString &profileName, const QString &kind, const QString &value);
std::optional<QString> getSecret(const QString &profileName, const QString &kind);
void deleteSecret(const QString &profileName, const QString &kind);

} // namespace ncssh::core
