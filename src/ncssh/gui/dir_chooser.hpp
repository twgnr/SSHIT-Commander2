// Ordner-Auswahl ueber die Provider-Abstraktion — funktioniert auch remote (SFTP).
//
// Wird genutzt, wenn das Ziel eines Kopier-/Verschiebevorgangs auf einem
// entfernten Server liegt, wo der native QFileDialog nicht browsen kann.
// (Port von gui/dir_chooser.py)
#pragma once

#include "ncssh/core/filesystem.hpp"
#include "ncssh/gui/bridge.hpp"

#include <QDialog>
#include <QStringList>

class QListWidget;
class QListWidgetItem;
class QLabel;

namespace ncssh::gui {

class DirChooserDialog : public QDialog {
    Q_OBJECT
public:
    DirChooserDialog(AsyncBridge *bridge, core::FileSystemProvider *provider,
                     const QString &startPath, const QStringList &bookmarks = {},
                     QWidget *parent = nullptr);

    // Gewaehltes Verzeichnis (nach Accepted).
    QString chosen() const { return m_chosen; }

private:
    void load();
    void enter(QListWidgetItem *item);
    void gotoPath(const QString &path);

    AsyncBridge *m_bridge;
    core::FileSystemProvider *m_provider;
    QString m_path;
    QString m_chosen;

    QLabel *m_pathLabel = nullptr;
    QListWidget *m_list = nullptr;
};

} // namespace ncssh::gui
