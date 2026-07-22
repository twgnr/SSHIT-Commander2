// System-Suche: Dateiname-Suche und Inhalts-Suche (grep) mit Wildcards/Regex,
// Ausschluessen, Filtern; Ergebnisse laufen live ein.
// (Port von gui/search_dialog.py)
#pragma once

#include "ncssh/core/filesearch.hpp"
#include "ncssh/gui/bridge.hpp"

#include <QDialog>

class QLineEdit;
class QListWidget;
class QCheckBox;
class QComboBox;
class QSpinBox;
class QPushButton;
class QLabel;

namespace ncssh::gui {

class SearchDialog : public QDialog {
    Q_OBJECT
public:
    // mode: "name" (Dateiname) oder "content" (grep).
    SearchDialog(AsyncBridge *bridge, const QString &mode, const QString &startPath,
                 QWidget *parent = nullptr);

    // Doppelt angeklickter Treffer (nach Accepted) — Pfad zum Oeffnen.
    QString chosenPath() const { return m_chosenPath; }

private:
    void startSearch();
    void stopSearch();
    core::SearchOptions collectOptions() const;

    AsyncBridge *m_bridge;
    QString m_mode;
    QString m_chosenPath;
    BridgeTask *m_task = nullptr;

    QLineEdit *m_root = nullptr;
    QLineEdit *m_pattern = nullptr;
    QLineEdit *m_include = nullptr;
    QLineEdit *m_exclude = nullptr;
    QLineEdit *m_excludeDir = nullptr;
    QCheckBox *m_regex = nullptr;
    QCheckBox *m_ignoreCase = nullptr;
    QCheckBox *m_wholeWord = nullptr;
    QCheckBox *m_binary = nullptr;
    QCheckBox *m_namesOnly = nullptr;
    QCheckBox *m_invert = nullptr;
    QComboBox *m_kind = nullptr;
    QSpinBox *m_maxDepth = nullptr;
    QSpinBox *m_minSize = nullptr;
    QSpinBox *m_newerThan = nullptr;
    QSpinBox *m_context = nullptr;
    QWidget *m_advanced = nullptr;          // einklappbarer Bereich
    QPushButton *m_advancedButton = nullptr;
    QListWidget *m_results = nullptr;
    QPushButton *m_startBtn = nullptr;
    QLabel *m_status = nullptr;
};

} // namespace ncssh::gui
