#include "ncssh/gui/settings_dialog.hpp"

#include "ncssh/core/ai.hpp"
#include "ncssh/core/configio.hpp"
#include "ncssh/core/dateformat.hpp"
#include "ncssh/core/i18n.hpp"
#include "ncssh/core/settings.hpp"
#include "ncssh/core/shortcuts.hpp"
#include "ncssh/gui/style.hpp"
#include "ncssh/net/ollama.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QJsonObject>
#include <QPointer>
#include <QProgressBar>
#include "ncssh/gui/file_dialogs.hpp"
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QApplication>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;

SettingsDialog::SettingsDialog(QWidget *parent, AsyncBridge *bridge)
    : QDialog(parent), m_bridge(bridge)
{
    setWindowTitle(_t("Einstellungen"));
    resize(760, 640);

    auto *layout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    tabs->addTab(buildGeneralTab(), _t("Allgemein"));
    tabs->addTab(buildAiTab(), _t("KI"));
    tabs->addTab(buildShortcutsTab(), _t("Tastenkürzel"));
    layout->addWidget(tabs, 1);

    // Konfiguration als Ganzes sichern/einspielen (ohne Geheimnisse).
    auto *ioRow = new QHBoxLayout();
    auto *exportBtn = new QPushButton(_t("Konfiguration exportieren"), this);
    connect(exportBtn, &QPushButton::clicked, this, &SettingsDialog::exportConfig);
    auto *importBtn = new QPushButton(_t("Konfiguration importieren"), this);
    connect(importBtn, &QPushButton::clicked, this, &SettingsDialog::importConfig);
    ioRow->addWidget(exportBtn);
    ioRow->addWidget(importBtn);
    ioRow->addStretch();
    layout->addLayout(ioRow);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(box, &QDialogButtonBox::accepted, this, &SettingsDialog::save);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(box);
}

SettingsDialog::~SettingsDialog()
{
    // Ein laufender Modell-Download haelt Zeiger auf Widgets dieses Dialogs.
    // Ohne Abbruch schreiben seine Rueckmeldungen in bereits zerstoerte
    // Objekte, sobald der Nutzer das Fenster schliesst.
    if (m_pullTask && m_bridge)
        m_bridge->cancel(m_pullTask);
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

    auto *themeRow = new QHBoxLayout();
    m_theme = new QComboBox(page);
    m_theme->addItems(themeNames());
    m_theme->setCurrentText(core::getSettingString(QStringLiteral("theme"), defaultTheme()));
    auto *delThemeBtn = new QPushButton(_t("Theme löschen"), page);
    connect(delThemeBtn, &QPushButton::clicked, this, &SettingsDialog::deleteTheme);
    themeRow->addWidget(m_theme, 1);
    themeRow->addWidget(delThemeBtn);
    form->addRow(_t("Theme"), themeRow);

    const auto makeFontSpin = [&](const char *key, int def) {
        auto *spin = new QSpinBox(page);
        spin->setRange(6, 32);
        spin->setValue(core::getSettingInt(QString::fromLatin1(key), def));
        return spin;
    };
    m_editorFont = makeFontSpin("editor_font_size", 11);
    m_terminalFont = makeFontSpin("terminal_font_size", 10);
    // Default muss zu FilePanel::applyPaneStyle passen (11) — sonst
    // verkleinert schon das blosse Oeffnen+Speichern die Pane-Schrift.
    m_paneFont = makeFontSpin("pane_font_size", 11);
    form->addRow(_t("Editor-Schriftgröße"), m_editorFont);
    form->addRow(_t("Terminal-Schriftgröße"), m_terminalFont);
    form->addRow(_t("Pane-Schriftgröße (Dateiliste)"), m_paneFont);

    m_dateFormat = new QLineEdit(
        core::getSettingString(QStringLiteral("date_format"),
                               QString::fromLatin1(core::DEFAULT_DATE_FORMAT)), page);
    m_dateFormat->setPlaceholderText(QStringLiteral("DD.MM.YYYY HH24:MI"));
    form->addRow(_t("Datumsformat (Pane)"), m_dateFormat);
    auto *dateHint = new QLabel(
        _t("Token: DD MM YYYY YY HH24 HH12 MI SS MON — z. B. DD.MM.YYYY HH24:MI"), page);
    dateHint->setObjectName(QStringLiteral("Muted"));
    dateHint->setWordWrap(true);
    form->addRow(QString(), dateHint);

    m_hideHidden = new QCheckBox(_t("Versteckte Dateien ausblenden"), page);
    m_hideHidden->setChecked(core::getSettingBool(QStringLiteral("hide_hidden"), false));
    form->addRow(QString(), m_hideHidden);

    m_showIcons = new QCheckBox(_t("Programm-Logos vor Dateinamen anzeigen"), page);
    m_showIcons->setChecked(core::getSettingBool(QStringLiteral("show_file_icons"), true));
    form->addRow(QString(), m_showIcons);

    m_thumbnails = new QCheckBox(_t("Bild-Vorschau als Icon (Thumbnails)"), page);
    m_thumbnails->setChecked(core::getSettingBool(QStringLiteral("pane_thumbnails"), false));
    form->addRow(QString(), m_thumbnails);

    m_naturalSort = new QCheckBox(_t("Natürliche Sortierung (1, 2, 10)"), page);
    m_naturalSort->setChecked(core::getSettingBool(QStringLiteral("natural_sort"), true));
    form->addRow(QString(), m_naturalSort);

    m_compactRows = new QCheckBox(_t("Schlanke Ansicht (schmalere Zeilen)"), page);
    m_compactRows->setChecked(core::getSettingBool(QStringLiteral("compact_rows"), false));
    form->addRow(QString(), m_compactRows);

    // Ausfuehrbare Dateien hervorheben — Schalter und Farbe gehoeren zusammen.
    auto *execRow = new QHBoxLayout();
    m_execHighlight = new QCheckBox(_t("Ausführbare Dateien farblich hervorheben"), page);
    m_execHighlight->setChecked(core::getSettingBool(QStringLiteral("exec_highlight"), true));
    m_execColor = core::getSettingString(QStringLiteral("exec_color"),
                                         QStringLiteral("#3fb950"));
    m_execColorBtn = new QPushButton(_t("Farbe wählen …"), page);
    connect(m_execColorBtn, &QPushButton::clicked, this, &SettingsDialog::pickExecColor);
    connect(m_execHighlight, &QCheckBox::toggled, m_execColorBtn, &QPushButton::setEnabled);
    m_execColorBtn->setEnabled(m_execHighlight->isChecked());
    updateExecColorButton();
    execRow->addWidget(m_execHighlight, 1);
    execRow->addWidget(m_execColorBtn);
    form->addRow(_t("Farbe für ausführbare Dateien"), execRow);

    m_restoreTabs = new QCheckBox(_t("Tabs beim Start wiederherstellen"), page);
    m_restoreTabs->setChecked(core::getSettingBool(QStringLiteral("restore_tabs"), true));
    form->addRow(QString(), m_restoreTabs);

    m_autoConnect = new QCheckBox(_t("Beim Start automatisch zum letzten Server verbinden"),
                                  page);
    // Default true — muss zum Default in Workspace::restoreFrom passen, sonst
    // zeigt der Haken etwas anderes, als die App beim Start tatsaechlich tut.
    m_autoConnect->setChecked(core::getSettingBool(QStringLiteral("auto_connect_last"), true));
    form->addRow(QString(), m_autoConnect);

    auto *pathRow = new QHBoxLayout();
    m_startPath = new QLineEdit(core::getSettingString(QStringLiteral("start_path")), page);
    m_startPath->setPlaceholderText(_t("leer = Benutzerordner"));
    auto *browse = new QPushButton(QStringLiteral("…"), page);
    browse->setFixedWidth(34);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString d = getExistingDirectory(this, _t("Startpfad wählen"));
        if (!d.isEmpty())
            m_startPath->setText(d);
    });
    pathRow->addWidget(m_startPath, 1);
    pathRow->addWidget(browse);
    form->addRow(_t("Standard-Startpfad (lokal)"), pathRow);

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

    m_aiEnabled = new QCheckBox(_t("KI-Assistent aktivieren (lokales Ollama)"), page);
    m_aiEnabled->setChecked(core::getSettingBool(QStringLiteral("ai_enabled"), false));
    form->addRow(QString(), m_aiEnabled);

    auto *urlRow = new QHBoxLayout();
    // Schluessel aus core/ai — sonst schreibt der Dialog woandershin, als die
    // KI-Schicht liest, und die Einstellung bleibt wirkungslos.
    m_aiUrl = new QLineEdit(
        core::getSettingString(QString::fromLatin1(core::OLLAMA_URL),
                               QString::fromLatin1(core::OLLAMA_DEFAULT_BASE_URL)), page);
    auto *testBtn = new QPushButton(_t("Verbindung testen"), page);
    connect(testBtn, &QPushButton::clicked, this, &SettingsDialog::testOllama);
    urlRow->addWidget(m_aiUrl, 1);
    urlRow->addWidget(testBtn);
    form->addRow(_t("Ollama-Adresse"), urlRow);

    auto *modelRow = new QHBoxLayout();
    m_aiModel = new QComboBox(page);
    m_aiModel->setEditable(true);
    m_aiModel->setCurrentText(
        core::getSettingString(QString::fromLatin1(core::AI_MODEL)));
    auto *loadBtn = new QPushButton(_t("Modelle laden"), page);
    connect(loadBtn, &QPushButton::clicked, this, &SettingsDialog::loadOllamaModels);
    modelRow->addWidget(m_aiModel, 1);
    modelRow->addWidget(loadBtn);
    form->addRow(_t("Modell"), modelRow);

    m_aiStatus = new QLabel(page);
    m_aiStatus->setObjectName(QStringLiteral("Muted"));
    m_aiStatus->setWordWrap(true);
    form->addRow(m_aiStatus);

    // --- Modell herunterladen -------------------------------------------------
    // Vorschlaege mit Groessenangabe; eigene Tags bleiben eintippbar.
    auto *pullRow = new QHBoxLayout();
    m_pullModel = new QComboBox(page);
    m_pullModel->setEditable(true);
    m_pullModel->lineEdit()->setPlaceholderText(_t("Modell wählen oder Tag eintippen …"));
    const std::pair<const char *, QString> suggestions[] = {
        {"qwen2.5-coder:7b", _t("Spezialist für Code & Konfigurationsdateien (~5 GB)")},
        {"llama3.2",         _t("Klein & schnell, guter Allrounder (~2 GB)")},
        {"llama3.1:8b",      _t("Stark & vielseitig (~5 GB)")},
        {"mistral",          _t("Kompakt & solide (~4 GB)")},
        {"gemma2:9b",        _t("Google-Modell, leistungsfähig (~5 GB)")},
        {"phi3:mini",        _t("Sehr klein und schnell (~2 GB)")},
        {"qwen2.5:7b",       _t("Guter Allrounder (~5 GB)")},
    };
    for (const auto &[tag, hint] : suggestions) {
        m_pullModel->addItem(QString::fromLatin1(tag));
        m_pullModel->setItemData(m_pullModel->count() - 1, hint, Qt::ToolTipRole);
    }
    m_pullModel->setCurrentText(QString());   // keine Vorauswahl
    m_pullButton = new QPushButton(_t("Modell laden"), page);
    m_pullButton->setEnabled(m_bridge != nullptr);
    connect(m_pullButton, &QPushButton::clicked, this, &SettingsDialog::startPull);
    pullRow->addWidget(m_pullModel, 1);
    pullRow->addWidget(m_pullButton);
    form->addRow(_t("Modell laden"), pullRow);

    m_pullProgress = new QProgressBar(page);
    m_pullProgress->setVisible(false);
    form->addRow(QString(), m_pullProgress);

    auto *privacy = new QLabel(
        _t("Modelle werden lokal über Ollama ausgeführt — Inhalte verlassen den Rechner nicht."),
        page);
    privacy->setObjectName(QStringLiteral("Muted"));
    privacy->setWordWrap(true);
    form->addRow(privacy);
    return page;
}

void SettingsDialog::startPull()
{
    if (!m_bridge)
        return;
    const QString model = m_pullModel->currentText().trimmed();
    if (model.isEmpty())
        return;
    const QString baseUrl = m_aiUrl->text().trimmed();
    m_pullButton->setEnabled(false);
    m_pullProgress->setVisible(true);
    m_pullProgress->setRange(0, 0);   // unbestimmt, bis Groessen bekannt sind
    m_aiStatus->setText(_t("Lade %1 …").arg(model));

    // Die Rueckmeldungen laufen ueber die Bridge (Kontext ist NICHT dieser
    // Dialog). Schliesst der Nutzer das Fenster waehrend des Downloads, muss
    // jeder Rueckweg pruefen, ob es den Dialog ueberhaupt noch gibt.
    QPointer<SettingsDialog> self(this);
    m_pullTask = m_bridge->stream(
        [baseUrl, model](const AsyncBridge::EmitLine &emitLine, const CancelTokenPtr &cancel) {
            core::pullStream(baseUrl, model, emitLine, cancel);
        },
        [self](const QString &line) {
            if (!self)
                return;
            const core::PullProgress p = core::parsePullLine(line);
            if (p.total > 0) {
                self->m_pullProgress->setRange(0, 100);
                self->m_pullProgress->setValue(int(p.completed * 100 / p.total));
            }
            if (!p.status.isEmpty())
                self->m_aiStatus->setText(p.status);
        },
        [self, model] {
            if (!self)
                return;
            self->m_pullTask = nullptr;
            self->m_pullProgress->setVisible(false);
            self->m_pullButton->setEnabled(true);
            self->m_aiStatus->setText(_t("✓ %1 geladen.").arg(model));
            self->loadOllamaModels();       // frisch geladenes Modell in die Auswahl
        },
        [self](const QString &err) {
            if (!self)
                return;
            self->m_pullTask = nullptr;
            self->m_pullProgress->setVisible(false);
            self->m_pullButton->setEnabled(true);
            self->m_aiStatus->setText(QStringLiteral("✗ %1").arg(err));
        });
}

void SettingsDialog::updateExecColorButton()
{
    // Farbe direkt auf der Schaltflaeche zeigen — sonst raet man beim Waehlen.
    m_execColorBtn->setStyleSheet(
        QStringLiteral("QPushButton { border: 1px solid palette(mid); background: %1; }")
            .arg(m_execColor));
}

void SettingsDialog::pickExecColor()
{
    const QColor chosen = QColorDialog::getColor(QColor(m_execColor), this,
                                                 _t("Farbe für ausführbare Dateien"));
    if (!chosen.isValid())
        return;
    m_execColor = chosen.name();
    updateExecColorButton();
}

void SettingsDialog::deleteTheme()
{
    const QString name = m_theme->currentText();
    if (isBuiltin(name)) {
        QMessageBox::information(this, _t("Theme löschen"),
                                 _t("Nur eigene Farbschemata lassen sich löschen."));
        return;
    }
    if (QMessageBox::question(this, _t("Theme löschen"),
                              _t("Theme „%1“ löschen?").arg(name)) != QMessageBox::Yes)
        return;
    deleteCustomTheme(name);
    reloadThemes();
}

void SettingsDialog::reloadThemes()
{
    const QString keep = m_theme->currentText();
    m_theme->clear();
    m_theme->addItems(themeNames());
    m_theme->setCurrentText(m_theme->findText(keep) >= 0 ? keep : defaultTheme());
}

// --- Konfiguration im-/exportieren -----------------------------------------

void SettingsDialog::exportConfig()
{
    const QString path = getSaveFileName(this, _t("Konfiguration exportieren"),
                                         QStringLiteral("sshit-config.json"),
                                         _t("JSON-Dateien (*.json)"));
    if (path.isEmpty())
        return;
    try {
        core::writeExport(path);
        QMessageBox::information(
            this, _t("Konfiguration exportieren"),
            _t("Konfiguration exportiert nach:\n%1\n\nHinweis: Passwörter und Token bleiben "
               "im Schlüsselbund und werden NICHT exportiert.").arg(path));
    } catch (const std::exception &exc) {
        QMessageBox::warning(this, _t("Export fehlgeschlagen"), QString::fromUtf8(exc.what()));
    }
}

QStringList SettingsDialog::chooseImportSections(const QStringList &available)
{
    QDialog dlg(this);
    dlg.setWindowTitle(_t("Was importieren?"));
    auto *layout = new QVBoxLayout(&dlg);
    auto *hint = new QLabel(_t("Bestehende Daten der gewählten Bereiche werden überschrieben:"),
                            &dlg);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    std::vector<QCheckBox *> boxes;
    for (const QString &section : available) {
        auto *box = new QCheckBox(section, &dlg);
        box->setChecked(true);
        layout->addWidget(box);
        boxes.push_back(box);
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(buttons);
    if (dlg.exec() != QDialog::Accepted)
        return {};
    QStringList chosen;
    for (size_t i = 0; i < boxes.size(); ++i) {
        if (boxes[i]->isChecked())
            chosen << available.at(int(i));
    }
    return chosen;
}

void SettingsDialog::importConfig()
{
    const QString path = getOpenFileName(this, _t("Konfiguration importieren"), QString(),
                                         _t("JSON-Dateien (*.json)"));
    if (path.isEmpty())
        return;
    try {
        const QJsonObject bundle = core::readBundle(path);
        const QStringList available = core::availableSections(bundle);
        if (available.isEmpty()) {
            QMessageBox::information(this, _t("Konfiguration importieren"),
                                     _t("Die Datei enthält keine Daten."));
            return;
        }
        const QStringList chosen = chooseImportSections(available);
        if (chosen.isEmpty())
            return;
        const QStringList applied = core::applyBundle(bundle, chosen);
        QMessageBox::information(
            this, _t("Konfiguration importieren"),
            _t("Übernommen: %1\n\nDie Anwendung sollte neu gestartet werden.")
                .arg(applied.join(QStringLiteral(", "))));
    } catch (const std::exception &exc) {
        QMessageBox::warning(this, _t("Import fehlgeschlagen"), QString::fromUtf8(exc.what()));
    }
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
    m_aiStatus->setText(_t("Teste Verbindung …"));
    try {
        const QString v = net::version(m_aiUrl->text().trimmed());
        m_aiStatus->setText(_t("✓ Verbunden mit Ollama %1").arg(v));
    } catch (const std::exception &exc) {
        Q_UNUSED(exc);
        m_aiStatus->setText(
            _t("✗ Ollama nicht erreichbar. Dienst starten oder von ollama.com installieren."));
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
        if (models.empty())
            m_aiStatus->setText(_t("Verbunden, aber kein Modell installiert — unten laden."));
        else
            m_aiStatus->setText(_t("✓ %1 Modell(e) verfügbar.").arg(models.size()));
    } catch (const std::exception &exc) {
        Q_UNUSED(exc);
        m_aiStatus->setText(
            _t("✗ Ollama nicht erreichbar. Dienst starten oder von ollama.com installieren."));
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

    // Sprachwechsel merken (wird erst nach Neustart wirksam).
    const QString oldLang =
        core::getSettingString(QStringLiteral("language"), QStringLiteral("de"));
    const QString newLang = m_language->currentData().toString();

    core::setSetting(QStringLiteral("language"), m_language->currentData().toString());
    core::setSetting(QStringLiteral("theme"), m_theme->currentText());
    core::setSetting(QStringLiteral("editor_font_size"), m_editorFont->value());
    core::setSetting(QStringLiteral("terminal_font_size"), m_terminalFont->value());
    core::setSetting(QStringLiteral("pane_font_size"), m_paneFont->value());
    core::setSetting(QStringLiteral("date_format"), m_dateFormat->text());
    core::setSetting(QStringLiteral("hide_hidden"), m_hideHidden->isChecked());
    core::setSetting(QStringLiteral("show_file_icons"), m_showIcons->isChecked());
    core::setSetting(QStringLiteral("pane_thumbnails"), m_thumbnails->isChecked());
    core::setSetting(QStringLiteral("natural_sort"), m_naturalSort->isChecked());
    core::setSetting(QStringLiteral("compact_rows"), m_compactRows->isChecked());
    core::setSetting(QStringLiteral("exec_highlight"), m_execHighlight->isChecked());
    core::setSetting(QStringLiteral("exec_color"), m_execColor);
    core::setSetting(QStringLiteral("restore_tabs"), m_restoreTabs->isChecked());
    core::setSetting(QStringLiteral("auto_connect_last"), m_autoConnect->isChecked());
    core::setSetting(QStringLiteral("start_path"), m_startPath->text());
    core::setSetting(QString::fromLatin1(core::AI_ENABLED), m_aiEnabled->isChecked());
    core::setSetting(QString::fromLatin1(core::OLLAMA_URL), m_aiUrl->text().trimmed());
    core::setSetting(QString::fromLatin1(core::AI_MODEL),
                     m_aiModel->currentText().trimmed());
    core::saveShortcuts(mapping);

    // Sprache geaendert? Dann ist ein Neustart noetig — fragen und ggf. sofort
    // neu starten. Die Einstellung ist bereits atomar gespeichert, die neue
    // Instanz liest also die neue Sprache.
    if (oldLang != newLang) {
        const auto choice = QMessageBox::question(
            this, _t("Sprache geändert"),
            _t("Die neue Sprache wird erst nach einem Neustart wirksam.\n\n"
               "Jetzt neu starten?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        accept();
        if (choice == QMessageBox::Yes) {
            // Neue Instanz starten (ohne Programmpfad-Argument), dann beenden.
            QProcess::startDetached(QApplication::applicationFilePath(),
                                    QApplication::arguments().mid(1));
            QApplication::quit();
        }
        return;
    }
    accept();
}

} // namespace ncssh::gui
