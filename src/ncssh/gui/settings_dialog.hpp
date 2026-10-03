// Einstellungs-Dialog: Allgemein (Sprache, Theme, Schriftgroessen, Datumsformat,
// versteckte Dateien, Startpfad), KI (Ollama) und Tastenkuerzel.
#pragma once

#include "ncssh/gui/bridge.hpp"

#include <QDialog>
#include <QHash>

class QFormLayout;
class QComboBox;
class QLineEdit;
class QCheckBox;
class QSpinBox;
class QTableWidget;
class QLabel;
class QProgressBar;
class QPushButton;

namespace ncssh::gui {

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    // bridge darf null sein — dann ist der Modell-Download deaktiviert.
    explicit SettingsDialog(QWidget *parent = nullptr, AsyncBridge *bridge = nullptr);
    ~SettingsDialog() override;

private:
    QWidget *buildGeneralTab();
    QWidget *buildAiTab();
    QWidget *buildShortcutsTab();
    void save();
    void testOllama();
    void loadOllamaModels();
    // KI-Anbieter: Eingaben des bisherigen Anbieters merken, neuen anzeigen.
    void stashAiProvider();
    void showAiProvider(const QString &provider);
    void testAiConnection();          // je Anbieter: Ollama-Version bzw. Modellliste
    void loadAiModels();
    void startPull();                 // Modell ueber Ollama herunterladen
    void updateExecColorButton();
    void pickExecColor();
    void exportConfig();
    void importConfig();
    void deleteTheme();
    void reloadThemes();
    // Fragt ab, welche Bereiche eines Buendels uebernommen werden sollen.
    QStringList chooseImportSections(const QStringList &available);

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
    QCheckBox *m_confirmCopy = nullptr;
    QCheckBox *m_confirmDelete = nullptr;
    QCheckBox *m_naturalSort = nullptr;
    QCheckBox *m_thumbnails = nullptr;
    QCheckBox *m_execHighlight = nullptr;
    QCheckBox *m_autoConnect = nullptr;
    QCheckBox *m_compactRows = nullptr;
    QPushButton *m_execColorBtn = nullptr;
    QString m_execColor;
    QLineEdit *m_startPath = nullptr;

    // KI
    QCheckBox *m_aiEnabled = nullptr;
    QComboBox *m_aiProvider = nullptr;
    QFormLayout *m_aiForm = nullptr;
    QLineEdit *m_aiKey = nullptr;
    QLabel *m_aiPrivacy = nullptr;
    QPushButton *m_aiConsentReset = nullptr;
    QWidget *m_pullRowWidget = nullptr;
    QString m_aiShownProvider;
    // Eingaben je Anbieter (bis zum Speichern nur im Dialog).
    QHash<QString, QString> m_aiUrls;
    QHash<QString, QString> m_aiModels;
    QHash<QString, QString> m_aiKeys;
    QHash<QString, QString> m_aiKeysLoaded;   // nur Geaendertes in den Schluesselbund
    QLineEdit *m_aiUrl = nullptr;
    QComboBox *m_aiModel = nullptr;
    QComboBox *m_pullModel = nullptr;
    QProgressBar *m_pullProgress = nullptr;
    QPushButton *m_pullButton = nullptr;
    QLabel *m_aiStatus = nullptr;
    BridgeTask *m_pullTask = nullptr;

    // Tastenkuerzel
    QTableWidget *m_shortcuts = nullptr;

    AsyncBridge *m_bridge = nullptr;
};

} // namespace ncssh::gui
