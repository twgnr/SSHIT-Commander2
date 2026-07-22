// Befehlsassistent: Parameter eines Befehls per Textfeld / Dropdown / Checkbox
// setzen, mit Live-Vorschau; einfuegen oder direkt ausfuehren. Unter Linux
// zusaetzlich sudo / als anderer Benutzer.
// (Port von gui/command_builder.py)
#pragma once

#include "ncssh/core/commands.hpp"

#include <QDialog>
#include <QHash>
#include <vector>

class QLineEdit;
class QComboBox;
class QCheckBox;
class QPlainTextEdit;
class QWidget;

namespace ncssh::gui {

class CommandBuilder : public QDialog {
    Q_OBJECT
public:
    CommandBuilder(const core::CommandSpec &spec, const QString &osType,
                   QWidget *parent = nullptr);

    QString command() const { return m_command; }
    bool runDirectly() const { return m_runDirectly; }

private:
    void updatePreview();
    QString buildCommand() const;

    core::CommandSpec m_spec;
    QString m_osType;
    QString m_command;
    bool m_runDirectly = false;

    // Eingabe-Widgets je Parametername
    QHash<QString, QLineEdit *> m_textInputs;
    QHash<QString, QComboBox *> m_choiceInputs;
    QHash<QString, QCheckBox *> m_flagInputs;

    QCheckBox *m_sudo = nullptr;
    QLineEdit *m_sudoUser = nullptr;
    QPlainTextEdit *m_preview = nullptr;
};

} // namespace ncssh::gui
