#include "ncssh/core/keytools.hpp"

#include "ncssh/core/ppk.hpp"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <stdexcept>

namespace ncssh::core {

const std::vector<std::pair<QString, QString>> &keyTypes()
{
    static const std::vector<std::pair<QString, QString>> types = {
        {QStringLiteral("Ed25519 (empfohlen)"), QStringLiteral("ed25519")},
        {QStringLiteral("RSA 4096"), QStringLiteral("rsa-4096")},
        {QStringLiteral("RSA 3072"), QStringLiteral("rsa-3072")},
        {QStringLiteral("ECDSA nistp256"), QStringLiteral("ecdsa")},
    };
    return types;
}

static void fail(const QString &msg)
{
    throw std::runtime_error(msg.toStdString());
}

std::pair<QByteArray, QByteArray> generate(const QString &keyType, const QString &comment)
{
    QString type;
    QStringList extra;
    const QString kt = keyType.toLower();
    if (kt == QLatin1String("ed25519")) {
        type = QStringLiteral("ed25519");
    } else if (kt == QLatin1String("rsa") || kt == QLatin1String("rsa-3072")) {
        type = QStringLiteral("rsa");
        extra << QStringLiteral("-b") << QStringLiteral("3072");
    } else if (kt == QLatin1String("rsa-4096")) {
        type = QStringLiteral("rsa");
        extra << QStringLiteral("-b") << QStringLiteral("4096");
    } else if (kt == QLatin1String("ecdsa")) {
        type = QStringLiteral("ecdsa");
        extra << QStringLiteral("-b") << QStringLiteral("256");
    } else {
        fail(QStringLiteral("Unbekannter Schlüsseltyp: %1").arg(keyType));
    }

    QTemporaryDir dir;
    if (!dir.isValid())
        fail("Konnte kein temporaeres Verzeichnis anlegen.");
    const QString keyPath = dir.filePath(QStringLiteral("id_key"));

    QStringList args{QStringLiteral("-t"), type};
    args += extra;
    args << QStringLiteral("-N") << QString()           // keine Passphrase
         << QStringLiteral("-C") << comment
         << QStringLiteral("-f") << QDir::toNativeSeparators(keyPath);
    QProcess proc;
    proc.start(QStringLiteral("ssh-keygen"), args);
    if (!proc.waitForStarted(5000))
        fail("Konnte 'ssh-keygen' nicht starten (OpenSSH-Client noetig).");
    if (!proc.waitForFinished(30000)) {
        proc.kill();
        fail("Schluesselerzeugung hat zu lange gedauert.");
    }
    if (proc.exitCode() != 0)
        fail(QStringLiteral("ssh-keygen fehlgeschlagen: %1")
                 .arg(QString::fromLocal8Bit(proc.readAllStandardError())));

    QFile privFile(keyPath);
    QFile pubFile(keyPath + QStringLiteral(".pub"));
    if (!privFile.open(QIODevice::ReadOnly) || !pubFile.open(QIODevice::ReadOnly))
        fail("Erzeugte Schluesseldateien nicht lesbar.");
    return {privFile.readAll(), pubFile.readAll()};
}

QByteArray toPpk(const QByteArray &opensshPrivate, const QString &comment)
{
    return opensshToPpk(opensshPrivate, comment);
}

QByteArray toOpenssh(const QByteArray &ppkData)
{
    return ppkToOpenssh(ppkData);
}

QByteArray publicFromPrivate(const QByteArray &opensshPrivate)
{
    QTemporaryDir dir;
    if (!dir.isValid())
        fail("Konnte kein temporaeres Verzeichnis anlegen.");
    const QString keyPath = dir.filePath(QStringLiteral("id_key"));
    QFile f(keyPath);
    if (!f.open(QIODevice::WriteOnly))
        fail("Konnte Schluessel nicht zwischenspeichern.");
    f.write(opensshPrivate);
    f.close();
#ifndef Q_OS_WIN
    QFile::setPermissions(keyPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
#endif

    QProcess proc;
    proc.start(QStringLiteral("ssh-keygen"),
               {QStringLiteral("-y"), QStringLiteral("-f"),
                QDir::toNativeSeparators(keyPath)});
    if (!proc.waitForStarted(5000))
        fail("Konnte 'ssh-keygen' nicht starten.");
    if (!proc.waitForFinished(15000)) {
        proc.kill();
        fail("Public-Key-Ableitung hat zu lange gedauert.");
    }
    if (proc.exitCode() != 0)
        fail(QStringLiteral("ssh-keygen -y fehlgeschlagen: %1")
                 .arg(QString::fromLocal8Bit(proc.readAllStandardError())));
    return proc.readAllStandardOutput().trimmed();
}

bool keyNeedsPassphrase(const QByteArray &privateKey)
{
    // --- OpenSSH-Format ------------------------------------------------------
    // Aufbau des dekodierten Rumpfs:
    //   "openssh-key-v1\0" | uint32 len | ciphername | uint32 len | kdfname | …
    // ciphername == "none" bedeutet unverschluesselt. Im base64-Text steht das
    // nicht lesbar, deshalb wird hier wirklich dekodiert.
    const int begin = privateKey.indexOf("BEGIN OPENSSH PRIVATE KEY");
    if (begin >= 0) {
        const int bodyStart = privateKey.indexOf('\n', begin);
        const int end = privateKey.indexOf("-----END", bodyStart);
        if (bodyStart < 0 || end < 0)
            return false;
        QByteArray base64 = privateKey.mid(bodyStart + 1, end - bodyStart - 1);
        base64.replace('\n', "").replace('\r', "");
        const QByteArray blob = QByteArray::fromBase64(base64);
        static const QByteArray magic("openssh-key-v1\0", 15);
        if (!blob.startsWith(magic))
            return false;
        int pos = magic.size();
        if (blob.size() < pos + 4)
            return false;
        // Laenge liegt als big-endian uint32 vor.
        const auto readLength = [&blob](int at) {
            return (quint32(uchar(blob[at])) << 24) | (quint32(uchar(blob[at + 1])) << 16)
                   | (quint32(uchar(blob[at + 2])) << 8) | quint32(uchar(blob[at + 3]));
        };
        const quint32 cipherLen = readLength(pos);
        pos += 4;
        if (cipherLen > quint32(blob.size() - pos))
            return false;
        const QByteArray cipher = blob.mid(pos, int(cipherLen));
        return cipher != "none";
    }

    // --- klassisches PEM -----------------------------------------------------
    // Dort steht die Verschluesselung im Klartext-Kopf.
    return privateKey.contains("Proc-Type: 4,ENCRYPTED")
           || privateKey.contains("BEGIN ENCRYPTED PRIVATE KEY");
}

bool keyFileNeedsPassphrase(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    return keyNeedsPassphrase(file.readAll());
}

} // namespace ncssh::core
