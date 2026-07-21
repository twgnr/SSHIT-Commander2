#include "ncssh/core/i18n.hpp"

#include "ncssh/core/settings.hpp"

#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>

namespace ncssh::core {

// Reihenfolge = Anzeigereihenfolge in der Sprachauswahl. "de" ist die Quelle.
static const QStringList kLanguages = {QStringLiteral("de"), QStringLiteral("en")};

static QHash<QString, QString> g_catalog;
static QString g_current = QStringLiteral("de");
static bool g_loaded = false;

QStringList availableLanguages() { return kLanguages; }

QString languageName(const QString &code)
{
    if (code == QLatin1String("de")) return QStringLiteral("Deutsch");
    if (code == QLatin1String("en")) return QStringLiteral("English");
    return code;
}

static QHash<QString, QString> loadCatalog(const QString &code)
{
    QHash<QString, QString> catalog;
    if (code == QLatin1String("de"))
        return catalog;
    QFile f(QStringLiteral(":/i18n/%1.json").arg(code));
    if (!f.open(QIODevice::ReadOnly))
        return catalog;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return catalog;
    const QJsonObject obj = doc.object();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        const QString v = it.value().toString();
        if (!v.isEmpty())  // leere Uebersetzungen -> Fallback auf Quelltext
            catalog.insert(it.key(), v);
    }
    return catalog;
}

static void ensureLoaded()
{
    if (g_loaded)
        return;
    QString code = getSettingString(QStringLiteral("language"), QStringLiteral("de"));
    if (!kLanguages.contains(code))
        code = QStringLiteral("de");
    g_current = code;
    g_catalog = loadCatalog(code);
    g_loaded = true;
}

void setLanguage(const QString &code)
{
    g_current = kLanguages.contains(code) ? code : QStringLiteral("de");
    g_catalog = loadCatalog(g_current);
    g_loaded = true;
}

QString currentLanguage()
{
    ensureLoaded();
    return g_current;
}

QString _t(const QString &text)
{
    ensureLoaded();
    return g_catalog.value(text, text);
}

QString _t(const char *text)
{
    return _t(QString::fromUtf8(text));
}

} // namespace ncssh::core
