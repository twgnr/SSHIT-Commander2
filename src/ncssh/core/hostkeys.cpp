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
    // Erst ausserhalb des Locks parsen, dann in einem Schritt tauschen — so
    // sieht ein paralleler Leser nie einen halb gefuellten Bestand.
    QHash<QString, QString> fresh;
    if (err.error == QJsonParseError::NoError && doc.isObject()) {
        const QJsonObject raw = doc.object();
        for (auto it = raw.constBegin(); it != raw.constEnd(); ++it)
            fresh.insert(it.key(), it.value().isString()
                                       ? it.value().toString()
                                       : it.value().toVariant().toString());
    }
    std::lock_guard lock(m_mutex);
    m_data = std::move(fresh);
}

void HostKeyStore::save() const
{
    std::lock_guard lock(m_mutex);
    saveLocked();
}

void HostKeyStore::saveLocked() const
{
    // Unter dem Lock schreiben: zwei parallele save() duerfen sich nicht
    // ueberholen (sonst landet ggf. der aeltere Stand zuletzt auf der Platte).
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
    const QString k = key(host, port, algo);
    std::lock_guard lock(m_mutex);
    const auto it = m_data.constFind(k);
    if (it == m_data.constEnd())
        return std::nullopt;
    return QString(*it);  // Kopie, solange der Lock gehalten wird
}

std::optional<QString> HostKeyStore::getLegacy(const QString &host, int port) const
{
    return get(host, port);
}

QHash<QString, QString> HostKeyStore::entries() const
{
    std::lock_guard lock(m_mutex);
    // Eigene Kopie (detach) statt geteiltem Datenblock: die implizite
    // Referenzzaehlung von QHash ist zwar atomar, ein spaeteres detach im
    // Aufrufer-Thread wuerde aber ohne Lock aus m_data kopieren.
    QHash<QString, QString> copy = m_data;
    copy.detach();
    return copy;
}

void HostKeyStore::add(const QString &host, int port, const QString &fingerprint,
                       const QString &algo)
{
    std::lock_guard lock(m_mutex);
    m_data.insert(key(host, port, algo), fingerprint);
    // Den unspezifischen Alt-Eintrag nur entfernen, wenn er zu GENAU diesem
    // Key gehoert (gleicher Fingerprint) — sonst wuerde der gueltige Pin eines
    // anderen Key-Typs verworfen und ein spaeterer MITM mit diesem Typ nur
    // als "unbekannt" statt "geaendert" gemeldet.
    if (!algo.isEmpty() && m_data.value(key(host, port)) == fingerprint)
        m_data.remove(key(host, port));
    saveLocked();
}

void HostKeyStore::remove(const QString &host, int port, const QString &algo)
{
    std::lock_guard lock(m_mutex);
    m_data.remove(key(host, port, algo));
    saveLocked();
}

void HostKeyStore::removeKey(const QString &rawKey)
{
    std::lock_guard lock(m_mutex);
    m_data.remove(rawKey);
    saveLocked();
}

} // namespace ncssh::core
