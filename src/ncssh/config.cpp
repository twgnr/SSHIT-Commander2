#include "ncssh/config.hpp"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <stdexcept>

namespace ncssh {

QString configDir()
{
#ifdef Q_OS_WIN
    QString base = qEnvironmentVariable("APPDATA");
    if (base.isEmpty())
        base = QDir::homePath();
#elif defined(Q_OS_MACOS)
    const QString base = QDir::homePath() + QStringLiteral("/Library/Application Support");
#else
    QString base = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.config");
#endif
    const QString path = base + QStringLiteral("/ncssh");
    QDir().mkpath(path);
#ifndef Q_OS_WIN
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                    | QFileDevice::ExeOwner);
#endif
    return path;
}

void atomicWriteText(const QString &path, const QString &text)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        throw std::runtime_error(("Kann Datei nicht schreiben: " + path).toStdString());
    file.write(text.toUtf8());
    if (!file.commit())
        throw std::runtime_error(("Schreiben fehlgeschlagen: " + path).toStdString());
}

QString profilesFile()     { return configDir() + QStringLiteral("/servers.json"); }
QString historyFile()      { return configDir() + QStringLiteral("/history.json"); }
QString bookmarksFile()    { return configDir() + QStringLiteral("/bookmarks.json"); }
QString hostKeysFile()     { return configDir() + QStringLiteral("/host_keys.json"); }
QString tabFavoritesFile() { return configDir() + QStringLiteral("/tab_favorites.json"); }

} // namespace ncssh
