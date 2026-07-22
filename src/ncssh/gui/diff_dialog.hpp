// Verzeichnis-Vergleich/Sync: beide Panes gegenueberstellen (nur links/rechts,
// neuer), optional rekursiv, und per Klick angleichen (ueber die Transfer-Queue).
// (Port von gui/diff_dialog.py)
#pragma once

#include "ncssh/core/diff.hpp"
#include "ncssh/core/filesystem.hpp"
#include "ncssh/gui/bridge.hpp"

#include <QDialog>
#include <vector>

class QTableWidget;
class QCheckBox;
class QLabel;

namespace ncssh::gui {

class TransferManager;

class DiffDialog : public QDialog {
    Q_OBJECT
public:
    DiffDialog(AsyncBridge *bridge, TransferManager *transfers,
               core::FileSystemProvider *left, const QString &leftPath,
               core::FileSystemProvider *right, const QString &rightPath,
               QWidget *parent = nullptr);

private:
    void compare();
    void copySelected(bool toRight);

    AsyncBridge *m_bridge;
    TransferManager *m_transfers;
    core::FileSystemProvider *m_left;
    QString m_leftPath;
    core::FileSystemProvider *m_right;
    QString m_rightPath;
    std::vector<core::DiffEntry> m_entries;

    QTableWidget *m_table = nullptr;
    QCheckBox *m_recursive = nullptr;
    QCheckBox *m_onlyDifferences = nullptr;
    QLabel *m_status = nullptr;
};

} // namespace ncssh::gui
