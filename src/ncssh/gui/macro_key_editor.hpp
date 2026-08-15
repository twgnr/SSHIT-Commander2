// Editor fuer eine einzelne Makro-Taste: Beschriftung, Icon, Schrift, Kuerzel
// und die Aktion samt typabhaengigem Payload-Editor.
#pragma once

#include "ncssh/core/macroactions.hpp"

#include <QDialog>
#include <QJsonObject>
#include <QStringList>

class QLineEdit;
class QComboBox;
class QPlainTextEdit;
class QSpinBox;
class QStackedWidget;
class QListWidget;
class QPushButton;
class QLabel;
class QKeySequenceEdit;
class QCheckBox;

namespace ncssh::gui {

// Typabhaengiger Payload-Editor (text | number | none | layer | window |
// sequence | ssh | json).
class PayloadEditor : public QWidget {
    Q_OBJECT
public:
    explicit PayloadEditor(const QStringList &layerNames, QWidget *parent = nullptr);

    // Schaltet auf den zum Aktionstyp passenden Editor um.
    void setAction(const QString &actionType);
    // Laedt einen vorhandenen Payload in den Editor.
    void load(const QString &actionType, const QJsonValue &payload);
    // Liest den aktuellen Payload passend zum Aktionstyp aus.
    QJsonValue value(const QString &actionType) const;

private:
    void seqRefresh();
    void seqAdd();
    void seqEdit();
    void seqRemove();
    void seqMove(int delta);

    QStringList m_layerNames;
    QStackedWidget *m_stack = nullptr;

    QLineEdit *m_text = nullptr;
    QSpinBox *m_number = nullptr;
    QLabel *m_none = nullptr;
    QComboBox *m_layer = nullptr;
    QComboBox *m_windowCommand = nullptr;
    QLineEdit *m_windowTitle = nullptr;
    QListWidget *m_sequence = nullptr;
    std::vector<QJsonObject> m_steps;   // Schritte fuer multi_action/sequence
    QLineEdit *m_sshCommand = nullptr;
    QCheckBox *m_sshRun = nullptr;
    QPlainTextEdit *m_json = nullptr;
};

class MacroKeyEditor : public QDialog {
    Q_OBJECT
public:
    // config = bestehende Tastenbelegung (leer = neue Taste).
    MacroKeyEditor(const QJsonObject &config, const QStringList &layerNames,
                   QWidget *parent = nullptr);

    QJsonObject config() const { return m_result; }
    bool cleared() const { return m_cleared; }   // Taste leeren gewaehlt

private:
    void onActionChanged();
    void pickIcon();
    void pickColor();
    void accept() override;

    QJsonObject m_result;
    bool m_cleared = false;
    QStringList m_layerNames;

    QLineEdit *m_label = nullptr;
    QComboBox *m_labelPos = nullptr;
    QLineEdit *m_icon = nullptr;
    QPushButton *m_color = nullptr;
    QLineEdit *m_fontFamily = nullptr;
    QKeySequenceEdit *m_shortcut = nullptr;
    QComboBox *m_action = nullptr;
    QLabel *m_actionHint = nullptr;
    PayloadEditor *m_payload = nullptr;
};

} // namespace ncssh::gui
