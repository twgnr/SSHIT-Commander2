#include "ncssh/core/applock.hpp"

#include "ncssh/config.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageAuthenticationCode>
#include <QPasswordDigestor>
#include <QRandomGenerator>
#include <QUrl>

#include <algorithm>
#include <cstring>

#include <mutex>
#include <stdexcept>
#include <utility>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <bcrypt.h>
#  include <dpapi.h>
#endif

namespace ncssh::core::applock {

namespace {

constexpr int kIterations = 200'000;   // PBKDF2-HMAC-SHA256
constexpr qint64 kStep = 30;           // TOTP-Zeitschritt (Sekunden)
const char kBase32[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
// Wiederherstellungscodes ohne verwechselbare Zeichen (0/O, 1/I).
const char kRecoveryAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

QByteArray randomBytes(int count)
{
    QByteArray out(count, Qt::Uninitialized);
    for (int i = 0; i < count; ++i)
        out[i] = char(QRandomGenerator::system()->bounded(256));
    return out;
}

// Datenschluessel nur im Speicher dieses Prozesses (nach dem Entsperren).
std::mutex g_keyMutex;
QByteArray g_dataKey;

QByteArray currentDataKey()
{
    const std::lock_guard<std::mutex> lock(g_keyMutex);
    return g_dataKey;
}

void setDataKey(const QByteArray &key)
{
    const std::lock_guard<std::mutex> lock(g_keyMutex);
    g_dataKey = key;
}

const QByteArray kFileAad("ncssh-file-v1");
const QByteArray kKeyAad("ncssh-datakey-v1");

// AES-256-GCM ueber Windows CNG. Liefert {Chiffrat, Tag}; leer bei Fehler.
std::optional<std::pair<QByteArray, QByteArray>> gcmEncrypt(const QByteArray &key, const QByteArray &nonce,
                                                            const QByteArray &plain, const QByteArray &aad)
{
#ifdef Q_OS_WIN
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_KEY_HANDLE hKey = nullptr;
    std::optional<std::pair<QByteArray, QByteArray>> result;
    if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0))
        && BCRYPT_SUCCESS(BCryptSetProperty(alg, BCRYPT_CHAINING_MODE,
                                            reinterpret_cast<PUCHAR>(const_cast<wchar_t *>(BCRYPT_CHAIN_MODE_GCM)),
                                            sizeof(BCRYPT_CHAIN_MODE_GCM), 0))
        && BCRYPT_SUCCESS(BCryptGenerateSymmetricKey(alg, &hKey, nullptr, 0,
                                                     reinterpret_cast<PUCHAR>(const_cast<char *>(key.constData())),
                                                     ULONG(key.size()), 0))) {
        QByteArray tag(16, '\0');
        QByteArray out(plain.size(), '\0');
        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
        BCRYPT_INIT_AUTH_MODE_INFO(info);
        info.pbNonce = reinterpret_cast<PUCHAR>(const_cast<char *>(nonce.constData()));
        info.cbNonce = ULONG(nonce.size());
        info.pbAuthData = reinterpret_cast<PUCHAR>(const_cast<char *>(aad.constData()));
        info.cbAuthData = ULONG(aad.size());
        info.pbTag = reinterpret_cast<PUCHAR>(tag.data());
        info.cbTag = ULONG(tag.size());
        ULONG written = 0;
        if (BCRYPT_SUCCESS(BCryptEncrypt(hKey, reinterpret_cast<PUCHAR>(const_cast<char *>(plain.constData())),
                                         ULONG(plain.size()), &info, nullptr, 0,
                                         reinterpret_cast<PUCHAR>(out.data()), ULONG(out.size()),
                                         &written, 0)))
            result = std::make_pair(out.left(int(written)), tag);
    }
    if (hKey)
        BCryptDestroyKey(hKey);
    if (alg)
        BCryptCloseAlgorithmProvider(alg, 0);
    return result;
#else
    Q_UNUSED(key); Q_UNUSED(nonce); Q_UNUSED(plain); Q_UNUSED(aad);
    return std::nullopt;   // Verschluesselung nur unter Windows
#endif
}

std::optional<QByteArray> gcmDecrypt(const QByteArray &key, const QByteArray &nonce, const QByteArray &cipher,
                                     const QByteArray &tag, const QByteArray &aad)
{
#ifdef Q_OS_WIN
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_KEY_HANDLE hKey = nullptr;
    std::optional<QByteArray> result;
    if (key.size() == 32 && nonce.size() == 12 && tag.size() == 16
        && BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0))
        && BCRYPT_SUCCESS(BCryptSetProperty(alg, BCRYPT_CHAINING_MODE,
                                            reinterpret_cast<PUCHAR>(const_cast<wchar_t *>(BCRYPT_CHAIN_MODE_GCM)),
                                            sizeof(BCRYPT_CHAIN_MODE_GCM), 0))
        && BCRYPT_SUCCESS(BCryptGenerateSymmetricKey(alg, &hKey, nullptr, 0,
                                                     reinterpret_cast<PUCHAR>(const_cast<char *>(key.constData())),
                                                     ULONG(key.size()), 0))) {
        QByteArray out(cipher.size(), '\0');
        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
        BCRYPT_INIT_AUTH_MODE_INFO(info);
        info.pbNonce = reinterpret_cast<PUCHAR>(const_cast<char *>(nonce.constData()));
        info.cbNonce = ULONG(nonce.size());
        info.pbAuthData = reinterpret_cast<PUCHAR>(const_cast<char *>(aad.constData()));
        info.cbAuthData = ULONG(aad.size());
        info.pbTag = reinterpret_cast<PUCHAR>(const_cast<char *>(tag.constData()));
        info.cbTag = ULONG(tag.size());
        ULONG written = 0;
        // Falscher Schluessel oder veraenderte Daten -> Tag passt nicht -> Fehler.
        if (BCRYPT_SUCCESS(BCryptDecrypt(hKey, reinterpret_cast<PUCHAR>(const_cast<char *>(cipher.constData())),
                                         ULONG(cipher.size()), &info, nullptr, 0,
                                         reinterpret_cast<PUCHAR>(out.data()), ULONG(out.size()),
                                         &written, 0)))
            result = out.left(int(written));
    }
    if (hKey)
        BCryptDestroyKey(hKey);
    if (alg)
        BCryptCloseAlgorithmProvider(alg, 0);
    return result;
#else
    Q_UNUSED(key); Q_UNUSED(nonce); Q_UNUSED(cipher); Q_UNUSED(tag); Q_UNUSED(aad);
    return std::nullopt;
#endif
}

// Vergleich in konstanter Zeit (kein Abbruch beim ersten Unterschied).
bool sameBytes(const QByteArray &a, const QByteArray &b)
{
    if (a.size() != b.size())
        return false;
    unsigned char diff = 0;
    for (qsizetype i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned char>(a.at(i) ^ b.at(i));
    return diff == 0;
}

QByteArray derive(const QString &password, const QByteArray &salt, int iterations)
{
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256, password.toUtf8(),
                                              salt, iterations, 32);
}

// TOTP-Geheimnis an das Windows-Konto binden (DPAPI).
QByteArray protect(const QByteArray &plain)
{
#ifdef Q_OS_WIN
    DATA_BLOB in{DWORD(plain.size()), reinterpret_cast<BYTE *>(const_cast<char *>(plain.constData()))};
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"SSHIT-Commander", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &out))
        return {};
    const QByteArray blob(reinterpret_cast<const char *>(out.pbData), int(out.cbData));
    LocalFree(out.pbData);
    return blob;
#else
    return plain;
#endif
}

std::optional<QByteArray> unprotect(const QByteArray &blob)
{
#ifdef Q_OS_WIN
    DATA_BLOB in{DWORD(blob.size()), reinterpret_cast<BYTE *>(const_cast<char *>(blob.constData()))};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        return std::nullopt;
    const QByteArray plain(reinterpret_cast<const char *>(out.pbData), int(out.cbData));
    LocalFree(out.pbData);
    return plain;
#else
    return blob;
#endif
}

QJsonObject load()
{
    QFile f(lockFile());
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    return doc.isObject() ? doc.object() : QJsonObject{};
}

void store(const QJsonObject &state)
{
    ncssh::atomicWriteText(lockFile(), QString::fromUtf8(QJsonDocument(state).toJson()));
}

QString normalizeRecovery(const QString &code)
{
    QString out;
    for (const QChar c : code)
        if (c.isLetterOrNumber())
            out += c.toUpper();
    return out;
}

QString recoveryHash(const QString &code, const QByteArray &salt)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(salt + normalizeRecovery(code).toUtf8(), QCryptographicHash::Sha256)
            .toHex());
}

QByteArray saltOf(const QJsonObject &state)
{
    return QByteArray::fromBase64(state.value(QStringLiteral("salt")).toString().toLatin1());
}

QByteArray b64(const QJsonObject &o, const char *key)
{
    return QByteArray::fromBase64(o.value(QString::fromLatin1(key)).toString().toLatin1());
}

bool hasWrappedKey(const QJsonObject &state)
{
    return state.contains(QStringLiteral("key_data"));
}

// Datenschluessel mit dem Passwort verpacken. Eigenes Salz, damit der
// Pruef-Hash des Passworts nie zugleich der Schluessel ist.
void wrapDataKey(QJsonObject &state, const QByteArray &dataKey, const QString &password)
{
    const QByteArray salt = randomBytes(16);
    const QByteArray nonce = randomBytes(12);
    const QByteArray kek = derive(password, salt, kIterations);
    const auto sealed = gcmEncrypt(kek, nonce, dataKey, kKeyAad);
    if (!sealed)
        throw std::runtime_error("Datenschluessel konnte nicht verschluesselt werden.");
    state.insert(QStringLiteral("key_salt"), QString::fromLatin1(salt.toBase64()));
    state.insert(QStringLiteral("key_iterations"), kIterations);
    state.insert(QStringLiteral("key_nonce"), QString::fromLatin1(nonce.toBase64()));
    state.insert(QStringLiteral("key_data"), QString::fromLatin1(sealed->first.toBase64()));
    state.insert(QStringLiteral("key_tag"), QString::fromLatin1(sealed->second.toBase64()));
}

std::optional<QByteArray> unwrapDataKey(const QJsonObject &state, const QString &password)
{
    if (!hasWrappedKey(state))
        return std::nullopt;
    const QByteArray kek = derive(password, b64(state, "key_salt"),
                                  state.value(QStringLiteral("key_iterations")).toInt(kIterations));
    return gcmDecrypt(kek, b64(state, "key_nonce"), b64(state, "key_data"), b64(state, "key_tag"),
                      kKeyAad);
}

} // namespace

QString lockFile()
{
    return ncssh::configDir() + QStringLiteral("/applock.json");
}

bool isEnabled()
{
    // Unlesbare/kaputte Datei gilt als "keine Sperre" — sonst sperrte ein
    // Schreibfehler den Nutzer dauerhaft aus (entfernen kann man sie ohnehin).
    const QJsonObject state = load();
    return !state.value(QStringLiteral("hash")).toString().isEmpty()
           && !state.value(QStringLiteral("salt")).toString().isEmpty();
}

bool hasTwoFactor()
{
    return isEnabled() && !load().value(QStringLiteral("totp")).toString().isEmpty();
}

void setPassword(const QString &password, const QString &currentPassword)
{
    QJsonObject state = load();
    // Datenschluessel bestimmen, BEVOR etwas geschrieben wird: aus dem
    // Speicher, sonst mit dem bisherigen Passwort entpackt; nur ohne
    // vorhandenen Schluessel einen neuen erzeugen. Ginge er verloren, waeren
    // die verschluesselten Profile unlesbar.
    QByteArray dataKey = currentDataKey();
    if (dataKey.isEmpty() && hasWrappedKey(state) && !currentPassword.isEmpty())
        dataKey = unwrapDataKey(state, currentPassword).value_or(QByteArray());
    if (dataKey.isEmpty() && hasWrappedKey(state))
        throw std::runtime_error("Der Datenschluessel ist gesperrt — bisheriges Passwort noetig.");
    if (dataKey.isEmpty())
        dataKey = randomBytes(32);
    wrapDataKey(state, dataKey, password);
    // Frisches Salz bei jeder Aenderung; 2FA und Wiederherstellungscodes (mit
    // eigenem Salz) bleiben unberuehrt.
    const QByteArray salt = randomBytes(16);
    state.insert(QStringLiteral("version"), 1);
    state.insert(QStringLiteral("salt"), QString::fromLatin1(salt.toBase64()));
    state.insert(QStringLiteral("iterations"), kIterations);
    state.insert(QStringLiteral("hash"),
                 QString::fromLatin1(derive(password, salt, kIterations).toBase64()));
    store(state);
    setDataKey(dataKey);
}

bool verifyPassword(const QString &password)
{
    const QJsonObject state = load();
    const QByteArray salt = saltOf(state);
    const QByteArray hash =
        QByteArray::fromBase64(state.value(QStringLiteral("hash")).toString().toLatin1());
    const int iterations = state.value(QStringLiteral("iterations")).toInt(kIterations);
    if (salt.isEmpty() || hash.isEmpty() || iterations < 1)
        return false;
    return sameBytes(derive(password, salt, iterations), hash);
}

void disable()
{
    QFile::remove(lockFile());
    forgetDataKey();
}

bool unlock(const QString &password)
{
    if (!verifyPassword(password))
        return false;
    QJsonObject state = load();
    if (!hasWrappedKey(state)) {
        // Sperre aus 1.0.4 (noch ohne Verschluesselung): jetzt einen
        // Datenschluessel anlegen; die Profile verschluesselt das Speichern.
        const QByteArray dataKey = randomBytes(32);
        wrapDataKey(state, dataKey, password);
        store(state);
        setDataKey(dataKey);
        return true;
    }
    const auto dataKey = unwrapDataKey(state, password);
    if (!dataKey)
        return false;
    setDataKey(*dataKey);
    return true;
}

bool hasDataKey()
{
    return !currentDataKey().isEmpty();
}

void forgetDataKey()
{
    setDataKey({});
}

bool isEncrypted(const QByteArray &bytes)
{
    // Schneller Vorabtest, damit normale JSON-Dateien nicht geparst werden.
    if (!bytes.contains("ncssh-encrypted"))
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes);
    return doc.isObject()
           && doc.object().value(QStringLiteral("_format")).toString() == QLatin1String("ncssh-encrypted");
}

QByteArray encryptData(const QByteArray &plain)
{
    const QByteArray key = currentDataKey();
    if (key.isEmpty())
        throw std::runtime_error("Kein Datenschluessel — App ist nicht entsperrt.");
    const QByteArray nonce = randomBytes(12);   // je Schreibvorgang neu (GCM!)
    const auto sealed = gcmEncrypt(key, nonce, plain, kFileAad);
    if (!sealed)
        throw std::runtime_error("Verschluesselung fehlgeschlagen.");
    const QJsonObject env{
        {QStringLiteral("_format"), QStringLiteral("ncssh-encrypted")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("alg"), QStringLiteral("AES-256-GCM")},
        {QStringLiteral("nonce"), QString::fromLatin1(nonce.toBase64())},
        {QStringLiteral("data"), QString::fromLatin1(sealed->first.toBase64())},
        {QStringLiteral("tag"), QString::fromLatin1(sealed->second.toBase64())},
    };
    return QJsonDocument(env).toJson(QJsonDocument::Indented);
}

std::optional<QByteArray> decryptData(const QByteArray &envelope)
{
    const QByteArray key = currentDataKey();
    if (key.isEmpty() || !isEncrypted(envelope))
        return std::nullopt;
    const QJsonObject env = QJsonDocument::fromJson(envelope).object();
    return gcmDecrypt(key, b64(env, "nonce"), b64(env, "data"), b64(env, "tag"), kFileAad);
}

void writeProtectedFile(const QString &path, const QByteArray &plain)
{
    if (!isEnabled()) {
        ncssh::atomicWriteText(path, QString::fromUtf8(plain));
        return;
    }
    // Aktive Sperre ohne Schluessel: lieber Fehler als Klartext auf der Platte.
    ncssh::atomicWriteText(path, QString::fromLatin1(encryptData(plain)));
}

QString base32Encode(const QByteArray &data)
{
    QString out;
    int buffer = 0, bits = 0;
    for (const char ch : data) {
        buffer = (buffer << 8) | static_cast<unsigned char>(ch);
        bits += 8;
        while (bits >= 5) {
            out += QLatin1Char(kBase32[(buffer >> (bits - 5)) & 31]);
            bits -= 5;
        }
    }
    if (bits > 0)
        out += QLatin1Char(kBase32[(buffer << (5 - bits)) & 31]);
    return out;
}

QByteArray base32Decode(const QString &text)
{
    QByteArray out;
    int buffer = 0, bits = 0;
    for (const QChar c : text) {
        if (c.isSpace() || c == QLatin1Char('=') || c == QLatin1Char('-'))
            continue;
        const char up = c.toUpper().toLatin1();
        const char *pos = std::strchr(kBase32, up);
        if (!pos || up == 0)
            return {};   // ungueltiges Zeichen
        buffer = (buffer << 5) | int(pos - kBase32);
        bits += 5;
        if (bits >= 8) {
            out += char((buffer >> (bits - 8)) & 0xFF);
            bits -= 8;
        }
    }
    return out;
}

QString newTotpSecret()
{
    return base32Encode(randomBytes(20));
}

QString totpCode(const QByteArray &key, qint64 unixSeconds, int digits)
{
    const quint64 counter = quint64(unixSeconds / kStep);
    QByteArray msg(8, '\0');
    for (int i = 7; i >= 0; --i)
        msg[i] = char((counter >> (8 * (7 - i))) & 0xFF);
    const QByteArray mac = QMessageAuthenticationCode::hash(msg, key, QCryptographicHash::Sha1);
    const int offset = mac.at(mac.size() - 1) & 0x0F;
    const quint32 binary = (quint32(static_cast<unsigned char>(mac.at(offset)) & 0x7F) << 24)
                           | (quint32(static_cast<unsigned char>(mac.at(offset + 1))) << 16)
                           | (quint32(static_cast<unsigned char>(mac.at(offset + 2))) << 8)
                           | quint32(static_cast<unsigned char>(mac.at(offset + 3)));
    quint32 mod = 1;
    for (int i = 0; i < digits; ++i)
        mod *= 10;
    return QStringLiteral("%1").arg(binary % mod, digits, 10, QLatin1Char('0'));
}

bool verifyTotp(const QString &secretBase32, const QString &code, qint64 unixSeconds)
{
    const QByteArray key = base32Decode(secretBase32);
    const QString wanted = code.trimmed().remove(QLatin1Char(' '));
    if (key.isEmpty() || wanted.size() != 6)
        return false;
    for (int drift = -1; drift <= 1; ++drift)
        if (sameBytes(totpCode(key, unixSeconds + drift * kStep).toLatin1(), wanted.toLatin1()))
            return true;
    return false;
}

QString otpauthUri(const QString &secretBase32, const QString &account)
{
    const QString issuer = QStringLiteral("SSHIT-Commander");
    return QStringLiteral("otpauth://totp/%1:%2?secret=%3&issuer=%1&algorithm=SHA1&digits=6&period=30")
        .arg(QString::fromLatin1(QUrl::toPercentEncoding(issuer)),
             QString::fromLatin1(QUrl::toPercentEncoding(account)), secretBase32);
}

QStringList newRecoveryCodes(int count)
{
    QStringList codes;
    for (int n = 0; n < count; ++n) {
        QString code;
        for (int i = 0; i < 10; ++i) {
            if (i == 5)
                code += QLatin1Char('-');
            code += QLatin1Char(kRecoveryAlphabet[QRandomGenerator::system()->bounded(32)]);
        }
        codes << code;
    }
    return codes;
}

void replaceRecoveryCodes(const QStringList &recoveryCodes)
{
    QJsonObject state = load();
    const QByteArray salt = randomBytes(16);
    QJsonArray hashes;
    for (const QString &code : recoveryCodes)
        hashes.append(recoveryHash(code, salt));
    state.insert(QStringLiteral("recovery_salt"), QString::fromLatin1(salt.toBase64()));
    state.insert(QStringLiteral("recovery"), hashes);
    store(state);
}

void enableTwoFactor(const QString &secretBase32, const QStringList &recoveryCodes)
{
    QJsonObject state = load();
    state.insert(QStringLiteral("totp"),
                 QString::fromLatin1(protect(secretBase32.toLatin1()).toBase64()));
    state.remove(QStringLiteral("totp_last"));
    store(state);
    replaceRecoveryCodes(recoveryCodes);
}

void disableTwoFactor()
{
    QJsonObject state = load();
    for (const char *key : {"totp", "totp_last", "recovery", "recovery_salt"})
        state.remove(QString::fromLatin1(key));
    store(state);
}

int remainingRecoveryCodes()
{
    return int(load().value(QStringLiteral("recovery")).toArray().size());
}

std::optional<QString> totpSecret()
{
    const QByteArray blob =
        QByteArray::fromBase64(load().value(QStringLiteral("totp")).toString().toLatin1());
    if (blob.isEmpty())
        return std::nullopt;
    const auto plain = unprotect(blob);
    if (!plain || plain->isEmpty())
        return std::nullopt;
    return QString::fromLatin1(*plain);
}

bool verifySecondFactor(const QString &input, qint64 unixSeconds)
{
    QJsonObject state = load();
    const QString compact = input.trimmed().remove(QLatin1Char(' '));
    // 6 Ziffern: TOTP. Ein Code gilt nur einmal (kein Wiederverwenden eines
    // mitgelesenen Codes innerhalb seines Zeitfensters).
    if (compact.size() == 6 && std::all_of(compact.begin(), compact.end(),
                                           [](QChar c) { return c.isDigit(); })) {
        const auto secret = totpSecret();
        if (!secret)
            return false;
        const QByteArray key = base32Decode(*secret);
        const qint64 last = qint64(state.value(QStringLiteral("totp_last")).toDouble(-1));
        for (int drift = -1; drift <= 1; ++drift) {
            const qint64 t = unixSeconds + drift * kStep;
            const qint64 counter = t / kStep;
            if (counter <= last)
                continue;
            if (sameBytes(totpCode(key, t).toLatin1(), compact.toLatin1())) {
                state.insert(QStringLiteral("totp_last"), double(counter));
                store(state);
                return true;
            }
        }
        return false;
    }
    // Sonst: Wiederherstellungscode (einmalig).
    const QByteArray salt = QByteArray::fromBase64(
        state.value(QStringLiteral("recovery_salt")).toString().toLatin1());
    const QString hash = recoveryHash(input, salt);
    QJsonArray hashes = state.value(QStringLiteral("recovery")).toArray();
    for (qsizetype i = 0; i < hashes.size(); ++i) {
        if (sameBytes(hashes.at(i).toString().toLatin1(), hash.toLatin1())) {
            hashes.removeAt(i);
            state.insert(QStringLiteral("recovery"), hashes);
            store(state);
            return true;
        }
    }
    return false;
}

} // namespace ncssh::core::applock
