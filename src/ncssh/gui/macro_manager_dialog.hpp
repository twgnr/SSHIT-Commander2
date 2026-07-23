// Makro-Manager: Raster frei belegbarer Tasten in mehreren Layern.
// Zwei Modi: Bearbeiten (Klick oeffnet den Tasten-Editor) und Ausfuehren
// (Klick loest die Aktion aus). Optional wechselt der Layer automatisch zum
// zuletzt aktiven Programm.  (Port von gui/macro_manager_dialog.py)
#pragma once

#include "ncssh/core/macroactions.hpp"
#include "ncssh/core/macros.hpp"
#include "ncssh/gui/bridge.hpp"

#include <QDialog>
#include <QJsonObject>
#include <QPushButton>
#include <functional>
#include <vector>

class QGridLayout;
class QListWidget;
class QLabel;
class QComboBox;
class QCheckBox;
class QTimer;
class QSpinBox;
class QVBoxLayout;
class QDockWidget;
class QCloseEvent;

namespace ncssh::gui {

// Eine Taste im Raster: eigenes Zeichnen (Icon + Beschriftung) und
// Unterscheidung zwischen kurzem Klick und langem Halten.
class KeyTile : public QPushButton {
    Q_OBJECT
public:
    explicit KeyTile(int index, QWidget *parent = nullptr);

    void setConfig(const QJsonObject &config, int size);
    void setDynamicText(const QString &text);
    int index() const { return m_index; }

signals:
    void clickedTile(int index);
    void heldTile(int index);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    int m_index;
    QJsonObject m_config;
    QString m_dynamicText;
    bool m_hasDynamicText = false;
    bool m_held = false;
    QTimer *m_holdTimer;
    static constexpr int kHoldMs = 450;
};

class MacroManagerDialog : public QDialog {
    Q_OBJECT
public:
    // sshSend/sshBroadcast verbinden die Makros mit den Konsolen der App.
    MacroManagerDialog(AsyncBridge *bridge,
                       std::function<void(const QString &, bool)> sshSend,
                       std::function<void(const QString &, bool)> sshBroadcast,
                       QWidget *parent = nullptr);
    ~MacroManagerDialog() override;

    // Vom Hauptfenster gerufen: passende Ansicht (schwebend oder angedockt)
    // zeigen und hervorheben; merkt sich "geoeffnet" fuer den naechsten Start.
    void present();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void refreshLayers();
    void onLayerSelected();
    void addLayer();
    void editLayer();
    void deleteLayer();
    void drawGrid();
    void onTileClicked(int index);
    void onTileHeld(int index);
    void runKey(const QJsonObject &config, int index);
    // Sequenz: jeder Schritt erst nach Abschluss des vorherigen.
    void runSteps(std::vector<QJsonObject> steps, const QString &keyId);
    void exportLayers();
    void importLayers();
    void toggleMode(bool runMode);
    void pollForeground();
    core::macros::Layer *currentLayer();

    // --- Andocken (Makroleiste am Rand des Hauptfensters) ---
    void setDock(const QString &side, bool persist);  // float|left|right|top|bottom
    void onDockCombo();
    void syncDockCombo();
    void applyModeVisibility();  // Editier-Chrome nur im Bearbeiten-Modus zeigen
    void updateModeLabel();
    void saveConfig();

    AsyncBridge *m_bridge;
    core::macros::MacroConfig m_config;
    core::macroactions::ExecContext m_context;
    QString m_currentLayer;
    QStringList m_layerHistory;      // fuer "Zurueck"
    bool m_runMode = false;
    QTimer *m_foregroundTimer = nullptr;
    QString m_lastForegroundApp;

    QListWidget *m_layerList = nullptr;
    QWidget *m_gridHost = nullptr;
    QGridLayout *m_grid = nullptr;
    std::vector<KeyTile *> m_tiles;
    QPushButton *m_modeButton = nullptr;
    QCheckBox *m_contextAware = nullptr;
    QSpinBox *m_keySize = nullptr;
    QLabel *m_status = nullptr;

    // Andocken: die gesamte Oberflaeche liegt in m_content, das zwischen dem
    // schwebenden Dialog (m_dialogLayout) und einem QDockWidget umziehen kann.
    QVBoxLayout *m_dialogLayout = nullptr;
    QWidget *m_content = nullptr;
    QWidget *m_leftPanel = nullptr;   // Layer-Editor (nur Bearbeiten-Modus)
    QWidget *m_dockRow = nullptr;     // "Andocken:"-Zeile (nur Bearbeiten-Modus)
    QComboBox *m_dockCombo = nullptr;
    QPushButton *m_closeButton = nullptr;
    QString m_dockSide = QStringLiteral("float");
    QDockWidget *m_dock = nullptr;
};

} // namespace ncssh::gui
