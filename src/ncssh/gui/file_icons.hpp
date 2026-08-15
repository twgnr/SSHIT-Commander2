// Liefert das zur Dateiendung gehoerende Programm-/Typ-Icon (Shell-Icon).
//
// QFileIconProvider liefert Typ-Icons nur fuer *existierende* Dateien. Damit
// das auch fuer Remote-Dateien (und ohne reale Datei) funktioniert, wird je
// Endung einmalig eine leere Probe-Datei angelegt und deren Icon
// zwischengespeichert.
#pragma once

#include <QHash>
#include <QIcon>
#include <QString>
#include <memory>

class QTemporaryDir;
class QFileIconProvider;

namespace ncssh::gui {

class FileIconCache {
public:
    FileIconCache();
    ~FileIconCache();

    QIcon forName(const QString &name);
    QIcon folder() const { return m_folder; }
    QIcon generic() const { return m_generic; }

private:
    QIcon probe(const QString &ext);

    std::unique_ptr<QFileIconProvider> m_provider;
    std::unique_ptr<QTemporaryDir> m_tmpDir;
    QHash<QString, QIcon> m_byExt;
    QIcon m_folder;
    QIcon m_generic;
};

// Gemeinsamer Icon-Cache (lazy; alle Panes teilen sich denselben).
FileIconCache &fileIcons();

} // namespace ncssh::gui
