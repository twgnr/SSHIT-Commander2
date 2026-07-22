// Tests fuer keyNeedsPassphrase — entscheidet beim Verbinden, ob nach der
// Passphrase gefragt wird. Ein falsches Ergebnis fuehrt entweder zu einer
// ueberfluessigen Abfrage oder zu einem unverstaendlichen Auth-Fehler.
#include "tests/harness.hpp"

#include "ncssh/core/keytools.hpp"

#include <QFile>
#include <QTemporaryDir>

using namespace ncssh::core;

namespace {

// Baut einen OpenSSH-Schluesselrumpf mit dem gewuenschten Cipher-Namen.
QByteArray opensshKey(const QByteArray &cipher, const QByteArray &kdf)
{
    const auto uint32be = [](quint32 value) {
        QByteArray out(4, '\0');
        out[0] = char((value >> 24) & 0xff);
        out[1] = char((value >> 16) & 0xff);
        out[2] = char((value >> 8) & 0xff);
        out[3] = char(value & 0xff);
        return out;
    };
    QByteArray blob("openssh-key-v1\0", 15);
    blob += uint32be(quint32(cipher.size())) + cipher;
    blob += uint32be(quint32(kdf.size())) + kdf;
    blob += uint32be(0);            // kdfoptions (leer)
    blob += uint32be(1);            // Anzahl Schluessel
    blob += uint32be(4) + "rest";   // Platzhalter

    QByteArray pem = "-----BEGIN OPENSSH PRIVATE KEY-----\n";
    const QByteArray base64 = blob.toBase64();
    for (int i = 0; i < base64.size(); i += 70)
        pem += base64.mid(i, 70) + "\n";
    pem += "-----END OPENSSH PRIVATE KEY-----\n";
    return pem;
}

} // namespace

TEST(keytools, openssh_unencrypted_needs_no_passphrase)
{
    CHECK_EQ(keyNeedsPassphrase(opensshKey("none", "none")), false);
}

TEST(keytools, openssh_encrypted_needs_passphrase)
{
    CHECK_EQ(keyNeedsPassphrase(opensshKey("aes256-ctr", "bcrypt")), true);
    CHECK_EQ(keyNeedsPassphrase(opensshKey("aes128-cbc", "bcrypt")), true);
}

TEST(keytools, openssh_base64_text_alone_is_not_decisive)
{
    // Regressionstest: eine reine Textsuche nach "bcrypt" im PEM haette hier
    // false geliefert, obwohl der Schluessel verschluesselt ist — der Name
    // steckt im base64-Rumpf und ist im Klartext nicht sichtbar.
    const QByteArray pem = opensshKey("aes256-ctr", "bcrypt");
    CHECK(!pem.contains("bcrypt"));      // im Text wirklich nicht enthalten
    CHECK_EQ(keyNeedsPassphrase(pem), true);
}

TEST(keytools, classic_pem_header)
{
    const QByteArray encrypted =
        "-----BEGIN RSA PRIVATE KEY-----\n"
        "Proc-Type: 4,ENCRYPTED\n"
        "DEK-Info: AES-128-CBC,0123456789ABCDEF\n\nAAAA\n"
        "-----END RSA PRIVATE KEY-----\n";
    CHECK_EQ(keyNeedsPassphrase(encrypted), true);

    const QByteArray plain =
        "-----BEGIN RSA PRIVATE KEY-----\nAAAA\n-----END RSA PRIVATE KEY-----\n";
    CHECK_EQ(keyNeedsPassphrase(plain), false);

    CHECK_EQ(keyNeedsPassphrase("-----BEGIN ENCRYPTED PRIVATE KEY-----\nAAAA\n"), true);
}

TEST(keytools, garbage_and_empty_are_safe)
{
    CHECK_EQ(keyNeedsPassphrase(QByteArray()), false);
    CHECK_EQ(keyNeedsPassphrase("kein schluessel"), false);
    // Abgeschnittener Rumpf darf nicht ueber das Ende hinauslesen.
    CHECK_EQ(keyNeedsPassphrase("-----BEGIN OPENSSH PRIVATE KEY-----\nAAAA\n"
                                "-----END OPENSSH PRIVATE KEY-----\n"),
             false);
}

TEST(keytools, file_variant_reads_from_disk)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("id_ed25519"));
    QFile f(path);
    CHECK(f.open(QIODevice::WriteOnly));
    f.write(opensshKey("aes256-ctr", "bcrypt"));
    f.close();
    CHECK_EQ(keyFileNeedsPassphrase(path), true);
    // Nicht vorhandene Datei -> keine Abfrage erzwingen.
    CHECK_EQ(keyFileNeedsPassphrase(tmp.filePath(QStringLiteral("fehlt"))), false);
}
