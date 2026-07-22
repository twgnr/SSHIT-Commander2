#include "ncssh/gui/file_icons.hpp"

#include <QFile>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QTemporaryDir>

namespace ncssh::gui {

FileIconCache::FileIconCache()
    : m_provider(std::make_unique<QFileIconProvider>())
{
    m_folder = m_provider->icon(QFileIconProvider::Folder);
    m_generic = m_provider->icon(QFileIconProvider::File);
}

FileIconCache::~FileIconCache() = default;

QIcon FileIconCache::forName(const QString &name)
{
    const QString ext = QFileInfo(name).suffix().toLower();
    if (ext.isEmpty())
        return m_generic;
    auto it = m_byExt.find(ext);
    if (it != m_byExt.end())
        return it.value();

    QIcon icon = probe(ext);
    if (icon.isNull())
        icon = m_generic;
    m_byExt.insert(ext, icon);
    return icon;
}

QIcon FileIconCache::probe(const QString &ext)
{
    // Leere Probe-Datei mit der Endung anlegen; das Shell-Icon haengt nur an ihr.
    if (!m_tmpDir) {
        m_tmpDir = std::make_unique<QTemporaryDir>();
        if (!m_tmpDir->isValid())
            return {};
    }
    const QString path = m_tmpDir->filePath(QStringLiteral("probe.") + ext);
    if (!QFile::exists(path)) {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return {};
        f.close();
    }
    return m_provider->icon(QFileInfo(path));
}

FileIconCache &fileIcons()
{
    static FileIconCache cache;
    return cache;
}

} // namespace ncssh::gui
