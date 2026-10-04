// App-Sperre: Passwort beim Start, optional mit zweitem Faktor (TOTP aus einer
// Authenticator-App, RFC 6238) und einmaligen Wiederherstellungscodes.
//
// Gespeichert in einer EIGENEN Datei (applock.json im Konfigurationsordner),
// nicht in settings.json — der Konfigurations-Export/-Import darf die Sperre
// weder mitnehmen noch abschalten. Das Passwort liegt nur als PBKDF2-Hash
// vor, das TOTP-Geheimnis mit Windows-DPAPI an das Benutzerkonto gebunden,
// Wiederherstellungscodes nur als Hash.
//
// Ist die Sperre aktiv, sind die Serverprofile (servers.json) mit einem
// zufaelligen Datenschluessel verschluesselt (AES-256-GCM). Der Schluessel
// liegt nur mit dem Passwort verpackt vor (PBKDF2 -> AES-GCM): ohne Passwort
// sind die Profile nicht lesbar — auch nicht durch Loeschen von applock.json.
// Einstellungen, Verlauf und Lesezeichen bleiben unverschluesselt; Server-
// Passwoerter liegen ohnehin im Windows-Schluesselbund.
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <optional>

namespace ncssh::core::applock {

// Datei mit den Sperrdaten.
QString lockFile();

bool isEnabled();          // Passwort gesetzt -> Abfrage beim Start
bool hasTwoFactor();       // zusaetzlich TOTP-Code

// Mindestlaenge fuer neue Passwoerter.
constexpr int kMinPasswordLength = 8;

// Passwort setzen/aendern (aktiviert die Sperre; 2FA bleibt unveraendert).
// Der Datenschluessel der Profil-Verschluesselung wird dabei neu verpackt:
// aus dem Speicher (nach unlock) oder — falls noetig — mit currentPassword
// entpackt. Beim ersten Setzen entsteht ein neuer Datenschluessel.
void setPassword(const QString &password, const QString &currentPassword = {});
bool verifyPassword(const QString &password);
// Sperre komplett entfernen (inkl. 2FA) und den Datenschluessel vergessen.
// Vorher die verschluesselten Dateien entschluesselt zurueckschreiben!
void disable();

// --- Datenschluessel (Verschluesselung der Serverprofile) -----------------
// Beim Entsperren: Passwort pruefen und den Datenschluessel in den Speicher
// holen. Eine Sperre ohne Datenschluessel (aus 1.0.4) bekommt dabei einen.
bool unlock(const QString &password);
bool hasDataKey();
void forgetDataKey();
// Verschluesselter Umschlag (JSON, AES-256-GCM) bzw. dessen Erkennung.
bool isEncrypted(const QByteArray &bytes);
QByteArray encryptData(const QByteArray &plain);                 // braucht den Datenschluessel
std::optional<QByteArray> decryptData(const QByteArray &envelope);  // nullopt: kein Schluessel/manipuliert
// Datei schreiben: verschluesselt, wenn die Sperre aktiv ist. Wirft, wenn sie
// aktiv ist, aber der Datenschluessel fehlt (nie stillschweigend Klartext).
void writeProtectedFile(const QString &path, const QByteArray &plain);

// --- Zwei-Faktor (TOTP) ---------------------------------------------------
QString base32Encode(const QByteArray &data);              // ohne '='-Auffuellung
QByteArray base32Decode(const QString &text);              // toleriert Leer-/Kleinbuchstaben
QString newTotpSecret();                                   // 160 Bit, Base32
// Code fuer einen Zeitpunkt (30-s-Schritte, HMAC-SHA1).
QString totpCode(const QByteArray &key, qint64 unixSeconds, int digits = 6);
// Akzeptiert den aktuellen sowie den vorigen/naechsten Zeitschritt (Uhrabweichung).
bool verifyTotp(const QString &secretBase32, const QString &code, qint64 unixSeconds);
// otpauth://-Adresse fuer Authenticator-Apps.
QString otpauthUri(const QString &secretBase32, const QString &account);

// Neue Wiederherstellungscodes ("XXXXX-XXXXX"), nur zur einmaligen Anzeige.
QStringList newRecoveryCodes(int count = 8);
// 2FA einschalten (Geheimnis + Hashes der Wiederherstellungscodes speichern).
void enableTwoFactor(const QString &secretBase32, const QStringList &recoveryCodes);
void disableTwoFactor();
// Wiederherstellungscodes ersetzen (z. B. nach Verbrauch).
void replaceRecoveryCodes(const QStringList &recoveryCodes);
int remainingRecoveryCodes();
// Gespeichertes TOTP-Geheimnis (nullopt, wenn nicht lesbar, z. B. anderes
// Windows-Konto).
std::optional<QString> totpSecret();

// Zweiter Faktor beim Entsperren: TOTP-Code ODER ein Wiederherstellungscode
// (der dabei verbraucht wird).
bool verifySecondFactor(const QString &input, qint64 unixSeconds);

} // namespace ncssh::core::applock
