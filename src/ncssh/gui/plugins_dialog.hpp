// Externe Plugins verwalten: Programm, Parameter ({path}), Arbeitsverzeichnis
// und Kontextmenue-Einbindung.
#pragma once

#include "ncssh/core/plugins.hpp"

#include <QDialog>
#include <vector>

class QListWidget;
class QLineEdit;
class QCheckBox;
class QComboBox;

namespace ncssh::gui {

class PluginsDialog : public QDialog {
    Q_OBJECT
public:
    explicit PluginsDialog(QWidget *parent = nullptr);

private:
    void reload();
    void loadIntoForm(const core::plugins::Plugin &plugin);
    core::plugins::Plugin formToPlugin() const;
    void onSave();
    void onDelete();
    void onLaunch();

    std::vector<core::plugins::Plugin> m_plugins;
    int m_currentId = 0;

    QListWidget *m_list = nullptr;
    QLineEdit *m_name = nullptr;
    QLineEdit *m_exe = nullptr;
    QLineEdit *m_args = nullptr;
    QLineEdit *m_workingDir = nullptr;
    QCheckBox *m_context = nullptr;
    QComboBox *m_targets = nullptr;
};

} // namespace ncssh::gui
