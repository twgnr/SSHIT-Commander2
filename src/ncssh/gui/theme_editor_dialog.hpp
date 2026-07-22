// Theme-Editor: eigene Farbschemata anlegen, bearbeiten und loeschen —
// mit Live-Vorschau auf dem Dialog selbst.
// (Port von gui/theme_editor_dialog.py)
#pragma once

#include "ncssh/gui/style.hpp"

#include <QDialog>
#include <QHash>

class QLineEdit;
class QComboBox;
class QPushButton;
class QLabel;

namespace ncssh::gui {

class ThemeEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit ThemeEditorDialog(QWidget *parent = nullptr);

    // Name des zuletzt gespeicherten Themes (leer, wenn nichts gespeichert).
    QString savedTheme() const { return m_savedTheme; }

private:
    void loadTheme(const QString &name);
    void pickColor(const QString &key);
    void updatePreview();
    void save();
    void remove();

    ThemeColors m_colors;
    QString m_savedTheme;
    QString m_editingName;   // gerade bearbeitetes eigenes Theme (fuer Rueckfrage)

    QComboBox *m_baseTheme = nullptr;
    QLineEdit *m_name = nullptr;
    QHash<QString, QPushButton *> m_swatches;
    QWidget *m_preview = nullptr;
    QLabel *m_status = nullptr;
};

} // namespace ncssh::gui
