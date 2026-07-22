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

} // namespace ncssh::core
