// Pfad-Lesezeichen pro Server verwalten: anspringen, entfernen, exportieren
// und importieren.
#pragma once

#include "ncssh/core/bookmarks.hpp"

#include <QDialog>

class QListWidget;

namespace ncssh::gui {

class BookmarksDialog : public QDialog {
    Q_OBJECT
public:
    // key = Profilname bzw. "local" (Gruppe der Lesezeichen).
    BookmarksDialog(core::BookmarkStore *store, const QString &key,
                    QWidget *parent = nullptr);

    // Der zum Anspringen gewaehlte Pfad (nach Accepted).
    QString chosenPath() const { return m_chosenPath; }

private:
    void reload();
    void removeSelected();
    void exportBookmarks();
    void importBookmarks();

    core::BookmarkStore *m_store;
    QString m_key;
    QString m_chosenPath;
    QListWidget *m_list = nullptr;
};

} // namespace ncssh::gui
