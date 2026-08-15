// KI-Chat-Panel: erklaert Terminalausgaben/Dateien ueber ein lokales
// Ollama-Modell, mit Folgefragen. Rein beratend — fuehrt nichts aus.
#pragma once

#include "ncssh/gui/bridge.hpp"

#include <QDialog>
#include <QJsonArray>

class QTextBrowser;
class QLineEdit;
class QLabel;
class QPushButton;

namespace ncssh::gui {

class AiChatPanel : public QDialog {
    Q_OBJECT
public:
    // messages = vorbereitete Startnachrichten (core::ai::buildTerminalMessages o.ae.),
    // title = Fenstertitel, z.B. "KI — Terminalausgabe erklären".
    AiChatPanel(AsyncBridge *bridge, const QJsonArray &messages, const QString &title,
                QWidget *parent = nullptr);

private:
    void ask();                     // Folgefrage senden
    void streamAnswer();            // laufende Konversation an Ollama schicken
    void appendMarkdown(const QString &role, const QString &text);

    AsyncBridge *m_bridge;
    QJsonArray m_messages;
    QString m_pending;              // Antwort im Aufbau
    BridgeTask *m_task = nullptr;

    QTextBrowser *m_view = nullptr;
    QLineEdit *m_input = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_stopBtn = nullptr;
};

} // namespace ncssh::gui
