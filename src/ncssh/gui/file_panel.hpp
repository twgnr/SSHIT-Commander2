// Datei-Pane: Pfadzeile, Dateitabelle, Navigation und Datei-Operationen.
// Arbeitet gegen einen FileSystemProvider (lokal oder SFTP/sudo) — transparent.
// (Port von gui/file_panel.py; funktional zusammengefasst)
#pragma once

#include "ncssh/core/filesystem.hpp"
#include "ncssh/core/models.hpp"
#include "ncssh/gui/bridge.hpp"

#include <QWidget>
#include <memory>
#include <vector>

class QLineEdit;
class QTableWidget;
class QLabel;
class QComboBox;

namespace ncssh::gui {

class FilePanel : public QWidget {
    Q_OBJECT
public:
    explicit FilePanel(AsyncBridge *bridge, const QString &title, QWidget *parent = nullptr);

    // Provider setzen (Eigentum bleibt beim Aufrufer/Workspace).
    void setProvider(core::FileSystemProvider *provider, const QString &startPath = {});
    core::FileSystemProvider *provider() const { return m_provider; }

    QString currentPath() const { return m_path; }
    QString selectedPath() const;                 // markierte Datei (Vollpfad) oder ""
    std::vector<QString> selectedPaths() const;   // Mehrfachauswahl
    void setHeaderTitle(const QString &title);
    void refresh();
    void navigateTo(const QString &path);

signals:
    void activated();                             // Pane wurde fokussiert
    void pathChanged(const QString &path);
    void transferRequested(const QString &srcPath);  // F5 aus dieser Pane
    void statusMessage(const QString &msg);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void buildUi(const QString &title);
    void loadDir(const QString &path);
    void populate(const std::vector<core::FileEntry> &entries);
    void onDoubleClick(int row, int column);
    void goUp();
    void openContextMenu(const QPoint &pos);
    // Datei-Operationen (F-Tasten)
    void opView();
    void opEdit();
    void opMkdir();
    void opDelete();
    void opRename();
    void toggleHidden();

    AsyncBridge *m_bridge;
    core::FileSystemProvider *m_provider = nullptr;
    QString m_path;
    std::vector<core::FileEntry> m_entries;
    bool m_showHidden = true;

    QLabel *m_header = nullptr;
    QLineEdit *m_pathEdit = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_status = nullptr;
};

} // namespace ncssh::gui
