// SSH-Schluessel erzeugen und zwischen OpenSSH und PuTTY (PPK) konvertieren.
// Erzeugung ueber das systemeigene ssh-keygen (OpenSSH-Format); die PPK-
// Konvertierung nutzt core/ppk.  (Port von core/keytools.py)
#pragma once

#include <QByteArray>
#include <QString>
#include <utility>
#include <vector>

namespace ncssh::core {

// (Anzeigename, interner Schluessel) — fuer die Typ-Auswahl im Dialog.
const std::vector<std::pair<QString, QString>> &keyTypes();

// Erzeugt ein Schluesselpaar. Gibt (privat_openssh, public_openssh) zurueck.
std::pair<QByteArray, QByteArray> generate(const QString &keyType = QStringLiteral("ed25519"),
                                           const QString &comment = {});

// OpenSSH-Privatschluessel -> PuTTY-PPK (v2, unverschluesselt).
QByteArray toPpk(const QByteArray &opensshPrivate, const QString &comment = {});

// PuTTY-PPK -> OpenSSH-Privatschluessel (unverschluesselt).
QByteArray toOpenssh(const QByteArray &ppkData);

// Oeffentlichen Schluessel (OpenSSH-Zeile) aus einem privaten ableiten.
QByteArray publicFromPrivate(const QByteArray &opensshPrivate);

} // namespace ncssh::core
