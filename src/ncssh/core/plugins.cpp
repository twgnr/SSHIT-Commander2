#include "ncssh/core/plugins.hpp"

#include "ncssh/config.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSet>
#include <stdexcept>

namespace ncssh::core::plugins {

const std::vector<std::pair<QString, QString>> &targetLabels()
{
    static const std::vector<std::pair<QString, QString>> labels = {
        {QStringLiteral("Datei und Ordner"), QStringLiteral("both")},
        {QStringLiteral("Nur Dateien"), QStringLiteral("file")},
        {QStringLiteral("Nur Ordner"), QStringLiteral("dir")},
    };
    return labels;
}

QString appBaseDir()
{
    // Im C++-Build ist das Hauptverzeichnis schlicht das Verzeichnis der EXE.
    return QCoreApplication::applicationDirPath();
}

QString pluginsDir()
{
    const QString d = appBaseDir() + QStringLiteral("/plugins");
    QDir().mkpath(d);
    return d;
}

static QString pluginsConfigFile()
{
    return ncssh::configDir() + QStringLiteral("/plugins.json");
}

QJsonObject Plugin::toJson() const
{
    return QJsonObject{
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), name},
        {QStringLiteral("exe"), exe},
        {QStringLiteral("args"), args},
        {QStringLiteral("working_dir"), workingDir},
        {QStringLiteral("context"), context},
        {QStringLiteral("targets"), targets},
    };
}

Plugin Plugin::fromJson(const QJsonObject &d)
{
    const auto s = [&](const QString &key, const QString &def = {}) -> QString {
        const QJsonValue v = d.value(key);
        return v.isNull() || v.isUndefined() ? def : v.toVariant().toString();
    };
    Plugin p;
    p.id = d.value(QStringLiteral("id")).toInt(0);
    p.name = s(QStringLiteral("name"));
    p.exe = s(QStringLiteral("exe"));
    p.args = s(QStringLiteral("args"));
    p.workingDir = s(QStringLiteral("working_dir"));
    p.context = d.value(QStringLiteral("context")).toBool(false);
    p.targets = s(QStringLiteral("targets"), QStringLiteral("both"));
    if (p.targets.isEmpty())
        p.targets = QStringLiteral("both");
    return p;
}

bool Plugin::matches(bool isDir) const
{
    if (!context)
        return false;
    if (targets == QLatin1String("file"))
        return !isDir;
    if (targets == QLatin1String("dir"))
        return isDir;
    return true;
}

QString bundledFile()
{
    return pluginsDir() + QStringLiteral("/plugins.json");
}

static std::vector<Plugin> readList(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QByteArray raw = f.readAll();
    if (raw.startsWith("\xEF\xBB\xBF"))  // utf-8-sig BOM tolerieren
        raw.remove(0, 3);
    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (!doc.isArray())
        return {};
    std::vector<Plugin> out;
    for (const QJsonValue &v : doc.array()) {
        if (v.isObject())
            out.push_back(Plugin::fromJson(v.toObject()));
    }
    return out;
}

std::vector<Plugin> load()
{
    return readList(pluginsConfigFile());
}

std::vector<Plugin> loadBundled()
{
    auto out = readList(bundledFile());
    for (auto &p : out)
        p.bundled = true;
    return out;
}

QString resolvePath(const QString &path)
{
    const QString p = path.trimmed();
    if (p.isEmpty())
        return {};
    if (QDir::isAbsolutePath(p))
        return p;
    return QDir::cleanPath(pluginsDir() + QLatin1Char('/') + p);
}

static QString normExe(const QString &exe)
{
    return QDir::toNativeSeparators(QDir::cleanPath(resolvePath(exe))).toLower();
}

std::vector<Plugin> loadAll()
{
    std::vector<Plugin> result = load();
    QSet<QString> userExes;
    for (const auto &p : result)
        userExes.insert(normExe(p.exe));
    const auto bundled = loadBundled();
    for (int i = 0; i < int(bundled.size()); ++i) {
        Plugin b = bundled[i];
        if (userExes.contains(normExe(b.exe)))
            continue;
        b.id = 1'000'000 + i;  // kollisionsfreier ID-Bereich
        result.push_back(b);
    }
    return result;
}

void save(const std::vector<Plugin> &plugins)
{
    QJsonArray arr;
    for (const auto &p : plugins)
        arr.append(p.toJson());
    QFile f(pluginsConfigFile());
    if (!f.open(QIODevice::WriteOnly))
        throw std::runtime_error("Kann plugins.json nicht schreiben.");
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
}

int nextId(const std::vector<Plugin> &plugins)
{
    int maxId = 0;
    for (const auto &p : plugins)
        maxId = qMax(maxId, p.id);
    return maxId + 1;
}

const Plugin *byId(const std::vector<Plugin> &plugins, int pid)
{
    for (const auto &p : plugins) {
        if (p.id == pid)
            return &p;
    }
    return nullptr;
}

// Einfaches Argument-Splitting (Anfuehrungszeichen respektieren) — Ersatz fuer
// shlex.split; ausreichend fuer nutzerkonfigurierte Parameter-Vorlagen.
static QStringList splitArgs(const QString &s)
{
    QStringList out;
    QString cur;
    bool inQuote = false;
    QChar quoteCh;
    bool have = false;
    for (int i = 0; i < s.size(); ++i) {
        const QChar c = s[i];
        if (inQuote) {
            if (c == quoteCh)
                inQuote = false;
            else
                cur += c;
        } else if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            inQuote = true;
            quoteCh = c;
            have = true;
        } else if (c.isSpace()) {
            if (have) {
                out << cur;
                cur.clear();
                have = false;
            }
        } else {
            cur += c;
            have = true;
        }
    }
    if (have)
        out << cur;
    return out;
}

void launch(const Plugin &plugin, const QString &path)
{
    if (plugin.exe.trimmed().isEmpty())
        throw std::runtime_error("Kein Programm angegeben.");
    const QString exe = resolvePath(plugin.exe);
    if (!QFileInfo(exe).isFile())
        throw std::runtime_error(("Programm nicht gefunden: " + exe).toStdString());

    QStringList args;
    const QString tmpl = plugin.args.trimmed();
    if (!tmpl.isEmpty()) {
        bool used = false;
        for (QString part : splitArgs(tmpl)) {
            if (part.contains(QLatin1String("{path}"))) {
                used = true;
                args << part.replace(QLatin1String("{path}"), path);
            } else {
                args << part;
            }
        }
        if (!path.isEmpty() && !used)
            args << path;
    } else if (!path.isEmpty()) {
        args << path;
    }

    const QString wd = plugin.workingDir.trimmed();
    const QString cwd = !wd.isEmpty() ? resolvePath(wd) : QFileInfo(exe).absolutePath();
    if (!QProcess::startDetached(exe, args, cwd))
        throw std::runtime_error(("Konnte Plugin nicht starten: " + exe).toStdString());
}

} // namespace ncssh::core::plugins
