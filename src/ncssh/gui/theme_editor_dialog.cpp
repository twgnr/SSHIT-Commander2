#include "ncssh/gui/theme_editor_dialog.hpp"

#include "ncssh/core/i18n.hpp"

#include <QColorDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;

ThemeEditorDialog::ThemeEditorDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(_t("Theme-Editor"));
    resize(720, 640);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout();

    m_baseTheme = new QComboBox(this);
    m_baseTheme->addItems(themeNames());
    connect(m_baseTheme, &QComboBox::currentTextChanged, this, &ThemeEditorDialog::loadTheme);
    form->addRow(_t("Basis / Bearbeiten"), m_baseTheme);

    m_name = new QLineEdit(this);
    m_name->setPlaceholderText(_t("Name des eigenen Themes"));
    form->addRow(_t("Name"), m_name);
    layout->addLayout(form);

    // Farbfelder in einem scrollbaren Raster
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto *host = new QWidget(scroll);
    auto *grid = new QGridLayout(host);
    int row = 0;
    for (const ColorKey &ck : colorKeys()) {
        grid->addWidget(new QLabel(ck.label, host), row, 0);
        auto *swatch = new QPushButton(host);
        swatch->setFixedSize(120, 26);
        const QString key = ck.key;
        connect(swatch, &QPushButton::clicked, this, [this, key] { pickColor(key); });
        m_swatches.insert(key, swatch);
        grid->addWidget(swatch, row, 1);
        ++row;
    }
    scroll->setWidget(host);
    layout->addWidget(scroll, 1);

    // Live-Vorschau — zeigt auch Auswahl, gedaempften und inaktiven Text, weil
    // sich genau daran entscheidet, ob ein Farbschema lesbar bleibt.
    auto *previewBox = new QGroupBox(_t("Vorschau"), this);
    auto *previewLayout = new QVBoxLayout(previewBox);
    m_preview = new QWidget(previewBox);
    auto *innerLayout = new QVBoxLayout(m_preview);

    auto *widgetRow = new QHBoxLayout();
    auto *sampleBtn = new QPushButton(_t("Knopf"), m_preview);
    sampleBtn->setDefault(true);
    widgetRow->addWidget(sampleBtn);
    auto *disabledBtn = new QPushButton(_t("Inaktiv"), m_preview);
    disabledBtn->setEnabled(false);
    widgetRow->addWidget(disabledBtn);
    widgetRow->addWidget(new QLineEdit(_t("Eingabefeld"), m_preview));
    auto *muted = new QLabel(_t("Gedämpfter Text"), m_preview);
    muted->setObjectName(QStringLiteral("Muted"));
    widgetRow->addWidget(muted);
    innerLayout->addLayout(widgetRow);

    // Eine kleine Liste macht Auswahl- und Wechselzeilenfarbe sichtbar.
    auto *sampleList = new QListWidget(m_preview);
    sampleList->setAlternatingRowColors(true);
    sampleList->addItem(_t("Beispiel-Eintrag 1"));
    sampleList->addItem(_t("Beispiel-Eintrag 2"));
    sampleList->addItem(_t("Ausgewählt"));
    sampleList->setCurrentRow(2);
    sampleList->setFixedHeight(78);
    innerLayout->addWidget(sampleList);

    previewLayout->addWidget(m_preview);
    layout->addWidget(previewBox);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("Muted"));
    layout->addWidget(m_status);

    auto *buttons = new QHBoxLayout();
    auto *deleteBtn = new QPushButton(_t("Löschen"), this);
    auto *cancel = new QPushButton(_t("Schließen"), this);
    auto *saveBtn = new QPushButton(_t("Speichern"), this);
    saveBtn->setDefault(true);
    connect(deleteBtn, &QPushButton::clicked, this, &ThemeEditorDialog::remove);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(saveBtn, &QPushButton::clicked, this, &ThemeEditorDialog::save);
    buttons->addWidget(deleteBtn);
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(saveBtn);
    layout->addLayout(buttons);

    loadTheme(m_baseTheme->currentText());
}

void ThemeEditorDialog::loadTheme(const QString &name)
{
    m_colors = themeColors(name);
    // Eingebaute Themes koennen nur als Basis fuer ein neues dienen.
    m_name->setText(isBuiltin(name) ? name + QStringLiteral(" (Kopie)") : name);
    updatePreview();
}

void ThemeEditorDialog::pickColor(const QString &key)
{
    const QColor current(m_colors.value(key));
    const QColor chosen = QColorDialog::getColor(current, this, _t("Farbe wählen"));
    if (!chosen.isValid())
        return;
    m_colors.insert(key, chosen.name());
    updatePreview();
}

void ThemeEditorDialog::updatePreview()
{
    for (auto it = m_swatches.begin(); it != m_swatches.end(); ++it) {
        const QString value = m_colors.value(it.key());
        it.value()->setText(value);
        it.value()->setStyleSheet(
            QStringLiteral("background: %1; color: %2; border: 1px solid %3;")
                .arg(value, m_colors.value(QStringLiteral("text")),
                     m_colors.value(QStringLiteral("border"))));
    }
    // Vorschau-Bereich mit dem bearbeiteten Theme stylen.
    m_preview->setStyleSheet(stylesheetFor(m_colors));
}

void ThemeEditorDialog::save()
{
    const QString name = m_name->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, _t("Fehler"), _t("Bitte einen Namen angeben."));
        return;
    }
    if (isBuiltin(name)) {
        QMessageBox::warning(this, _t("Theme-Name"),
                             _t("Dieser Name ist von einem eingebauten Theme belegt."));
        return;
    }
    // Bestehendes eigenes Theme nicht stillschweigend ersetzen.
    if (name != m_editingName && customThemes().contains(name)) {
        if (QMessageBox::question(this, _t("Theme-Name"),
                                  _t("Ein Theme mit diesem Namen existiert bereits.")
                                      + QStringLiteral("\n") + _t("Überschreiben?"))
            != QMessageBox::Yes)
            return;
    }
    saveCustomTheme(name, m_colors);
    m_editingName = name;
    m_savedTheme = name;
    // Auswahl aktualisieren, damit das neue Theme sofort erscheint.
    const QString keep = name;
    m_baseTheme->blockSignals(true);
    m_baseTheme->clear();
    m_baseTheme->addItems(themeNames());
    m_baseTheme->setCurrentText(keep);
    m_baseTheme->blockSignals(false);
    m_status->setText(QStringLiteral("Gespeichert: %1").arg(name));
}

void ThemeEditorDialog::remove()
{
    const QString name = m_baseTheme->currentText();
    if (isBuiltin(name)) {
        QMessageBox::information(this, _t("Löschen"),
                                 _t("Eingebaute Themes können nicht gelöscht werden."));
        return;
    }
    if (QMessageBox::question(this, _t("Löschen"),
                              QStringLiteral("Theme \"%1\" löschen?").arg(name))
        != QMessageBox::Yes)
        return;
    deleteCustomTheme(name);
    m_baseTheme->blockSignals(true);
    m_baseTheme->clear();
    m_baseTheme->addItems(themeNames());
    m_baseTheme->blockSignals(false);
    loadTheme(m_baseTheme->currentText());
    m_status->setText(QStringLiteral("Gelöscht: %1").arg(name));
}

} // namespace ncssh::gui
