#include "ncssh/core/settings.hpp"

#include "ncssh/config.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

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

QVariant getSetting(const QString &key, const QVariant &defaultValue)
{
    const QJsonObject data = readAll();
    if (!data.contains(key))
        return defaultValue;
    return data.value(key).toVariant();
}

void setSetting(const QString &key, const QJsonValue &value)
{
    QJsonObject data = readAll();
    data.insert(key, value);
    const QJsonDocument doc(data);
    ncssh::atomicWriteText(settingsPath(),
                           QString::fromUtf8(doc.toJson(QJsonDocument::Indented)));
}

} // namespace ncssh::core
