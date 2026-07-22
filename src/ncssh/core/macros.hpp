// Datenmodell und Persistenz fuer den Makro-Manager.
//
// Eine Konfiguration besteht aus mehreren Layern (Seiten); jeder Layer hat ein
// eigenes Raster (rows x cols) und eine Abbildung Tastenindex -> Tastenbelegung.
// Datei: <config_dir>/macros.json  (Schema-Version 1)
//
// Tastenbelegungen sind bewusst QJsonObject, weil das "payload" je nach
// "action_type" sehr unterschiedlich aussieht.  (Port von core/macros.py)
#pragma once

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <optional>

namespace ncssh::core::macros {

constexpr int kConfigVersion = 1;
constexpr int kDefaultRows = 4;
constexpr int kDefaultCols = 8;
constexpr int kDefaultKeySize = 96;
inline const QString kDefaultLayer = QStringLiteral("main");

QString macrosFile();

struct Layer {
    QString name;
    QString app;
    int rows = kDefaultRows;
    int cols = kDefaultCols;
    QMap<int, QJsonObject> keys;

    int capacity() const { return rows * cols; }
    std::optional<QJsonObject> key(int index) const;
    void setKey(int index, const QJsonObject &config);  // leeres Objekt -> entfernen

    QJsonObject toJson() const;
    static Layer fromJson(const QString &name, const QJsonObject &data);
};

struct MacroConfig {
    QMap<QString, Layer> layers;
    QStringList order;
    int keySize = kDefaultKeySize;
    QString dock = QStringLiteral("float");  // float | left | right | top | bottom
    bool open = false;
    QString mode = QStringLiteral("edit");   // edit | run
    bool contextAware = false;

    QStringList layerNames() const;          // order zuerst, Rest alphabetisch
    Layer *get(const QString &name);
    QString uniqueName(const QString &desired, const QString &original = {}) const;
    Layer &addLayer(const QString &desired, int rows = kDefaultRows,
                    int cols = kDefaultCols, const QString &app = {});
    QString renameLayer(const QString &oldName, const QString &desired);
    void removeLayer(const QString &name);

    QJsonObject toJson() const;
    static MacroConfig fromJson(const QJsonObject &data);
    static MacroConfig makeDefault();
};

MacroConfig load();
void save(const MacroConfig &config);

inline const QString kExportFormat = QStringLiteral("sshit-macros");

QJsonObject exportBundle(const MacroConfig &config, const QStringList &names = {});
void writeExport(const MacroConfig &config, const QString &path, const QStringList &names = {});

// Liest Layer aus einer Export- oder macros.json-Datei. Wirft std::runtime_error
// bei ungueltigem Inhalt.
QMap<QString, Layer> readImport(const QString &path);

// Leere Tastenbelegung mit Vorgabewerten.
QJsonObject newKey(const QString &actionType = QStringLiteral("execute"),
                   const QJsonValue &payload = QStringLiteral(""));

} // namespace ncssh::core::macros
