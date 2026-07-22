#include "ncssh/core/macros.hpp"

#include "ncssh/config.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <stdexcept>

namespace ncssh::core::macros {

QString macrosFile()
{
    return ncssh::configDir() + QStringLiteral("/macros.json");
}

std::optional<QJsonObject> Layer::key(int index) const
{
    auto it = keys.find(index);
    if (it == keys.end())
        return std::nullopt;
    return it.value();
}

void Layer::setKey(int index, const QJsonObject &config)
{
    if (!config.isEmpty())
        keys.insert(index, config);
    else
        keys.remove(index);
}

QJsonObject Layer::toJson() const
{
    QJsonObject keysObj;
    for (auto it = keys.begin(); it != keys.end(); ++it)  // QMap sortiert nach Key
        keysObj.insert(QString::number(it.key()), it.value());
    return QJsonObject{
        {QStringLiteral("app"), app},
        {QStringLiteral("rows"), rows},
        {QStringLiteral("cols"), cols},
        {QStringLiteral("keys"), keysObj},
    };
}

Layer Layer::fromJson(const QString &name, const QJsonObject &data)
{
    Layer l;
    l.name = name;
    l.app = data.value(QStringLiteral("app")).toString();
    l.rows = data.value(QStringLiteral("rows")).toInt(kDefaultRows);
    if (l.rows <= 0) l.rows = kDefaultRows;
    l.cols = data.value(QStringLiteral("cols")).toInt(kDefaultCols);
    if (l.cols <= 0) l.cols = kDefaultCols;
    const QJsonObject keysObj = data.value(QStringLiteral("keys")).toObject();
    for (auto it = keysObj.begin(); it != keysObj.end(); ++it) {
        bool ok = false;
        const int idx = it.key().toInt(&ok);
        if (ok)
            l.keys.insert(idx, it.value().toObject());
    }
    return l;
}

QStringList MacroConfig::layerNames() const
{
    QStringList names = order;
    QStringList sorted = layers.keys();  // QMap::keys ist bereits sortiert
    for (const QString &name : sorted) {
        if (!names.contains(name))
            names.append(name);
    }
    QStringList out;
    for (const QString &n : names) {
        if (layers.contains(n))
            out.append(n);
    }
    return out;
}

Layer *MacroConfig::get(const QString &name)
{
    auto it = layers.find(name);
    return it == layers.end() ? nullptr : &it.value();
}

QString MacroConfig::uniqueName(const QString &desiredIn, const QString &original) const
{
    QString desired = desiredIn.trimmed();
    if (desired.isEmpty())
        desired = QStringLiteral("Layer");
    if (desired == original || !layers.contains(desired))
        return desired;
    int i = 1;
    while (layers.contains(QStringLiteral("%1_%2").arg(desired).arg(i)))
        ++i;
    return QStringLiteral("%1_%2").arg(desired).arg(i);
}

Layer &MacroConfig::addLayer(const QString &desired, int rows, int cols, const QString &app)
{
    const QString name = uniqueName(desired);
    Layer layer;
    layer.name = name;
    layer.app = app;
    layer.rows = rows;
    layer.cols = cols;
    layers.insert(name, layer);
    order.append(name);
    return layers[name];
}

QString MacroConfig::renameLayer(const QString &oldName, const QString &desired)
{
    if (!layers.contains(oldName))
        return oldName;
    const QString newName = uniqueName(desired, oldName);
    if (newName == oldName)
        return oldName;
    Layer layer = layers.take(oldName);
    layer.name = newName;
    layers.insert(newName, layer);
    for (QString &n : order) {
        if (n == oldName)
            n = newName;
    }
    return newName;
}

void MacroConfig::removeLayer(const QString &name)
{
    layers.remove(name);
    order.removeAll(name);
}

QJsonObject MacroConfig::toJson() const
{
    const QStringList names = layerNames();
    QJsonObject layersObj;
    for (const QString &n : names)
        layersObj.insert(n, layers.value(n).toJson());
    QJsonArray orderArr;
    for (const QString &n : names)
        orderArr.append(n);
    return QJsonObject{
        {QStringLiteral("version"), kConfigVersion},
        {QStringLiteral("settings"), QJsonObject{
            {QStringLiteral("key_size"), keySize},
            {QStringLiteral("dock"), dock},
            {QStringLiteral("open"), open},
            {QStringLiteral("mode"), mode},
            {QStringLiteral("context_aware"), contextAware},
        }},
        {QStringLiteral("order"), orderArr},
        {QStringLiteral("layers"), layersObj},
    };
}

MacroConfig MacroConfig::fromJson(const QJsonObject &data)
{
    MacroConfig cfg;
    const QJsonObject layersRaw = data.value(QStringLiteral("layers")).toObject();
    for (auto it = layersRaw.begin(); it != layersRaw.end(); ++it)
        cfg.layers.insert(it.key(), Layer::fromJson(it.key(), it.value().toObject()));
    for (const QJsonValue &v : data.value(QStringLiteral("order")).toArray()) {
        const QString n = v.toString();
        if (cfg.layers.contains(n))
            cfg.order.append(n);
    }
    const QJsonObject settings = data.value(QStringLiteral("settings")).toObject();
    cfg.keySize = settings.value(QStringLiteral("key_size")).toInt(kDefaultKeySize);
    if (cfg.keySize <= 0) cfg.keySize = kDefaultKeySize;
    cfg.dock = settings.value(QStringLiteral("dock")).toString(QStringLiteral("float"));
    cfg.open = settings.value(QStringLiteral("open")).toBool(false);
    cfg.mode = settings.value(QStringLiteral("mode")).toString(QStringLiteral("edit"));
    cfg.contextAware = settings.value(QStringLiteral("context_aware")).toBool(false);
    if (!cfg.layers.contains(kDefaultLayer))
        cfg.addLayer(kDefaultLayer);
    return cfg;
}

MacroConfig MacroConfig::makeDefault()
{
    MacroConfig cfg;
    cfg.addLayer(kDefaultLayer);
    return cfg;
}

MacroConfig load()
{
    QFile f(macrosFile());
    if (!f.open(QIODevice::ReadOnly))
        return MacroConfig::makeDefault();
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return MacroConfig::makeDefault();
    return MacroConfig::fromJson(doc.object());
}

void save(const MacroConfig &config)
{
    QFile f(macrosFile());
    if (!f.open(QIODevice::WriteOnly))
        throw std::runtime_error("Kann macros.json nicht schreiben.");
    f.write(QJsonDocument(config.toJson()).toJson(QJsonDocument::Indented));
}

QJsonObject exportBundle(const MacroConfig &config, const QStringList &namesIn)
{
    const QStringList names = namesIn.isEmpty() ? config.layerNames() : namesIn;
    QJsonObject layersObj;
    for (const QString &n : names) {
        if (config.layers.contains(n))
            layersObj.insert(n, config.layers.value(n).toJson());
    }
    return QJsonObject{
        {QStringLiteral("_format"), kExportFormat},
        {QStringLiteral("version"), kConfigVersion},
        {QStringLiteral("key_size"), config.keySize},
        {QStringLiteral("layers"), layersObj},
    };
}

void writeExport(const MacroConfig &config, const QString &path, const QStringList &names)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        throw std::runtime_error(("Kann Datei nicht schreiben: " + path).toStdString());
    f.write(QJsonDocument(exportBundle(config, names)).toJson(QJsonDocument::Indented));
}

QMap<QString, Layer> readImport(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        throw std::runtime_error(("Kann Datei nicht lesen: " + path).toStdString());
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        throw std::runtime_error("Ungültiges Dateiformat.");
    const QJsonObject layersRaw = doc.object().value(QStringLiteral("layers")).toObject();
    if (layersRaw.isEmpty())
        throw std::runtime_error("Die Datei enthält keine Layer.");
    QMap<QString, Layer> out;
    for (auto it = layersRaw.begin(); it != layersRaw.end(); ++it) {
        if (it.value().isObject())
            out.insert(it.key(), Layer::fromJson(it.key(), it.value().toObject()));
    }
    return out;
}

QJsonObject newKey(const QString &actionType, const QJsonValue &payload)
{
    return QJsonObject{
        {QStringLiteral("label"), QString()},
        {QStringLiteral("icon"), QString()},
        {QStringLiteral("label_pos"), QStringLiteral("bottom")},
        {QStringLiteral("font_color"), QStringLiteral("#ffffff")},
        {QStringLiteral("font_family"), QString()},
        {QStringLiteral("shortcut"), QString()},
        {QStringLiteral("action_type"), actionType},
        {QStringLiteral("payload"), payload},
    };
}

} // namespace ncssh::core::macros
