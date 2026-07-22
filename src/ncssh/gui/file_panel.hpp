// Datei-Pane: Pfadzeile, Dateitabelle, Navigation und Datei-Operationen.
// Arbeitet gegen einen FileSystemProvider (lokal oder SFTP/sudo) — transparent.
// (Port von gui/file_panel.py; funktional zusammengefasst)
#pragma once

#include "ncssh/core/bookmarks.hpp"
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
class QPushButton;

namespace ncssh::gui {

class FilePanel : public QWidget {
    Q_OBJECT
public:
    explicit FilePanel(AsyncBridge *bridge, const QString &title, QWidget *parent = nullptr);

    // Provider setzen (Eigentum bleibt beim Aufrufer/Workspace).
    void setProvider(core::FileSystemProvider *provider, const QString &startPath = {});
    core::FileSystemProvider *provider() const { return m_provider; }

    // Lesezeichen-Gruppe dieser Pane (Profilname bzw. "local").
    void setBookmarkKey(const QString &key);

    // sudo-Modus (nur remote/Linux): schaltet auf das sudo-Dateisystem um.
    // Der Workspace stellt den Provider bereit; hier wird nur der Chip gefuehrt.
    void setSudoAvailable(bool available);
    bool sudoActive() const { return m_sudoActive; }

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
    void sudoToggled(bool on);                    // sudo-Chip umgeschaltet
    // Auswahl geaendert (fuer das Vorschau-Panel); leer = nichts markiert.
    void selectionChanged(const QString &path);
    // Drop aus der anderen Pane bzw. aus dem Explorer (lokale Pfade).
    void filesDropped(const QStringList &paths, bool fromExplorer);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

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
    void opProperties();
    void toggleHidden();
    void toggleBookmark();
    void openBookmarks();
    void updateBookmarkButton();
    void sortBy(int column);          // Spaltenkopf angeklickt
    void applyFilter(const QString &pattern);

    AsyncBridge *m_bridge;
    core::FileSystemProvider *m_provider = nullptr;
    QString m_path;
    std::vector<core::FileEntry> m_entries;
    bool m_showHidden = true;
    QString m_filter;                 // Wildcard-Filter (Strg+F)
    int m_sortColumn = 0;             // 0 Name · 1 Groesse · 2 Datum · 3 Rechte
    bool m_sortAscending = true;
    bool m_sudoAvailable = false;
    bool m_sudoActive = false;

    core::BookmarkStore m_bookmarks;
    QString m_bookmarkKey = QStringLiteral("local");

    QLabel *m_header = nullptr;
    QLineEdit *m_pathEdit = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_starButton = nullptr;
    QPushButton *m_sudoChip = nullptr;
    QLineEdit *m_filterEdit = nullptr;
};

} // namespace ncssh::gui
