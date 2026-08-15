// PuTTY-PPK <-> OpenSSH-Konverter.
//
// Unterstuetzt unverschluesselte PPK v2/v3 fuer ssh-ed25519, ssh-rsa, ecdsa-*
// und ssh-dss. Verschluesselte PPK werden abgelehnt (Hinweis: in PuTTYgen als
// OpenSSH exportieren). Reine Bytes-Verarbeitung, gut testbar.
#pragma once

#include <QByteArray>
#include <QString>

namespace ncssh::core {

bool isPpk(const QByteArray &data);

// PPK -> OpenSSH-Privatschluessel (unverschluesselt). Wirft bei verschluesselter
// PPK oder nicht unterstuetztem Key-Typ (std::runtime_error).
QByteArray ppkToOpenssh(const QByteArray &data, const QString &passphrase = {});

// Unverschluesselter OpenSSH-Privatschluessel -> PuTTY-PPK (Version 2).
QByteArray opensshToPpk(const QByteArray &data, const QString &comment = {});

} // namespace ncssh::core
