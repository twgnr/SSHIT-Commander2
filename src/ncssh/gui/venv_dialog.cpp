#include "ncssh/gui/venv_dialog.hpp"

#include "ncssh/core/i18n.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include "ncssh/gui/file_dialogs.hpp"
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;
using core::VenvInfo;

VenvDialog::VenvDialog(AsyncBridge *bridge, const QString &projectDir, const QString &osType,
                       QWidget *parent)
    : QDialog(parent), m_bridge(bridge), m_projectDir(projectDir), m_osType(osType)
{
    setWindowTitle(_t("venv verwalten"));
    resize(860, 640);

    auto *layout = new QVBoxLayout(this);

    // --- Neue Umgebung ---
    auto *setupBox = new QGroupBox(_t("Neue Umgebung:"), this);
    auto *form = new QFormLayout(setupBox);

    auto *projRow = new QHBoxLayout();
    m_project = new QLineEdit(projectDir, setupBox);
    m_project->setPlaceholderText(_t("Ordner mit pyproject.toml / requirements.txt …"));
    auto *browse = new QPushButton(QStringLiteral("…"), setupBox);
    browse->setFixedWidth(34);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString d = getExistingDirectory(this, _t("Projektordner wählen"),
                                                            m_project->text());
        if (!d.isEmpty()) {
            m_project->setText(d);
            m_install->setText(core::detectInstall(d));
            updatePreview();
        }
    });
    projRow->addWidget(m_project, 1);
    projRow->addWidget(browse);
    form->addRow(_t("Projektordner"), projRow);

    m_venvDir = new QLineEdit(QStringLiteral(".venv"), setupBox);
    m_venvDir->setPlaceholderText(_t("Zielordner der virtuellen Umgebung"));
    form->addRow(_t("venv-Pfad:"), m_venvDir);

    m_python = new QComboBox(setupBox);
    for (const auto &[label, command] : core::discoverPythons())
        m_python->addItem(label, command);
    form->addRow(_t("Python"), m_python);

    m_install = new QLineEdit(core::detectInstall(projectDir), setupBox);
    m_install->setPlaceholderText(_t("z. B. pip install -r requirements.txt (leer = keine)"));
    form->addRow(_t("Installation"), m_install);

    m_skipLock = new QCheckBox(
        _t("Abhängigkeiten ignorieren (pipenv --skip-lock / pip --no-deps)"), setupBox);
    form->addRow(QString(), m_skipLock);
    auto *installHint = new QLabel(_t("Wird nach dem Aktivieren der venv ausgeführt."),
                                   setupBox);
    installHint->setObjectName(QStringLiteral("Muted"));
    form->addRow(QString(), installHint);
    layout->addWidget(setupBox);

    layout->addWidget(new QLabel(_t("Befehlsvorschau"), this));
    m_preview = new QPlainTextEdit(this);
    m_preview->setReadOnly(true);
    m_preview->setMaximumHeight(110);
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    m_preview->setFont(mono);
    layout->addWidget(m_preview);

    // --- Bekannte Umgebungen ---
    layout->addWidget(new QLabel(_t("Bekannte Umgebungen (venv/pipenv):"), this));
    m_table = new QTableWidget(0, 5, this);
    m_table->setHorizontalHeaderLabels({_t("Name"), _t("Typ"), _t("Version"), _t("Projekt"),
                                        _t("Pfad")});
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    // Doppelklick aktiviert die Umgebung in der Konsole.
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        const QString path = m_table->item(row, 4)->text();
        m_commands = QStringList{core::activateCommand(m_osType, path)};
        QDialog::accept();
    });
    // Auswahl zeigt die hinterlegte Notiz zur Umgebung.
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] {
        const int row = m_table->currentRow();
        if (row < 0 || row >= int(m_envs.size())) {
            m_note->clear();
            m_noteProject->setText(_t("(kein Projekt hinterlegt)"));
            return;
        }
        m_note->setPlainText(m_envs[size_t(row)].info);
        const QString project = m_envs[size_t(row)].project;
        m_noteProject->setText(project.isEmpty() ? _t("(kein Projekt hinterlegt)") : project);
    });
    layout->addWidget(m_table, 1);

    // --- Notiz zur ausgewaehlten Umgebung ---
    auto *noteBox = new QGroupBox(_t("Notiz zur ausgewählten Umgebung"), this);
    auto *noteLayout = new QVBoxLayout(noteBox);
    m_noteProject = new QLabel(_t("(kein Projekt hinterlegt)"), noteBox);
    m_noteProject->setObjectName(QStringLiteral("Muted"));
    noteLayout->addWidget(m_noteProject);
    m_note = new QPlainTextEdit(noteBox);
    m_note->setPlaceholderText(_t("Kurze Notiz zu dieser Umgebung (optional)"));
    m_note->setMaximumHeight(70);
    noteLayout->addWidget(m_note);
    auto *saveNote = new QPushButton(_t("Info speichern"), noteBox);
    connect(saveNote, &QPushButton::clicked, this, &VenvDialog::saveNote);
    noteLayout->addWidget(saveNote, 0, Qt::AlignLeft);
    layout->addWidget(noteBox);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("Muted"));
    layout->addWidget(m_status);

    auto *buttons = new QHBoxLayout();
    auto *deleteBtn = new QPushButton(_t("Umgebung löschen"), this);
    auto *activateBtn = new QPushButton(_t("Aktivieren"), this);
    auto *cancel = new QPushButton(_t("Abbrechen"), this);
    auto *createBtn = new QPushButton(_t("Erstellen & aktivieren"), this);
    createBtn->setDefault(true);
    connect(deleteBtn, &QPushButton::clicked, this, &VenvDialog::deleteSelected);
    connect(activateBtn, &QPushButton::clicked, this, &VenvDialog::activateSelected);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(createBtn, &QPushButton::clicked, this, &VenvDialog::accept);
    buttons->addWidget(deleteBtn);
    buttons->addWidget(activateBtn);
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(createBtn);
    layout->addLayout(buttons);

    for (QLineEdit *e : {m_project, m_venvDir, m_install})
        connect(e, &QLineEdit::textChanged, this, &VenvDialog::updatePreview);
    connect(m_python, &QComboBox::currentIndexChanged, this, &VenvDialog::updatePreview);
    connect(m_skipLock, &QCheckBox::toggled, this, &VenvDialog::updatePreview);

    updatePreview();
    reloadEnvs();
}

void VenvDialog::updatePreview()
{
    const QString install = core::applySkip(m_install->text(), m_skipLock->isChecked());
    const auto cmds = core::setupCommands(m_osType, m_project->text(), m_venvDir->text(),
                                          m_python->currentData().toString(), install);
    QStringList lines;
    for (const QString &c : cmds)
        lines << c;
    m_preview->setPlainText(lines.join(QLatin1Char('\n')));
}

void VenvDialog::reloadEnvs()
{
    m_bridge->run<std::vector<VenvInfo>>(
        [] { return core::discover(); },
        [this](const std::vector<VenvInfo> &envs) {
            m_envs = envs;
            m_table->setRowCount(0);
            int row = 0;
            for (const VenvInfo &env : envs) {
                m_table->insertRow(row);
                m_table->setItem(row, 0, new QTableWidgetItem(env.name));
                m_table->setItem(row, 1, new QTableWidgetItem(env.kind));
                m_table->setItem(row, 2, new QTableWidgetItem(env.version));
                m_table->setItem(row, 3, new QTableWidgetItem(env.project));
                m_table->setItem(row, 4, new QTableWidgetItem(env.path));
                ++row;
            }
            m_status->setText(QStringLiteral("%1 Umgebung(en) gefunden · Doppelklick aktiviert")
                                  .arg(envs.size()));
        },
        [this](const QString &err) { m_status->setText(err); });
}

void VenvDialog::deleteSelected()
{
    const int row = m_table->currentRow();
    if (row < 0)
        return;
    const QString path = m_table->item(row, 4)->text();
    if (QMessageBox::question(
            this, _t("Löschen"),
            _t("Umgebung wirklich löschen (Ordner wird entfernt)?\n%1").arg(path))
        != QMessageBox::Yes)
        return;
    m_bridge->run(
        [path] {
            core::deleteEnvMeta(path);
            QDir(path).removeRecursively();
        },
        [this] { reloadEnvs(); },
        [this](const QString &err) { QMessageBox::warning(this, _t("Fehler"), err); });
}

void VenvDialog::activateSelected()
{
    const int row = m_table->currentRow();
    if (row < 0) {
        m_status->setText(_t("Bitte eine Umgebung in der Liste auswählen."));
        return;
    }
    m_commands = QStringList{core::activateCommand(m_osType, m_table->item(row, 4)->text())};
    QDialog::accept();
}

void VenvDialog::saveNote()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= int(m_envs.size())) {
        m_status->setText(_t("Bitte eine Umgebung in der Liste auswählen."));
        return;
    }
    const QString path = m_envs[size_t(row)].path;
    core::setEnvInfo(path, m_note->toPlainText());
    m_envs[size_t(row)].info = m_note->toPlainText();
    m_status->setText(_t("Info gespeichert."));
}

void VenvDialog::accept()
{
    // Ohne gueltiges Projekt bzw. Zielordner ergaeben die Befehle keinen Sinn.
    if (m_project->text().trimmed().isEmpty() || !QDir(m_project->text()).exists()) {
        QMessageBox::warning(this, _t("Info"),
                             _t("Bitte einen gültigen Projektordner wählen."));
        return;
    }
    if (m_venvDir->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, _t("Info"), _t("Bitte einen venv-Zielordner angeben."));
        return;
    }
    const QString install = core::applySkip(m_install->text(), m_skipLock->isChecked());
    const auto cmds = core::setupCommands(m_osType, m_project->text(), m_venvDir->text(),
                                          m_python->currentData().toString(), install);
    m_commands.clear();
    for (const QString &c : cmds)
        m_commands << c;
    QDialog::accept();
}

} // namespace ncssh::gui
