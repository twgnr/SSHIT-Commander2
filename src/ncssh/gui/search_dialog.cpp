#include "ncssh/gui/search_dialog.hpp"

#include "ncssh/core/i18n.hpp"
#include "ncssh/core/search.hpp"

#include <QCheckBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;

SearchDialog::SearchDialog(AsyncBridge *bridge, const QString &mode,
                           const QString &startPath, QWidget *parent)
    : QDialog(parent), m_bridge(bridge), m_mode(mode)
{
    const bool content = (mode == QLatin1String("content"));
    setWindowTitle(content ? _t("Inhalts-Suche (grep)") : _t("Datei-Suche (Name)"));
    resize(880, 600);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout();

    auto *rootRow = new QHBoxLayout();
    m_root = new QLineEdit(startPath, this);
    auto *browse = new QPushButton(QStringLiteral("…"), this);
    browse->setFixedWidth(34);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, _t("Startordner"),
                                                            m_root->text());
        if (!d.isEmpty())
            m_root->setText(d);
    });
    rootRow->addWidget(m_root, 1);
    rootRow->addWidget(browse);
    form->addRow(_t("Startordner"), rootRow);

    m_pattern = new QLineEdit(this);
    m_pattern->setPlaceholderText(content ? _t("Suchbegriff …") : _t("Dateiname / Muster (*.log) …"));
    connect(m_pattern, &QLineEdit::returnPressed, this, &SearchDialog::startSearch);
    form->addRow(content ? _t("Suchbegriff") : _t("Muster"), m_pattern);

    m_include = new QLineEdit(this);
    m_include->setPlaceholderText(QStringLiteral("*.py,*.txt"));
    form->addRow(_t("Nur Dateien (Globs)"), m_include);

    m_excludeDir = new QLineEdit(this);
    m_excludeDir->setPlaceholderText(QStringLiteral(".git,node_modules"));
    form->addRow(_t("Ordner ausschließen"), m_excludeDir);

    auto *optRow = new QHBoxLayout();
    m_regex = new QCheckBox(_t("Regex"), this);
    m_ignoreCase = new QCheckBox(_t("Groß/Klein egal"), this);
    m_ignoreCase->setChecked(true);
    m_wholeWord = new QCheckBox(_t("Ganzes Wort"), this);
    m_binary = new QCheckBox(_t("Binärdateien"), this);
    optRow->addWidget(m_regex);
    optRow->addWidget(m_ignoreCase);
    if (content) {
        optRow->addWidget(m_wholeWord);
        optRow->addWidget(m_binary);
    }
    optRow->addStretch(1);
    if (content) {
        optRow->addWidget(new QLabel(_t("Kontext"), this));
        m_context = new QSpinBox(this);
        m_context->setRange(0, 20);
        optRow->addWidget(m_context);
    }
    form->addRow(_t("Optionen"), optRow);
    layout->addLayout(form);

    m_results = new QListWidget(this);
    connect(m_results, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        // "pfad:zeile:text" -> reiner Pfad
        m_chosenPath = core::stripMatchLocation(item->text());
        if (m_chosenPath.isEmpty())
            m_chosenPath = item->text();
        accept();
    });
    layout->addWidget(m_results, 1);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("Muted"));
    layout->addWidget(m_status);

    auto *buttons = new QHBoxLayout();
    m_startBtn = new QPushButton(_t("Suchen"), this);
    m_startBtn->setDefault(true);
    auto *stopBtn = new QPushButton(_t("Stopp"), this);
    auto *closeBtn = new QPushButton(_t("Schließen"), this);
    connect(m_startBtn, &QPushButton::clicked, this, &SearchDialog::startSearch);
    connect(stopBtn, &QPushButton::clicked, this, &SearchDialog::stopSearch);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addWidget(m_startBtn);
    buttons->addWidget(stopBtn);
    buttons->addStretch(1);
    buttons->addWidget(closeBtn);
    layout->addLayout(buttons);
}

core::SearchOptions SearchDialog::collectOptions() const
{
    core::SearchOptions opts;
    opts.pattern = m_pattern->text();
    opts.mode = m_mode;
    opts.regex = m_regex->isChecked();
    opts.ignoreCase = m_ignoreCase->isChecked();
    opts.wholeWord = m_wholeWord->isChecked();
    opts.includeBinary = m_binary->isChecked();
    opts.context = m_context ? m_context->value() : 0;
    const auto split = [](const QString &s) {
        QStringList out;
        for (const QString &p : s.split(QLatin1Char(','))) {
            if (!p.trimmed().isEmpty())
                out << p.trimmed();
        }
        return out;
    };
    opts.include = split(m_include->text());
    opts.excludeDir = split(m_excludeDir->text());
    return opts;
}

void SearchDialog::startSearch()
{
    if (m_task) {
        m_bridge->cancel(m_task);
        m_task = nullptr;
    }
    m_results->clear();
    const QString root = m_root->text();
    const core::SearchOptions opts = collectOptions();
    if (opts.pattern.isEmpty() || root.isEmpty())
        return;
    m_status->setText(_t("Suche läuft …"));
    m_startBtn->setEnabled(false);

    m_task = m_bridge->stream(
        [root, opts](const AsyncBridge::EmitLine &emitLine, const CancelTokenPtr &cancel) {
            core::iterSearch(root, opts, [&emitLine](const QString &hit) { emitLine(hit); },
                             cancel);
        },
        [this](const QString &hit) {
            m_results->addItem(hit);
            m_status->setText(QStringLiteral("%1 Treffer").arg(m_results->count()));
        },
        [this] {
            m_status->setText(QStringLiteral("Fertig — %1 Treffer").arg(m_results->count()));
            m_startBtn->setEnabled(true);
            m_task = nullptr;
        },
        [this](const QString &err) {
            m_status->setText(err);
            m_startBtn->setEnabled(true);
            m_task = nullptr;
        });
}

void SearchDialog::stopSearch()
{
    if (m_task) {
        m_bridge->cancel(m_task);
        m_status->setText(_t("Abgebrochen."));
        m_startBtn->setEnabled(true);
        m_task = nullptr;
    }
}

} // namespace ncssh::gui
