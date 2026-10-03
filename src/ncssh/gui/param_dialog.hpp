// Parameter-Fenster der Konsole: Optionen des getippten Befehls (Doppelklick
// fuegt an) und — wo der Befehl eine Reihenfolge hat (cp QUELLE ZIEL) —
// Eingabefelder fuer die Argumente, mit Tab-Pfadvervollstaendigung wie im
// Terminal. Angefuegt wird direkt an die Befehlszeile der Konsole.
#pragma once

#include "ncssh/core/command_params.hpp"
#include "ncssh/gui/bridge.hpp"

#include <QDialog>
#include <functional>
#include <vector>

class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QWidget;

namespace ncssh::core { class FileSystemProvider; }

namespace ncssh::gui {

class ParamDialog : public QDialog {
    Q_OBJECT
public:
    using AppendFn = std::function<void(const QString &text)>;
    using LineFn = std::function<QString()>;

    // line: getippte Zeile beim Oeffnen; currentLine liefert sie spaeter neu
    // (fuer fehlende Befehlswoerter beim Anfuegen der Argumente).
    ParamDialog(AsyncBridge *bridge, const QString &line, const QString &osType,
                core::FileSystemProvider *provider, const QString &cwd,
                AppendFn append, LineFn currentLine, QWidget *parent = nullptr);

    // Optionen aus der --help-Ausgabe nachreichen (kommen asynchron).
    void addHelpOptions(const std::vector<core::CommandOption> &options);
    void setHelpStatus(const QString &text);

    QString commandName() const { return m_name; }

private:
    void addOptions(const std::vector<core::CommandOption> &options, bool fromCatalog);
    void insertOption(int index);
    void buildFields();
    void applyFilter();
    void appendPositionals();
    QString positionalPreview() const;
    void updatePreview();

    AsyncBridge *m_bridge;
    QString m_name;
    QString m_osType;
    core::FileSystemProvider *m_provider;
    QString m_cwd;
    AppendFn m_append;
    LineFn m_currentLine;

    std::vector<core::CommandSpec> m_specs;
    std::vector<core::CommandOption> m_options;   // Index = Item-Daten

    QComboBox *m_variant = nullptr;
    QWidget *m_fieldsBox = nullptr;
    QFormLayout *m_fieldsForm = nullptr;
    QStringList m_fieldNames;                     // Parametername je Feld
    QList<QWidget *> m_fieldWidgets;              // QLineEdit bzw. QComboBox je Feld
    QLabel *m_preview = nullptr;
    QPushButton *m_appendFieldsBtn = nullptr;

    QLineEdit *m_filter = nullptr;
    QTreeWidget *m_tree = nullptr;
    QLabel *m_helpStatus = nullptr;
};

} // namespace ncssh::gui
