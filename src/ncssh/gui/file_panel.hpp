// Datei-Pane: Pfadzeile, Dateitabelle, Navigation und Datei-Operationen.
// Arbeitet gegen einen FileSystemProvider (lokal oder SFTP/sudo) — transparent.
// (Port von gui/file_panel.py; funktional zusammengefasst)
#pragma once

#include "ncssh/core/bookmarks.hpp"
#include "ncssh/core/filesystem.hpp"
#include "ncssh/core/models.hpp"
#include "ncssh/gui/bridge.hpp"

#include <functional>
#include <QHash>
#include <QKeySequence>
#include <QSet>
#include <QStringList>
#include <QWidget>
#include <memory>
#include <vector>

class QLineEdit;
class QTableWidget;
class QLabel;
class QComboBox;
class QPushButton;
class QMenu;
class QTimer;
class QScrollArea;
class QHBoxLayout;
class QStackedWidget;
class QListView;
class QAbstractItemView;

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
    // Trennen-Chip im Header ein-/ausblenden (verbundene Remote-Seite).
    void setConnected(bool connected);
    bool sudoActive() const { return m_sudoActive; }
    // Chip-Zustand setzen, ohne sudoToggled auszuloesen — fuer den Fall, dass
    // das Umschalten fehlschlaegt und der Haken zurueck muss.
    void setSudoActive(bool active);

    QString currentPath() const { return m_path; }
    QString selectedPath() const;                 // markierte Datei (Vollpfad) oder ""
    std::vector<QString> selectedPaths() const;   // Mehrfachauswahl
    // Markierter Eintrag oder nullptr (auch bei ".."); zeigt in m_rows.
    const core::FileEntry *selectedEntry() const;
    void setHeaderTitle(const QString &title);
    void refresh();
    void navigateTo(const QString &path);
    // Konfigurierte Kuerzel der Datei-Operationen uebernehmen (view/edit/…).
    void applyShortcuts();
    // Fuehrt die Datei-Operation zur Kuerzel-ID aus (view/edit/copy/…).
    // Public, damit die Haupt-Toolbar auf die aktive Pane wirken kann.
    void triggerOp(const QString &id);

    // --- Verlauf (Alt+Links / Alt+Rechts) ---
    void goBack();
    void goForward();
    bool canGoBack() const { return m_histPos > 0; }
    bool canGoForward() const { return m_histPos >= 0 && m_histPos < m_history.size() - 1; }

    // --- Markieren ---
    void markByPattern(bool select);   // Num + / Num -
    void invertMarks();                // Num *
    void selectAllMarks();             // Strg+A
    void clearMarks();

    // Angezeigte Zusatzspalten (ohne "Name") — aus den Einstellungen.
    QStringList visibleColumns() const;

    // App-weite Zwischenablage (Strg+C/X) — der Workspace holt sie sich beim
    // Einfuegen ab, weil nur er beide Panes kennt.
    static core::FileSystemProvider *clipboardProvider() { return s_clipProvider; }
    static QStringList clipboardPaths() { return s_clipPaths; }
    static bool clipboardIsMove() { return s_clipMove; }

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
    // Verschieben in die andere Pane (wie transferRequested, aber mit Loeschen).
    void moveRequested(const QString &srcPath);
    // Einfuegen aus der internen Zwischenablage; move = ausschneiden.
    void pasteRequested(bool move);
    // Verzeichnis-Vergleich mit der anderen Pane oeffnen.
    void dirDiffRequested();
    // Alarm-Trigger fuer das markierte Verzeichnis setzen.
    void dirAlarmRequested(const QString &path);
    // Trennen-Chip im Pane-Header geklickt.
    void disconnectRequested();
    // --- Netzwerk-Modus (net://) ---
    void connectToHostRequested(const QString &host);  // SSH zu diesem Host
    void rescanRequested();                            // Scanner erneut starten
    void exitNetworkModeRequested();                   // zurueck zum Dateisystem

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void buildUi(const QString &title);
    void loadDir(const QString &path, bool record = true);
    void populate(const std::vector<core::FileEntry> &entries);
    void onDoubleClick(int row, int column);
    void goUp();
    void openContextMenu(const QPoint &pos);
    // True, solange die Pane die Host-Liste des Scanners zeigt.
    bool hostMode() const;
    void openHostMenu(const QPoint &pos);
    // Datei-Operationen (F-Tasten)
    void opView();
    void opEdit();
    void opMkdir();
    void opNewFile();
    void opDelete();
    void opRename();
    void opProperties();
    void toggleHidden();
    void toggleBookmark();
    void openBookmarks();
    void updateBookmarkButton();
    // Statuszeile: Verzeichnis-Zusammenfassung plus aktuelle Auswahl.
    void updateSelectionStatus();
    void sortBy(int column);          // Spaltenkopf angeklickt
    void applyFilter(const QString &pattern);

    // --- Spalten (frei waehlbar, in core/settings gespeichert) ---
    static QStringList optionalColumns();          // kanonische Reihenfolge
    static QString columnLabel(const QString &id);
    void setTableHeaders();
    void showHeaderMenu(const QPoint &pos);
    void toggleColumn(const QString &id, bool on);
    void applyColumnWidths();
    QString columnValue(const QString &id, const core::FileEntry &entry) const;

    // --- Breadcrumb-Pfadleiste ---
    std::vector<std::pair<QString, QString>> breadcrumbParts() const;  // (Beschriftung, Pfad)
    void buildBreadcrumb();
    // Laufwerksauswahl fuellen (nur lokal sichtbar).
    void updateDriveCombo();
    void beginPathEdit();             // Breadcrumb -> Eingabefeld
    void endPathEdit();

    // --- Ansicht: Detail (Tabelle) oder Kachel ---
    QAbstractItemView *activeView() const;
    void setViewMode(bool grid);

    // --- Miniaturansichten (nur lokal, abschaltbar) ---
    bool thumbsEnabled() const;
    std::pair<int, int> visibleRows(int buffer = 8) const;   // [erste, letzte)
    void loadVisibleThumbs();

    // --- Kontextmenue-Aktionen ---
    void opExecute();                            // mit dem Standardprogramm oeffnen
    void openWithProgram(const QString &exe);    // mit einem bestimmten Programm
    void openWithChooser();                      // "Oeffnen mit"-Dialog (Windows)
    void addOpenWithMenu(QMenu *menu, bool isFile, const core::FileEntry *entry);
    void opChecksum();
    void opMakeZip();
    void opExtract();
    void copyPathToClipboard();
    void clipCopy();
    void clipCut();
    void clipPaste();
    // Fuehrt fn mit einem lokalen Pfad aus; Remote-Dateien werden vorher in einen
    // temporaeren Ordner geholt.
    void withLocalCopy(const std::function<void(const QString &)> &fn);

    // --- Markieren / Tippsuche (Hilfen) ---
    std::vector<int> markableRows() const;
    void applySelection(const std::vector<int> &rows, bool select);
    void markCurrent(bool select, bool toggle);  // Einfg / Leertaste
    void typeAhead(const QString &ch);
    void selectMatch(const QString &query);

    AsyncBridge *m_bridge;
    core::FileSystemProvider *m_provider = nullptr;
    QString m_path;
    std::vector<core::FileEntry> m_entries;      // Rohdaten des Verzeichnisses
    std::vector<core::FileEntry> m_rows;         // sichtbare Zeilen (Index == Tabellenzeile)
    bool m_showHidden = true;
    QString m_filter;                 // Wildcard-Filter (Strg+F)
    QStringList m_fileCols;           // angezeigte Spalten; [0] ist immer "name"
    QString m_sortKey = QStringLiteral("name");   // Spalten-Kennung, nach der sortiert wird
    bool m_sortAscending = true;
    bool m_sudoAvailable = false;
    bool m_sudoActive = false;

    QHash<QString, QKeySequence> m_opShortcuts;   // konfigurierte Datei-Op-Kuerzel
    QStringList m_history;            // besuchte Pfade
    qsizetype m_histPos = -1;         // Position im Verlauf
    QString m_typeAheadBuffer;        // getippte Zeichen (Sprungsuche)
    QTimer *m_typeAheadTimer = nullptr;

    // App-weite Zwischenablage — funktioniert auch fuer Remote-Pfade und kennt
    // den Unterschied zwischen Kopieren und Ausschneiden. (Original: Klassen-
    // attribute von FilePanel.)
    static core::FileSystemProvider *s_clipProvider;
    static QStringList s_clipPaths;
    static bool s_clipMove;

    core::BookmarkStore m_bookmarks;
    QString m_bookmarkKey = QStringLiteral("local");

    QLabel *m_header = nullptr;
    QScrollArea *m_crumbScroll = nullptr;
    QHBoxLayout *m_crumbLayout = nullptr;
    QComboBox *m_driveCombo = nullptr;
    QLineEdit *m_pathEdit = nullptr;
    QTableWidget *m_table = nullptr;
    QStackedWidget *m_viewStack = nullptr;
    QListView *m_grid = nullptr;      // teilt Model UND Auswahl mit m_table
    bool m_gridMode = false;
    QTimer *m_thumbTimer = nullptr;   // Nachladen beim Scrollen entprellen
    quint64 m_thumbToken = 0;         // verwirft Ergebnisse alter Verzeichnisse
    QSet<QString> m_thumbRequested;
    QLabel *m_status = nullptr;
    QString m_baseStatus;             // Zusammenfassung ohne Auswahl-Teil
    QPushButton *m_starButton = nullptr;
    QPushButton *m_sudoChip = nullptr;
    QPushButton *m_disconnectChip = nullptr;
    QLineEdit *m_filterEdit = nullptr;
};

} // namespace ncssh::gui
