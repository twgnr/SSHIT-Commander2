// Einzeldatei-Vergleich: zwei Dateien als farbiger Unified-Diff.
#pragma once

#include "ncssh/core/filediff.hpp"
#include "ncssh/core/filesystem.hpp"
#include "ncssh/gui/bridge.hpp"

#include <QDialog>

class QPlainTextEdit;
class QLabel;

namespace ncssh::gui {

class FileDiffDialog : public QDialog {
    Q_OBJECT
public:
    FileDiffDialog(AsyncBridge *bridge,
                   core::FileSystemProvider *provA, const QString &pathA,
                   core::FileSystemProvider *provB, const QString &pathB,
                   QWidget *parent = nullptr);

private:
    // Ergebnis des Worker-Jobs (Diff laeuft NICHT im GUI-Thread).
    struct DiffResult {
        std::vector<core::DiffRow> rows;
        bool approximate = false;   // Grenze erreicht -> grober Vergleich
    };
    void render(const DiffResult &res);

    QString m_nameA;
    QString m_nameB;
    QPlainTextEdit *m_view = nullptr;
    QLabel *m_status = nullptr;
};

} // namespace ncssh::gui
