#include "ncssh/core/hostkeys.hpp"

#include "ncssh/config.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <stdexcept>

namespace ncssh::core {

HostKeyStore::HostKeyStore()
{
    load();
}

void HostKeyStore::load()
{
    const QString path = ncssh::hostKeysFile();
    QFile f(path);
    if (!f.exists())
        return;
    if (!f.open(QIODevice::ReadOnly))
        throw std::runtime_error(
            (QStringLiteral("Kann Datei nicht lesen: ") + path).toStdString());
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    m_data.clear();
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return;
    const QJsonObject raw = doc.object();
    for (auto it = raw.constBegin(); it != raw.constEnd(); ++it)
        m_data.insert(it.key(), it.value().isString() ? it.value().toString()
                                                      : it.value().toVariant().toString());
}

void HostKeyStore::save() const
{
    QJsonObject data;
    for (auto it = m_data.constBegin(); it != m_data.constEnd(); ++it)
        data.insert(it.key(), it.value());
    ncssh::atomicWriteText(
        ncssh::hostKeysFile(),
        QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Indented)));
}

QString HostKeyStore::key(const QString &host, int port, const QString &algo)
{
    const QString base = QStringLiteral("%1:%2").arg(host).arg(port);
    return algo.isEmpty() ? base : base + QLatin1Char('|') + algo;
}

std::optional<QString> HostKeyStore::get(const QString &host, int port,
                                         const QString &algo) const
{
    const auto it = m_data.constFind(key(host, port, algo));
    if (it == m_data.constEnd())
        return std::nullopt;
    return *it;
}

std::optional<QString> HostKeyStore::getLegacy(const QString &host, int port) const
{
    return get(host, port);
}

void HostKeyStore::add(const QString &host, int port, const QString &fingerprint,
                       const QString &algo)
{
    m_data.insert(key(host, port, algo), fingerprint);
    // Den unspezifischen Alt-Eintrag nur entfernen, wenn er zu GENAU diesem
    // Key gehoert (gleicher Fingerprint) — sonst wuerde der gueltige Pin eines
    // anderen Key-Typs verworfen und ein spaeterer MITM mit diesem Typ nur
    // als "unbekannt" statt "geaendert" gemeldet.
    if (!algo.isEmpty() && m_data.value(key(host, port)) == fingerprint)
        m_data.remove(key(host, port));
    save();
}

void HostKeyStore::remove(const QString &host, int port, const QString &algo)
{
    m_data.remove(key(host, port, algo));
    save();
}

void HostKeyStore::removeKey(const QString &rawKey)
{
    m_data.remove(rawKey);
    save();
}

} // namespace ncssh::core
