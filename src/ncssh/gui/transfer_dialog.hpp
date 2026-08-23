// Transfer-Queue: laufende/fertige Uebertragungen mit Fortschritt, Geschwindigkeit
// und ETA; Abbrechen, Wiederholen, Liste aufraeumen.
#pragma once

#include <QDialog>

class QTableWidget;
class QPushButton;

namespace ncssh::gui {

class TransferManager;

class TransferDialog : public QDialog {
    Q_OBJECT
public:
    TransferDialog(TransferManager *manager, QWidget *parent = nullptr);

private:
    void rebuild();
    void updateRow(int jobId);
    int rowForJob(int jobId) const;
    // Knoepfe passend zum Zustand des markierten Auftrags freigeben.
    void updateButtons();

    TransferManager *m_manager;
    QTableWidget *m_table = nullptr;
    QPushButton *m_pauseBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;
    QPushButton *m_retryBtn = nullptr;
};

} // namespace ncssh::gui
