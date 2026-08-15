// Transfer-Queue: laufende/fertige Uebertragungen mit Fortschritt, Geschwindigkeit
// und ETA; Abbrechen, Wiederholen, Liste aufraeumen.
#pragma once

#include <QDialog>

class QTableWidget;

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

    TransferManager *m_manager;
    QTableWidget *m_table = nullptr;
};

} // namespace ncssh::gui
