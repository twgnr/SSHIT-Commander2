#include "ncssh/core/history.hpp"

#include "ncssh/config.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <stdexcept>

namespace ncssh::core {

namespace {

QString jsonToString(const QJsonValue &v)
{
    return v.isString() ? v.toString() : v.toVariant().toString();
}

} // namespace

HistoryStore::HistoryStore()
{
    load();
}

// --- Persistenz ------------------------------------------------------------

void HistoryStore::load()
{
    const QString path = ncssh::historyFile();
    QFile f(path);
    if (!f.exists())
        return;
    if (!f.open(QIODevice::ReadOnly))
        throw std::runtime_error(
            (QStringLiteral("Kann Datei nicht lesen: ") + path).toStdString());
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    // Defekte Datei oder gueltiges JSON, aber kein Objekt -> ignorieren
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return;
    const QJsonObject data = doc.object();
    m_history.clear();
    for (const QJsonValue v : data.value(QStringLiteral("history")).toArray())
        m_history.append(jsonToString(v));
    m_favorites.clear();
    for (const QJsonValue v : data.value(QStringLiteral("favorites")).toArray())
        m_favorites.append(jsonToString(v));
}

void HistoryStore::save() const
{
    const QJsonObject data{
        {QStringLiteral("history"), QJsonArray::fromStringList(m_history)},
        {QStringLiteral("favorites"), QJsonArray::fromStringList(m_favorites)},
    };
    ncssh::atomicWriteText(
        ncssh::historyFile(),
        QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Indented)));
}

void HistoryStore::refresh()
{
    // Andere Instanzen (weitere Konsolen, Verlaufsdialog) koennen die Datei
    // seit unserem letzten Laden geaendert haben. Ohne Neuladen schrieb jede
    // Instanz ihren alten Schnappschuss zurueck: Favoriten/Leeren aus dem
    // Dialog gingen verloren, Befehle anderer Konsolen fielen heraus.
    try {
        load();
    } catch (const std::exception &) {
        // Datei gerade nicht lesbar -> mit dem Speicherstand weiterarbeiten.
    }
}

// --- Historie --------------------------------------------------------------

void HistoryStore::add(const QString &command)
{
    const QString cmd = command.trimmed();
    if (cmd.isEmpty())
        return;
    refresh();
    if (!m_history.isEmpty() && m_history.last() == cmd)
        return;  // keine direkten Dubletten
    m_history.append(cmd);
    if (m_history.size() > MAX)
        m_history = m_history.mid(m_history.size() - MAX);
    save();
}

void HistoryStore::clearHistory()
{
    refresh();  // Favoriten anderer Instanzen erhalten
    m_history.clear();
    save();
}

// --- Favoriten -------------------------------------------------------------

void HistoryStore::addFavorite(const QString &command)
{
    const QString cmd = command.trimmed();
    if (cmd.isEmpty())
        return;
    refresh();
    if (!m_favorites.contains(cmd)) {
        m_favorites.append(cmd);
        save();
    }
}

void HistoryStore::removeFavorite(const QString &command)
{
    refresh();
    if (m_favorites.removeOne(command))
        save();
}

bool HistoryStore::isFavorite(const QString &command) const
{
    return m_favorites.contains(command);
}

} // namespace ncssh::core
