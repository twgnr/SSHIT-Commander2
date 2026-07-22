// Befehlspalette: tabellarischer, sortierbarer Katalog nuetzlicher Shell-Befehle
// (Befehl · Kategorie · Plattform · Beschreibung) mit OS-Filter und Suche.
// Gefaehrliche Befehle sind farbig markiert.
// (Port von gui/command_palette.py)
#pragma once

#include "ncssh/core/commands.hpp"

#include <QDialog>
#include <optional>
#include <vector>

class QLineEdit;
class QTableWidget;
class QComboBox;
class QLabel;

namespace ncssh::gui {

class CommandPalette : public QDialog {
    Q_OBJECT
public:
    // osType: "posix" | "windows" — das erkannte Server-OS.
    explicit CommandPalette(const QString &osType, QWidget *parent = nullptr);

    // Der zusammengebaute Befehl (nach Accepted).
    QString command() const { return m_command; }
    // true = direkt ausfuehren, false = nur in die Konsole einfuegen.
    bool runDirectly() const { return m_runDirectly; }

private:
    void refill();
    std::optional<core::CommandSpec> currentSpec() const;
    void openBuilder(bool runAfter);

    QString m_osType;
    QString m_command;
    bool m_runDirectly = false;
    std::vector<core::CommandSpec> m_shown;

    QLineEdit *m_search = nullptr;
    QComboBox *m_osFilter = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_detail = nullptr;
};

} // namespace ncssh::gui
