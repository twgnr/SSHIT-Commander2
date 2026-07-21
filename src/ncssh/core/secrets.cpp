#include "ncssh/core/secrets.hpp"

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <wincred.h>
#endif

namespace ncssh::core {

static QString targetName(const QString &profileName, const QString &kind)
{
    return QStringLiteral("ncssh:%1:%2").arg(profileName, kind);
}

#ifdef Q_OS_WIN

void setSecret(const QString &profileName, const QString &kind, const QString &value)
{
    if (value.isEmpty()) {
        deleteSecret(profileName, kind);
        return;
    }
    const QString target = targetName(profileName, kind);
    const QByteArray blob = value.toUtf8();
    CREDENTIALW cred{};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(target.utf16()));
    cred.CredentialBlobSize = static_cast<DWORD>(blob.size());
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(blob.constData()));
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    CredWriteW(&cred, 0);  // Fehler still ignorieren (Keyring optional)
}

std::optional<QString> getSecret(const QString &profileName, const QString &kind)
{
    const QString target = targetName(profileName, kind);
    PCREDENTIALW cred = nullptr;
    if (!CredReadW(reinterpret_cast<LPCWSTR>(target.utf16()), CRED_TYPE_GENERIC, 0, &cred))
        return std::nullopt;
    const QString value = QString::fromUtf8(
        reinterpret_cast<const char *>(cred->CredentialBlob),
        static_cast<int>(cred->CredentialBlobSize));
    CredFree(cred);
    return value;
}

void deleteSecret(const QString &profileName, const QString &kind)
{
    const QString target = targetName(profileName, kind);
    CredDeleteW(reinterpret_cast<LPCWSTR>(target.utf16()), CRED_TYPE_GENERIC, 0);
}

#else  // POSIX: kein Keyring-Backend eingebaut -> stiller No-Op wie in Python

void setSecret(const QString &, const QString &, const QString &) {}
std::optional<QString> getSecret(const QString &, const QString &) { return std::nullopt; }
void deleteSecret(const QString &, const QString &) {}

#endif

} // namespace ncssh::core
