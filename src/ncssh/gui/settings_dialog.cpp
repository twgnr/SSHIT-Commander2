#include "ncssh/gui/settings_dialog.hpp"

#include "ncssh/core/dateformat.hpp"
#include "ncssh/core/i18n.hpp"
#include "ncssh/core/settings.hpp"
#include "ncssh/core/shortcuts.hpp"
#include "ncssh/gui/style.hpp"
#include "ncssh/net/ollama.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(_t("Einstellungen"));
    resize(720, 580);

    auto *layout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    tabs->addTab(buildGeneralTab(), _t("Allgemein"));
    tabs->addTab(buildAiTab(), _t("KI"));
    tabs->addTab(buildShortcutsTab(), _t("Tastenkürzel"));
    layout->addWidget(tabs, 1);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(box, &QDialogButtonBox::accepted, this, &SettingsDialog::save);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(box);
}

QWidget *SettingsDialog::buildGeneralTab()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);

    m_language = new QComboBox(page);
    for (const QString &code : core::availableLanguages())
        m_language->addItem(core::languageName(code), code);
    m_language->setCurrentIndex(
        m_language->findData(core::getSettingString(QStringLiteral("language"),
                                                    QStringLiteral("de"))));
    form->addRow(_t("Sprache"), m_language);

    m_theme = new QComboBox(page);
    m_theme->addItems(themeNames());
    m_theme->setCurrentText(core::getSettingString(QStringLiteral("theme"), defaultTheme()));
    form->addRow(_t("Theme"), m_theme);

    const auto makeFontSpin = [&](const char *key, int def) {
        auto *spin = new QSpinBox(page);
        spin->setRange(6, 32);
        spin->setValue(core::getSettingInt(QString::fromLatin1(key), def));
        return spin;
    };
    m_editorFont = makeFontSpin("editor_font_size", 11);
    m_terminalFont = makeFontSpin("terminal_font_size", 10);
    m_paneFont = makeFontSpin("pane_font_size", 10);
    form->addRow(_t("Schriftgröße Editor"), m_editorFont);
    form->addRow(_t("Schriftgröße Terminal"), m_terminalFont);
    form->addRow(_t("Schriftgröße Panes"), m_paneFont);

    m_dateFormat = new QLineEdit(
        core::getSettingString(QStringLiteral("date_format"),
                               QString::fromLatin1(core::DEFAULT_DATE_FORMAT)), page);
    m_dateFormat->setPlaceholderText(QStringLiteral("DD.MM.YYYY HH24:MI"));
    form->addRow(_t("Datumsformat"), m_dateFormat);

    m_hideHidden = new QCheckBox(_t("Versteckte Dateien ausblenden"), page);
    m_hideHidden->setChecked(core::getSettingBool(QStringLiteral("hide_hidden"), false));
    form->addRow(QString(), m_hideHidden);

    m_restoreTabs = new QCheckBox(_t("Tabs beim Start wiederherstellen"), page);
    m_restoreTabs->setChecked(core::getSettingBool(QStringLiteral("restore_tabs"), true));
    form->addRow(QString(), m_restoreTabs);

    auto *pathRow = new QHBoxLayout();
    m_startPath = new QLineEdit(core::getSettingString(QStringLiteral("start_path")), page);
    auto *browse = new QPushButton(QStringLiteral("…"), page);
    browse->setFixedWidth(34);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, _t("Standard-Startpfad"));
        if (!d.isEmpty())
            m_startPath->setText(d);
    });
    pathRow->addWidget(m_startPath, 1);
    pathRow->addWidget(browse);
    form->addRow(_t("Standard-Startpfad"), pathRow);

    auto *hint = new QLabel(_t("Sprache und Schriftgrößen greifen nach einem Neustart."), page);
    hint->setObjectName(QStringLiteral("Muted"));
    hint->setWordWrap(true);
    form->addRow(hint);
    return page;
}

QWidget *SettingsDialog::buildAiTab()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);

    m_aiEnabled = new QCheckBox(_t("KI-Assistent aktivieren (lokal über Ollama)"), page);
    m_aiEnabled->setChecked(core::getSettingBool(QStringLiteral("ai_enabled"), false));
    form->addRow(QString(), m_aiEnabled);

    auto *urlRow = new QHBoxLayout();
    m_aiUrl = new QLineEdit(
        core::getSettingString(QStringLiteral("ai_url"), net::DEFAULT_BASE_URL), page);
    auto *testBtn = new QPushButton(_t("Testen"), page);
    connect(testBtn, &QPushButton::clicked, this, &SettingsDialog::testOllama);
    urlRow->addWidget(m_aiUrl, 1);
    urlRow->addWidget(testBtn);
    form->addRow(_t("Ollama-Adresse"), urlRow);

    auto *modelRow = new QHBoxLayout();
    m_aiModel = new QComboBox(page);
    m_aiModel->setEditable(true);
    m_aiModel->setCurrentText(core::getSettingString(QStringLiteral("ai_model")));
    auto *loadBtn = new QPushButton(_t("Modelle laden"), page);
    connect(loadBtn, &QPushButton::clicked, this, &SettingsDialog::loadOllamaModels);
    modelRow->addWidget(m_aiModel, 1);
    modelRow->addWidget(loadBtn);
    form->addRow(_t("Modell"), modelRow);

    m_aiStatus = new QLabel(page);
    m_aiStatus->setObjectName(QStringLiteral("Muted"));
    m_aiStatus->setWordWrap(true);
    form->addRow(m_aiStatus);
    return page;
}

QWidget *SettingsDialog::buildShortcutsTab()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    m_shortcuts = new QTableWidget(0, 3, page);
    m_shortcuts->setHorizontalHeaderLabels({_t("Gruppe"), _t("Aktion"), _t("Kürzel")});
    m_shortcuts->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_shortcuts->verticalHeader()->setVisible(false);
    m_shortcuts->setAlternatingRowColors(true);

    const QHash<QString, QString> current = core::getShortcuts();
    int row = 0;
    for (const core::ShortcutDef &def : core::shortcutDefs()) {
        m_shortcuts->insertRow(row);
        auto *groupItem = new QTableWidgetItem(def.group);
        groupItem->setFlags(groupItem->flags() & ~Qt::ItemIsEditable);
        auto *labelItem = new QTableWidgetItem(def.label);
        labelItem->setFlags(labelItem->flags() & ~Qt::ItemIsEditable);
        labelItem->setData(Qt::UserRole, def.id);
        m_shortcuts->setItem(row, 0, groupItem);
        m_shortcuts->setItem(row, 1, labelItem);
        m_shortcuts->setItem(row, 2, new QTableWidgetItem(current.value(def.id)));
        ++row;
    }
    layout->addWidget(m_shortcuts, 1);

    auto *hint = new QLabel(
        _t("Kürzel direkt in die Spalte eintragen (z. B. Ctrl+Shift+F). Doppelte werden beim Speichern gemeldet."),
        page);
    hint->setObjectName(QStringLiteral("Muted"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *resetBtn = new QPushButton(_t("Auf Standard zurücksetzen"), page);
    connect(resetBtn, &QPushButton::clicked, this, [this] {
        const QHash<QString, QString> defaults = core::defaultShortcuts();
        for (int r = 0; r < m_shortcuts->rowCount(); ++r) {
            const QString id = m_shortcuts->item(r, 1)->data(Qt::UserRole).toString();
            m_shortcuts->item(r, 2)->setText(defaults.value(id));
        }
    });
    layout->addWidget(resetBtn);
    return page;
}

void SettingsDialog::testOllama()
{
    m_aiStatus->setText(_t("Teste …"));
    try {
        const QString v = net::version(m_aiUrl->text().trimmed());
        m_aiStatus->setText(QStringLiteral("✓ Ollama %1").arg(v));
    } catch (const std::exception &exc) {
        m_aiStatus->setText(QStringLiteral("✗ %1").arg(QString::fromUtf8(exc.what())));
    }
}

void SettingsDialog::loadOllamaModels()
{
    try {
        const auto models = net::listModels(m_aiUrl->text().trimmed());
        const QString keep = m_aiModel->currentText();
        m_aiModel->clear();
        for (const QJsonObject &m : models)
            m_aiModel->addItem(m.value(QStringLiteral("name")).toString());
        if (!keep.isEmpty())
            m_aiModel->setCurrentText(keep);
        m_aiStatus->setText(QStringLiteral("%1 Modell(e) gefunden").arg(models.size()));
    } catch (const std::exception &exc) {
        m_aiStatus->setText(QStringLiteral("✗ %1").arg(QString::fromUtf8(exc.what())));
    }
}

void SettingsDialog::save()
{
    // Tastenkuerzel auf Dubletten pruefen
    QHash<QString, QString> mapping;
    QHash<QString, QString> seen;  // Kuerzel -> Aktion
    for (int r = 0; r < m_shortcuts->rowCount(); ++r) {
        const QString id = m_shortcuts->item(r, 1)->data(Qt::UserRole).toString();
        const QString key = m_shortcuts->item(r, 2)->text().trimmed();
        mapping.insert(id, key);
        if (key.isEmpty())
            continue;
        if (seen.contains(key)) {
            QMessageBox::warning(
                this, _t("Doppeltes Kürzel"),
                QStringLiteral("\"%1\" ist doppelt vergeben (%2 und %3).")
                    .arg(key, seen.value(key), m_shortcuts->item(r, 1)->text()));
            return;
        }
        seen.insert(key, m_shortcuts->item(r, 1)->text());
    }

    core::setSetting(QStringLiteral("language"), m_language->currentData().toString());
    core::setSetting(QStringLiteral("theme"), m_theme->currentText());
    core::setSetting(QStringLiteral("editor_font_size"), m_editorFont->value());
    core::setSetting(QStringLiteral("terminal_font_size"), m_terminalFont->value());
    core::setSetting(QStringLiteral("pane_font_size"), m_paneFont->value());
    core::setSetting(QStringLiteral("date_format"), m_dateFormat->text());
    core::setSetting(QStringLiteral("hide_hidden"), m_hideHidden->isChecked());
    core::setSetting(QStringLiteral("restore_tabs"), m_restoreTabs->isChecked());
    core::setSetting(QStringLiteral("start_path"), m_startPath->text());
    core::setSetting(QStringLiteral("ai_enabled"), m_aiEnabled->isChecked());
    core::setSetting(QStringLiteral("ai_url"), m_aiUrl->text().trimmed());
    core::setSetting(QStringLiteral("ai_model"), m_aiModel->currentText().trimmed());
    core::saveShortcuts(mapping);
    accept();
}

} // namespace ncssh::gui
