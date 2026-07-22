// Einstellungs-Dialog: Allgemein (Sprache, Theme, Schriftgroessen, Datumsformat,
// versteckte Dateien, Startpfad), KI (Ollama) und Tastenkuerzel.
// (Port von gui/settings_dialog.py)
#pragma once

#include <QDialog>
#include <QHash>

class QComboBox;
class QLineEdit;
class QCheckBox;
class QSpinBox;
class QTableWidget;
class QLabel;

namespace ncssh::gui {

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

private:
    QWidget *buildGeneralTab();
    QWidget *buildAiTab();
    QWidget *buildShortcutsTab();
    void save();
    void testOllama();
    void loadOllamaModels();

    // Allgemein
    QComboBox *m_language = nullptr;
    QComboBox *m_theme = nullptr;
    QSpinBox *m_editorFont = nullptr;
    QSpinBox *m_terminalFont = nullptr;
    QSpinBox *m_paneFont = nullptr;
    QLineEdit *m_dateFormat = nullptr;
    QCheckBox *m_hideHidden = nullptr;
    QCheckBox *m_showIcons = nullptr;
    QCheckBox *m_restoreTabs = nullptr;
    QLineEdit *m_startPath = nullptr;

    // KI
    QCheckBox *m_aiEnabled = nullptr;
    QLineEdit *m_aiUrl = nullptr;
    QComboBox *m_aiModel = nullptr;
    QLabel *m_aiStatus = nullptr;

    // Tastenkuerzel
    QTableWidget *m_shortcuts = nullptr;
};

} // namespace ncssh::gui
