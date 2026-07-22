// Netzwerk-Scanner: Ziele/Ports waehlen, Hosts live einlaufen lassen
// (IP, Name, MAC/Hersteller, offene Ports, Freigaben, Weboberflaeche).
// (Port von gui/netscan_dialog.py)
#pragma once

#include "ncssh/core/netscan.hpp"
#include "ncssh/gui/bridge.hpp"

#include <QDialog>
#include <vector>

class QLineEdit;
class QComboBox;
class QCheckBox;
class QTableWidget;
class QProgressBar;
class QLabel;
class QPushButton;
class QSpinBox;
class QDoubleSpinBox;
class QTimer;

namespace ncssh::gui {

class NetscanDialog : public QDialog {
    Q_OBJECT
public:
    explicit NetscanDialog(AsyncBridge *bridge, QWidget *parent = nullptr);

    // Gefundene Hosts (fuer den Netzwerk-Modus einer Pane).
    const std::vector<core::HostResult> &hosts() const { return m_hosts; }
    // "left" | "right" — wohin die Ergebnisse uebernommen werden sollen.
    QString targetPane() const;

private:
    void startScan();
    void stopScan();
    void addHostRow(const core::HostResult &host);
    void loadLastScan();       // Ergebnis des letzten Laufs wiederherstellen
    void saveLastScan() const;

    AsyncBridge *m_bridge;
    std::vector<core::HostResult> m_hosts;
    BridgeTask *m_task = nullptr;

    QLineEdit *m_targets = nullptr;
    QComboBox *m_portPreset = nullptr;
    QLineEdit *m_customPorts = nullptr;
    QCheckBox *m_ping = nullptr;
    QCheckBox *m_onlyAlive = nullptr;
    QCheckBox *m_resolve = nullptr;
    QCheckBox *m_shares = nullptr;
    QCheckBox *m_identify = nullptr;
    QCheckBox *m_detectMac = nullptr;
    QSpinBox *m_concurrency = nullptr;
    QDoubleSpinBox *m_timeout = nullptr;
    QSpinBox *m_autoRescan = nullptr;
    QTimer *m_rescanTimer = nullptr;
    QComboBox *m_targetPane = nullptr;
    QTableWidget *m_table = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_startBtn = nullptr;
};

} // namespace ncssh::gui
