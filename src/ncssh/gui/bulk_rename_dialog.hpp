// Massen-Umbenennen: Suchen/Ersetzen (Text/Platzhalter/Regex), Praefix/Suffix,
// Nummerierung, Gross-/Kleinschreibung, Endung — mit Live-Vorschau,
// Konfliktauflösung und gefahrloser Ausfuehrungsreihenfolge.
// (Port von gui/bulk_rename_dialog.py)
#pragma once

#include "ncssh/core/bulkrename.hpp"
#include "ncssh/core/filesystem.hpp"
#include "ncssh/gui/bridge.hpp"

#include <QDialog>
#include <vector>

class QLineEdit;
class QComboBox;
class QCheckBox;
class QSpinBox;
class QTableWidget;
class QLabel;

namespace ncssh::gui {

class BulkRenameDialog : public QDialog {
    Q_OBJECT
public:
    BulkRenameDialog(AsyncBridge *bridge, core::FileSystemProvider *provider,
                     const QString &dir, const std::vector<QString> &names,
                     QWidget *parent = nullptr);

private:
    core::RenameOptions collectOptions() const;
    void updatePreview();
    void apply();

    AsyncBridge *m_bridge;
    core::FileSystemProvider *m_provider;
    QString m_dir;
    std::vector<QString> m_names;
    std::vector<core::RenamePair> m_pairs;

    QLineEdit *m_search = nullptr;
    QLineEdit *m_replace = nullptr;
    QComboBox *m_matchMode = nullptr;
    QCheckBox *m_ignoreCase = nullptr;
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
