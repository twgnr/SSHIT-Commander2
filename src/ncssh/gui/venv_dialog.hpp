// venv aufsetzen/verwalten: virtuelle Python-Umgebung anlegen und in einem
// Terminal aktivieren; listet bekannte venv-/pipenv-Umgebungen mit Typ,
// Version, Projekt und Notiz.  (Port von gui/venv_dialog.py)
#pragma once

#include "ncssh/core/venvtools.hpp"
#include "ncssh/gui/bridge.hpp"

#include <QDialog>
#include <QStringList>
#include <vector>

class QLineEdit;
class QComboBox;
class QCheckBox;
class QTableWidget;
class QPlainTextEdit;
class QLabel;

namespace ncssh::gui {

class VenvDialog : public QDialog {
    Q_OBJECT
public:
    VenvDialog(AsyncBridge *bridge, const QString &projectDir, const QString &osType,
               QWidget *parent = nullptr);

    // Auszufuehrende Befehlsfolge (nach Accepted) — die Konsole fuehrt sie aus.
    QStringList commands() const { return m_commands; }

private:
    void reloadEnvs();
    void updatePreview();
    void accept() override;
    void deleteSelected();

    AsyncBridge *m_bridge;
    QString m_projectDir;
    QString m_osType;
    QStringList m_commands;
    std::vector<core::VenvInfo> m_envs;

    QLineEdit *m_project = nullptr;
    QLineEdit *m_venvDir = nullptr;
    QComboBox *m_python = nullptr;
    QLineEdit *m_install = nullptr;
    QCheckBox *m_skipLock = nullptr;
    QPlainTextEdit *m_preview = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_status = nullptr;
};

} // namespace ncssh::gui
