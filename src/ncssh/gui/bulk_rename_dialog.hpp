// Massen-Umbenennen: Suchen/Ersetzen (Text/Platzhalter/Regex), Praefix/Suffix,
// Nummerierung, Gross-/Kleinschreibung, Endung — mit Live-Vorschau,
// Konfliktauflösung und gefahrloser Ausfuehrungsreihenfolge.
#pragma once

#include "ncssh/core/bulkrename.hpp"
#include "ncssh/core/filesystem.hpp"
#include "ncssh/gui/bridge.hpp"

#include <QDialog>
#include <functional>
#include <vector>

class QLineEdit;
class QComboBox;
class QCheckBox;
class QSpinBox;
class QTableWidget;
class QLabel;
class QPushButton;

namespace ncssh::gui {

class BulkRenameDialog : public QDialog {
    Q_OBJECT
public:
    BulkRenameDialog(AsyncBridge *bridge, core::FileSystemProvider *provider,
                     const QString &dir, const std::vector<QString> &names,
                     QWidget *parent = nullptr);

private:
    // Vorgefertigtes Suchen/Ersetzen fuer haeufige Aufraeumarbeiten.
    struct RegexPreset {
        QString label;
        QString search;
        QString replace;
        bool ignoreCase = false;
    };
    static const std::vector<RegexPreset> &regexPresets();

    core::RenameOptions collectOptions() const;
    void updatePreview();
    void apply();
    void undo();
    void applyRegexPreset(int index);
    // Fuehrt die Umbenennungen aus; onDone bekommt die Anzahl.
    void runRenames(const std::vector<core::RenamePair> &steps, int count,
                    const std::function<void(int)> &onDone, QPushButton *button);

    AsyncBridge *m_bridge;
    core::FileSystemProvider *m_provider;
    QString m_dir;
    std::vector<QString> m_names;
    std::vector<core::RenamePair> m_pairs;
    // Rueckabwicklung der zuletzt ausgefuehrten Umbenennung (neu -> alt).
    std::vector<core::RenamePair> m_undoJobs;

    QLineEdit *m_search = nullptr;
    QLineEdit *m_replace = nullptr;
    QComboBox *m_matchMode = nullptr;
    QComboBox *m_preset = nullptr;      // Regex-Vorlagen
    QCheckBox *m_ignoreCase = nullptr;
    QCheckBox *m_replaceAll = nullptr;
    QLabel *m_regexError = nullptr;
    QLineEdit *m_removeText = nullptr;
    QSpinBox *m_trimStart = nullptr;
    QSpinBox *m_trimEnd = nullptr;
    QLineEdit *m_insertText = nullptr;
    QSpinBox *m_insertPos = nullptr;
    QComboBox *m_scope = nullptr;
    QSpinBox *m_numPos = nullptr;
    QPushButton *m_applyButton = nullptr;
    QPushButton *m_undoButton = nullptr;
    QLineEdit *m_prefix = nullptr;
    QLineEdit *m_suffix = nullptr;
    QComboBox *m_caseMode = nullptr;
    QComboBox *m_spaceMode = nullptr;
    QCheckBox *m_numbering = nullptr;
    QSpinBox *m_start = nullptr;
    QSpinBox *m_step = nullptr;
    QSpinBox *m_width = nullptr;
    QLineEdit *m_numSep = nullptr;
    QComboBox *m_numPosition = nullptr;
    QComboBox *m_extMode = nullptr;
    QLineEdit *m_extValue = nullptr;
    QComboBox *m_sortMode = nullptr;
    QCheckBox *m_autoResolve = nullptr;

    QTableWidget *m_preview = nullptr;
    QLabel *m_status = nullptr;
};

} // namespace ncssh::gui
