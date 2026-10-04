#include "ncssh/core/settings.hpp"

#include "ncssh/config.hpp"

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QThread>

#include <stdexcept>

namespace ncssh::core {

static QString settingsPath()
{
    return ncssh::configDir() + QStringLiteral("/settings.json");
}

static QJsonObject readAll()
{
    QFile f(settingsPath());
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    // gueltiges JSON, aber kein Objekt (z.B. Liste) -> wie "nicht vorhanden"
    return doc.isObject() ? doc.object() : QJsonObject{};
}

// Fuer das Schreiben: den aktuellen Stand SICHER lesen. Eine vorhandene, aber
// (kurz) gesperrte Datei als "leer" zu behandeln hiesse, beim Zurueckschreiben
// alle anderen Einstellungen zu loeschen — dann lieber nicht schreiben.
static QJsonObject readAllForUpdate()
{
    const QString path = settingsPath();
    if (!QFile::exists(path))
        return {};
    for (int attempt = 0; attempt < 6; ++attempt) {
        if (attempt > 0)
            QThread::msleep(25 * attempt);
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            continue;   // gesperrt (Virenscanner …) -> erneut versuchen
        const QByteArray bytes = f.readAll();
        f.close();
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
        if (err.error == QJsonParseError::NoError && doc.isObject())
            return doc.object();
        // Wirklich kaputt (nicht nur gesperrt): sichern, dann neu beginnen —
        // so geht nichts unbemerkt verloren.
        QFile::copy(path, path + QStringLiteral(".kaputt-")
                              + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
        return {};
    }
    throw std::runtime_error(
        ("Einstellungen nicht lesbar (Datei gesperrt): " + path).toStdString());
}

QVariant getSetting(const QString &key, const QVariant &defaultValue)
{
    const QJsonObject data = readAll();
    if (!data.contains(key))
        return defaultValue;
    return data.value(key).toVariant();
}

void setSetting(const QString &key, const QJsonValue &value)
{
    QJsonObject data = readAllForUpdate();
    data.insert(key, value);
    const QJsonDocument doc(data);
    ncssh::atomicWriteText(settingsPath(),
                           QString::fromUtf8(doc.toJson(QJsonDocument::Indented)));
}

} // namespace ncssh::core
