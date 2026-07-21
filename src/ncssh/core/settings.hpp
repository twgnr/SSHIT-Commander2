// Kleiner Einstellungs-Speicher (settings.json im Config-Verzeichnis).
#pragma once

#include <QJsonValue>
#include <QString>
#include <QVariant>

namespace ncssh::core {

QVariant getSetting(const QString &key, const QVariant &defaultValue = {});
void setSetting(const QString &key, const QJsonValue &value);

// Bequemlichkeit fuer die haeufigsten Typen
inline QString getSettingString(const QString &key, const QString &def = {})
{ return getSetting(key, def).toString(); }
inline bool getSettingBool(const QString &key, bool def = false)
{ return getSetting(key, def).toBool(); }
inline int getSettingInt(const QString &key, int def = 0)
{ return getSetting(key, def).toInt(); }

} // namespace ncssh::core
