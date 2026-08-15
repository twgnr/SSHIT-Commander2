// Bestaetigungsdialoge: zeigen vor Kopier-/Verschiebe-/Umbenenn-/Loesch-Aktionen
// die betroffenen Pfade (Quelle -> Ziel) und fuehren die Aktion erst nach OK aus.
#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>
#include <functional>
#include <utility>
#include <vector>

class QLineEdit;
class QTableWidget;
class QPushButton;
class QLabel;

namespace ncssh::gui {

// Schnellsprung-Knopf mit Lesezeichen-Menue (auch vom DirChooser genutzt).
QPushButton *makeBookmarkButton(const QStringList &paths,
                                const std::function<void(const QString &)> &onPick,
                                QWidget *parent = nullptr);

using PathPair = std::pair<QString, QString>;

// Listet die betroffenen Pfade und fragt OK/Abbrechen.
class PathConfirmDialog : public QDialog {
    Q_OBJECT
public:
    struct Options {
        bool showTarget = true;      // false z.B. beim Loeschen (nur Quelle)
        QString confirmText;         // leer -> "Ausfuehren"
        QString sourceHeader;        // leer -> "Quelle"
        QString targetHeader;        // leer -> "Ziel"
    };

    PathConfirmDialog(const QString &title, const QString &intro,
                      const std::vector<PathPair> &pairs, const Options &options = {},
                      QWidget *parent = nullptr);

    // Bequemer One-Liner: zeigt den Dialog, true bei Bestaetigung.
    static bool confirm(const QString &title, const QString &intro,
                        const std::vector<PathPair> &pairs, const Options &options = {},
                        QWidget *parent = nullptr);
};

// Bestaetigt Kopieren/Verschieben mit waehlbarem Zielordner. Bei genau einem
// Objekt ist zusaetzlich der Ziel-Dateiname editierbar (Verschieben +
// Umbenennen in einem Schritt); die Ziel-Spalte aktualisiert sich live.
class TransferConfirmDialog : public QDialog {
    Q_OBJECT
public:
    // joiner(verzeichnis, name) -> zielpfad;  onBrowse(aktuelles_verzeichnis)
    // -> gewaehltes Verzeichnis (leer = abgebrochen).
    using Joiner = std::function<QString(const QString &, const QString &)>;
    using BrowseFn = std::function<QString(const QString &)>;

    TransferConfirmDialog(const QString &title, const QString &verb,
                          const QStringList &names, const QStringList &sourcePaths,
                          Joiner joiner, const QString &targetDir, BrowseFn onBrowse,
                          const QStringList &bookmarks = {}, QWidget *parent = nullptr);

    // Quelle -> Ziel-Paare (nach Accepted).
    std::vector<PathPair> results() const;
    QString targetDir() const;

private:
    void refresh();

    QStringList m_names;
    QStringList m_sources;
    Joiner m_joiner;
    BrowseFn m_onBrowse;
    bool m_single = false;

    QLineEdit *m_nameEdit = nullptr;   // nur bei genau einem Objekt
    QLineEdit *m_dirEdit = nullptr;
    QTableWidget *m_table = nullptr;
};

// Umbenennen und/oder Verschieben eines Eintrags. Namens- und Zielordner-Feld
// mit Live-Vorschau Quelle -> Ziel: rein umbenennen (Ordner unveraendert),
// rein verschieben (Name unveraendert) oder beides.
class RenameDialog : public QDialog {
    Q_OBJECT
public:
    using Joiner = std::function<QString(const QString &, const QString &)>;
    using BrowseFn = std::function<QString(const QString &)>;

    RenameDialog(const QString &title, const QString &origName, const QString &sourcePath,
                 const QString &targetDir, Joiner joiner, BrowseFn onBrowse,
                 const QStringList &bookmarks = {}, QWidget *parent = nullptr);

    // Zielpfad (leer, wenn kein Name eingegeben wurde).
    QString resultPath() const;

private:
    void refresh();

    Joiner m_joiner;
    BrowseFn m_onBrowse;
    QLineEdit *m_name = nullptr;
    QLineEdit *m_dir = nullptr;
    QLabel *m_targetLabel = nullptr;
};

} // namespace ncssh::gui
