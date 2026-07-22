#include "ncssh/gui/venv_dialog.hpp"

#include "ncssh/core/i18n.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
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
    auto *setupBox = new QGroupBox(_t("Neue Umgebung anlegen"), this);
    auto *form = new QFormLayout(setupBox);

    auto *projRow = new QHBoxLayout();
    m_project = new QLineEdit(projectDir, setupBox);
    auto *browse = new QPushButton(QStringLiteral("…"), setupBox);
    browse->setFixedWidth(34);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, _t("Projektordner"),
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
    form->addRow(_t("venv-Ordner"), m_venvDir);

    m_python = new QComboBox(setupBox);
    for (const auto &[label, command] : core::discoverPythons())
        m_python->addItem(label, command);
    form->addRow(_t("Python-Version"), m_python);

    m_install = new QLineEdit(core::detectInstall(projectDir), setupBox);
    m_install->setPlaceholderText(_t("z. B. pip install -r requirements.txt (leer = keine)"));
    form->addRow(_t("Installation"), m_install);

    m_skipLock = new QCheckBox(_t("--skip-lock / --no-deps"), setupBox);
    form->addRow(QString(), m_skipLock);
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
    layout->addWidget(new QLabel(_t("Bekannte Umgebungen"), this));
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
    layout->addWidget(m_table, 1);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("Muted"));
    layout->addWidget(m_status);

    auto *buttons = new QHBoxLayout();
    auto *deleteBtn = new QPushButton(_t("Umgebung löschen"), this);
    auto *cancel = new QPushButton(_t("Abbrechen"), this);
    auto *createBtn = new QPushButton(_t("Anlegen & aktivieren"), this);
    createBtn->setDefault(true);
    connect(deleteBtn, &QPushButton::clicked, this, &VenvDialog::deleteSelected);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(createBtn, &QPushButton::clicked, this, &VenvDialog::accept);
    buttons->addWidget(deleteBtn);
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
    if (QMessageBox::question(this, _t("Löschen"),
                              QStringLiteral("Umgebung wirklich löschen?\n%1").arg(path))
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

void VenvDialog::accept()
{
    const QString install = core::applySkip(m_install->text(), m_skipLock->isChecked());
    const auto cmds = core::setupCommands(m_osType, m_project->text(), m_venvDir->text(),
                                          m_python->currentData().toString(), install);
    m_commands.clear();
    for (const QString &c : cmds)
        m_commands << c;
    QDialog::accept();
}

} // namespace ncssh::gui
