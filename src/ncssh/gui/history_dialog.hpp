// Befehlsverlauf & Favoriten: Befehle aus der Historie als Favorit speichern
// und per Doppelklick in die aktive Konsole einfuegen.
#pragma once

#include "ncssh/core/history.hpp"

#include <QDialog>

class QListWidget;

namespace ncssh::gui {

class HistoryDialog : public QDialog {
    Q_OBJECT
public:
    explicit HistoryDialog(QWidget *parent = nullptr);

    // Der gewaehlte Befehl (nach Accepted).
    QString command() const { return m_command; }

private:
    void reload();
    QListWidget *activeList() const;

    core::HistoryStore m_store;
    QString m_command;
    QListWidget *m_history = nullptr;
    QListWidget *m_favorites = nullptr;
};

} // namespace ncssh::gui
