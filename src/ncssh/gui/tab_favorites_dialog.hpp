// Tab-Favoriten: ganze Tab-Konstellationen (Verbindung + Pane-Pfade) sichern
// und wiederherstellen.
#pragma once

#include "ncssh/core/tabfavorites.hpp"

#include <QDialog>
#include <QJsonObject>
#include <vector>

class QListWidget;
class QLabel;

namespace ncssh::gui {

class TabFavoritesDialog : public QDialog {
    Q_OBJECT
public:
    // currentTabs = Beschreibung der aktuell offenen Tabs (zum Sichern).
    TabFavoritesDialog(const std::vector<QJsonObject> &currentTabs, QWidget *parent = nullptr);

    // Die zum Wiederherstellen gewaehlte Tab-Liste (nach Accepted).
    std::vector<QJsonObject> chosenTabs() const { return m_chosen; }

private:
    void reload();
    void saveCurrent();
    void removeSelected();
    void renameSelected();

    core::TabFavoritesStore m_store;
    std::vector<QJsonObject> m_currentTabs;
    std::vector<QJsonObject> m_chosen;

    QListWidget *m_list = nullptr;
    QLabel *m_status = nullptr;
};

} // namespace ncssh::gui
