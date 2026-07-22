#include "ncssh/gui/plugins_dialog.hpp"

#include "ncssh/core/i18n.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;
using core::plugins::Plugin;

PluginsDialog::PluginsDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(_t("Plugins"));
    resize(760, 480);

    auto *root = new QHBoxLayout(this);

    auto *left = new QVBoxLayout();
    left->addWidget(new QLabel(_t("Plugins"), this));
    m_list = new QListWidget(this);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && row < int(m_plugins.size()))
            loadIntoForm(m_plugins[row]);
    });
    left->addWidget(m_list, 1);
    auto *hint = new QLabel(
        QStringLiteral("%1\n%2").arg(_t("Ablage:"), core::plugins::pluginsDir()), this);
    hint->setObjectName(QStringLiteral("Muted"));
    hint->setWordWrap(true);
    left->addWidget(hint);
    root->addLayout(left, 1);

    auto *form = new QFormLayout();
    m_name = new QLineEdit(this);
    auto *exeRow = new QHBoxLayout();
    m_exe = new QLineEdit(this);
    m_exe->setPlaceholderText(_t("Programm (relativ zum Plugin-Ordner oder absolut)"));
    auto *browse = new QPushButton(QStringLiteral("…"), this);
    browse->setFixedWidth(34);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, _t("Programm wählen"),
                                                       core::plugins::pluginsDir());
        if (!f.isEmpty())
            m_exe->setText(f);
    });
    exeRow->addWidget(m_exe, 1);
    exeRow->addWidget(browse);

    m_args = new QLineEdit(this);
    m_args->setPlaceholderText(QStringLiteral("--input \"{path}\""));
    m_workingDir = new QLineEdit(this);
    m_context = new QCheckBox(_t("Im Kontextmenü anzeigen"), this);
    m_targets = new QComboBox(this);
    for (const auto &[label, value] : core::plugins::targetLabels())
        m_targets->addItem(label, value);

    form->addRow(_t("Name"), m_name);
    form->addRow(_t("Programm"), exeRow);
    form->addRow(_t("Parameter"), m_args);
    form->addRow(_t("Arbeitsverzeichnis"), m_workingDir);
    form->addRow(QString(), m_context);
    form->addRow(_t("Gilt für"), m_targets);

    auto *right = new QVBoxLayout();
    right->addLayout(form);
    auto *placeholderHint = new QLabel(
        _t("Platzhalter {path} wird durch das gewählte Element ersetzt."), this);
    placeholderHint->setObjectName(QStringLiteral("Muted"));
    placeholderHint->setWordWrap(true);
    right->addWidget(placeholderHint);
    right->addStretch(1);

    auto *buttons = new QHBoxLayout();
    auto *newBtn = new QPushButton(_t("Neu"), this);
    auto *saveBtn = new QPushButton(_t("Speichern"), this);
    auto *delBtn = new QPushButton(_t("Löschen"), this);
    auto *launchBtn = new QPushButton(_t("Testen"), this);
    auto *closeBtn = new QPushButton(_t("Schließen"), this);
    closeBtn->setDefault(true);
    connect(newBtn, &QPushButton::clicked, this, [this] {
        m_currentId = 0;
        m_list->clearSelection();
        m_name->clear();
        m_exe->clear();
        m_args->clear();
        m_workingDir->clear();
        m_context->setChecked(false);
    });
    connect(saveBtn, &QPushButton::clicked, this, &PluginsDialog::onSave);
    connect(delBtn, &QPushButton::clicked, this, &PluginsDialog::onDelete);
    connect(launchBtn, &QPushButton::clicked, this, &PluginsDialog::onLaunch);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(newBtn);
    buttons->addWidget(saveBtn);
    buttons->addWidget(delBtn);
    buttons->addWidget(launchBtn);
    buttons->addStretch(1);
    buttons->addWidget(closeBtn);
    right->addLayout(buttons);
    root->addLayout(right, 2);

    reload();
}

void PluginsDialog::reload()
{
    m_plugins = core::plugins::loadAll();
    m_list->clear();
    for (const Plugin &p : m_plugins) {
        const QString label = p.bundled ? p.name + QStringLiteral("  (zentral)") : p.name;
        m_list->addItem(label.isEmpty() ? p.exe : label);
    }
}

void PluginsDialog::loadIntoForm(const Plugin &plugin)
{
    m_currentId = plugin.id;
    m_name->setText(plugin.name);
    m_exe->setText(plugin.exe);
    m_args->setText(plugin.args);
    m_workingDir->setText(plugin.workingDir);
    m_context->setChecked(plugin.context);
    m_targets->setCurrentIndex(m_targets->findData(plugin.targets));
    // Zentral bereitgestellte Plugins sind nicht editierbar.
    const bool editable = !plugin.bundled;
    for (QLineEdit *w : {m_name, m_exe, m_args, m_workingDir})
        w->setEnabled(editable);
    m_context->setEnabled(editable);
    m_targets->setEnabled(editable);
}

Plugin PluginsDialog::formToPlugin() const
{
    Plugin p;
    p.id = m_currentId;
    p.name = m_name->text().trimmed();
    p.exe = m_exe->text().trimmed();
    p.args = m_args->text();
    p.workingDir = m_workingDir->text().trimmed();
    p.context = m_context->isChecked();
    p.targets = m_targets->currentData().toString();
    return p;
}

void PluginsDialog::onSave()
{
    Plugin edited = formToPlugin();
    if (edited.exe.isEmpty()) {
        QMessageBox::warning(this, _t("Fehler"), _t("Kein Programm angegeben."));
        return;
    }
    // Nur eigene Plugins speichern (zentrale bleiben unangetastet).
    std::vector<Plugin> own = core::plugins::load();
    bool found = false;
    for (Plugin &p : own) {
        if (p.id == edited.id && edited.id != 0) {
            p = edited;
            found = true;
            break;
        }
    }
    if (!found) {
        edited.id = core::plugins::nextId(own);
        own.push_back(edited);
    }
    try {
        core::plugins::save(own);
        reload();
    } catch (const std::exception &exc) {
        QMessageBox::warning(this, _t("Fehler"), QString::fromUtf8(exc.what()));
    }
}

void PluginsDialog::onDelete()
{
    if (m_currentId == 0)
        return;
    std::vector<Plugin> own = core::plugins::load();
    const auto before = own.size();
    own.erase(std::remove_if(own.begin(), own.end(),
                             [this](const Plugin &p) { return p.id == m_currentId; }),
              own.end());
    if (own.size() == before)
        return;  // zentrales Plugin -> nicht loeschbar
    core::plugins::save(own);
    m_currentId = 0;
    reload();
}

void PluginsDialog::onLaunch()
{
    try {
        core::plugins::launch(formToPlugin());
    } catch (const std::exception &exc) {
        QMessageBox::warning(this, _t("Start fehlgeschlagen"), QString::fromUtf8(exc.what()));
    }
}

} // namespace ncssh::gui
