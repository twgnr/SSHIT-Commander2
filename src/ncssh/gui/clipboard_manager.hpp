// Clipboard-Manager: Historie aller kopierten Texte; ein Eintrag kann als
// "aktiv" gesetzt werden (Strg+V fuegt dann genau diesen ein).
// (Port von gui/clipboard_manager.py + clipboard_dialog.py)
#pragma once

#include <QDateTime>
#include <QDialog>
#include <QObject>
#include <QStringList>
#include <vector>

class QTableWidget;
class QLabel;

namespace ncssh::gui {

// Ein Eintrag der Historie: Text ODER Dateien (aus dem Explorer / einer Pane).
struct ClipEntry {
    int id = 0;
    QString kind = QStringLiteral("text");   // "text" | "file"
    QDateTime timestamp;
    bool active = false;
    QString text;                            // kind == "text"
    QStringList files;                       // kind == "file"

    // Einzeilige Beschreibung fuer die Tabelle.
    QString preview() const;
};

// Beobachtet die Zwischenablage und fuehrt die Historie.
class ClipboardManager : public QObject {
    Q_OBJECT
public:
    explicit ClipboardManager(QObject *parent = nullptr);

    const std::vector<ClipEntry> &entries() const { return m_entries; }
    void clear();
    void removeAt(int index);
    // Setzt den Eintrag als aktiven Inhalt der Zwischenablage.
    void activate(int index);

signals:
    void changed();

private:
    void onClipboardChanged();

    std::vector<ClipEntry> m_entries;
    QString m_lastSeen;
    int m_counter = 0;
    static constexpr int kMaxEntries = 100;
};

class ClipboardDialog : public QDialog {
    Q_OBJECT
public:
    ClipboardDialog(ClipboardManager *manager, QWidget *parent = nullptr);

    // Der zum Einfuegen gewaehlte Text (nach Accepted); bei Datei-Eintraegen
    // die Pfade zeilenweise.
    QString chosenText() const { return m_chosenText; }

private:
    void reload();

    ClipboardManager *m_manager;
    QString m_chosenText;
    QTableWidget *m_table = nullptr;
    QLabel *m_status = nullptr;
};

} // namespace ncssh::gui
