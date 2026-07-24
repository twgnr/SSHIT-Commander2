// Geplante/skriptbare Aufgaben: ein SFTP-Batch-Skript gegen die aktive Sitzung
// ausfuehren, optional in festem Intervall wiederholen.
#pragma once

#include "ncssh/gui/bridge.hpp"
#include "ncssh/net/ssh.hpp"

#include <QDialog>

class QPlainTextEdit;
class QCheckBox;
class QSpinBox;
class QTimer;
class QLabel;
class QPushButton;

namespace ncssh::gui {

class SftpBatchDialog : public QDialog {
    Q_OBJECT
public:
    SftpBatchDialog(AsyncBridge *bridge, net::SSHSessionPtr session, QWidget *parent = nullptr);

private:
    void runBatch();
    void stopBatch();
    void loadScript();
    void saveScript();
    void appendLog(const QString &line);
    void updateButtons();
    void onScheduleToggled(bool on);

    AsyncBridge *m_bridge;
    net::SSHSessionPtr m_session;  // haelt die Sitzung waehrend des Laufs am Leben

    QPlainTextEdit *m_editor = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QCheckBox *m_stopOnError = nullptr;
    QCheckBox *m_schedule = nullptr;
    QSpinBox *m_interval = nullptr;
    QTimer *m_timer = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_runBtn = nullptr;
    QPushButton *m_stopBtn = nullptr;

    BridgeTask *m_task = nullptr;
    bool m_running = false;
};

} // namespace ncssh::gui
