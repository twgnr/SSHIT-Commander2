#include "ncssh/gui/search_dialog.hpp"

#include "ncssh/core/i18n.hpp"
#include "ncssh/core/search.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include "ncssh/gui/file_dialogs.hpp"
#include <QFormLayout>
#include <QStorageInfo>
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
        const QString d = getExistingDirectory(this, _t("Startordner"),
                                                            m_root->text());
        if (!d.isEmpty())
            m_root->setText(d);
    });
    // Ganzes Laufwerk durchsuchen, ohne den Pfad von Hand zu kuerzen.
    auto *rootUp = new QPushButton(QStringLiteral("⌂"), this);
    rootUp->setFixedWidth(30);
    rootUp->setToolTip(_t("Auf Laufwerks-/Systemwurzel setzen"));
    connect(rootUp, &QPushButton::clicked, this, [this] {
        const QString current = QDir::cleanPath(m_root->text());
        const QString root = QDir(current).isRoot()
                                 ? current
                                 : QDir::rootPath();
        const QStorageInfo info(current);
        m_root->setText(info.isValid() ? info.rootPath() : root);
    });
    rootRow->addWidget(m_root, 1);
    rootRow->addWidget(rootUp);
    rootRow->addWidget(browse);
    form->addRow(_t("Root"), rootRow);

    m_pattern = new QLineEdit(this);
    m_pattern->setPlaceholderText(content ? _t("Text im Datei-Inhalt")
                                          : _t("Name/Muster, z.B. *.log"));
    connect(m_pattern, &QLineEdit::returnPressed, this, &SearchDialog::startSearch);
    form->addRow(content ? _t("Inhalt (grep)") : _t("Dateiname"), m_pattern);

    m_include = new QLineEdit(this);
    m_include->setPlaceholderText(_t("z.B. *.py,*.txt"));
    form->addRow(_t("Datei-Filter"), m_include);

    auto *optRow = new QHBoxLayout();
    m_regex = new QCheckBox(content ? _t("Regex im Datei-Inhalt")
                                    : _t("Regex, z.B. ^conf.*\\.ya?ml$"), this);
    m_ignoreCase = new QCheckBox(_t("Groß/klein ignorieren"), this);
    m_ignoreCase->setChecked(true);
    m_wholeWord = new QCheckBox(_t("Ganzes Wort"), this);
    m_binary = new QCheckBox(_t("Binärdateien einbeziehen"), this);
    optRow->addWidget(m_regex);
    optRow->addWidget(m_ignoreCase);
    if (content) {
        optRow->addWidget(m_wholeWord);
        optRow->addWidget(m_binary);
    }
    optRow->addStretch(1);
    form->addRow(_t("Optionen"), optRow);

    // --- Erweitert (eingeklappt) ---------------------------------------------
    // Die Suchmaschine kann deutlich mehr, als die Grundmaske zeigt; die
    // selteneren Schalter stecken hier, damit der Dialog schlank bleibt.
    m_advancedButton = new QPushButton(_t("Erweitert ▾"), this);
    m_advancedButton->setFlat(true);
    m_advanced = new QWidget(this);
    m_advanced->setVisible(false);
    connect(m_advancedButton, &QPushButton::clicked, this, [this] {
        const bool show = !m_advanced->isVisible();
        m_advanced->setVisible(show);
        m_advancedButton->setText(show ? _t("Erweitert ▴") : _t("Erweitert ▾"));
    });
    auto *adv = new QFormLayout(m_advanced);

    m_exclude = new QLineEdit(m_advanced);
    m_exclude->setPlaceholderText(_t("z.B. *.min.js,*.map"));
    adv->addRow(_t("Dateien ausschließen"), m_exclude);

    m_excludeDir = new QLineEdit(m_advanced);
    m_excludeDir->setPlaceholderText(_t("z.B. .git,node_modules"));
    adv->addRow(_t("Ordner ausschließen"), m_excludeDir);

    auto *kindRow = new QHBoxLayout();
    m_kind = new QComboBox(m_advanced);
    m_kind->addItem(_t("Alles"), QStringLiteral("all"));
    m_kind->addItem(_t("Nur Dateien"), QStringLiteral("file"));
    m_kind->addItem(_t("Nur Ordner"), QStringLiteral("dir"));
    m_namesOnly = new QCheckBox(_t("Nur Dateinamen"), m_advanced);
    m_invert = new QCheckBox(_t("Zeilen ohne Treffer"), m_advanced);
    m_invert->setToolTip(_t("Invertiert"));
    kindRow->addWidget(m_kind);
    if (content) {
        kindRow->addWidget(m_namesOnly);
        kindRow->addWidget(m_invert);
    }
    kindRow->addStretch(1);
    adv->addRow(_t("Art"), kindRow);

    auto *limitRow = new QHBoxLayout();
    m_maxDepth = new QSpinBox(m_advanced);
    m_maxDepth->setRange(0, 99);
    m_maxDepth->setSpecialValueText(_t("∞"));   // 0 = unbegrenzt
    m_minSize = new QSpinBox(m_advanced);
    m_minSize->setRange(0, 1000000);
    m_minSize->setSuffix(QStringLiteral(" KB"));
    m_newerThan = new QSpinBox(m_advanced);
    m_newerThan->setRange(0, 3650);
    m_newerThan->setSpecialValueText(_t("∞"));
    limitRow->addWidget(new QLabel(_t("Max-Tiefe"), m_advanced));
    limitRow->addWidget(m_maxDepth);
    limitRow->addWidget(new QLabel(_t("Min-Größe (KB)"), m_advanced));
    limitRow->addWidget(m_minSize);
    limitRow->addWidget(new QLabel(_t("Geändert ≤ Tage"), m_advanced));
    limitRow->addWidget(m_newerThan);
    limitRow->addStretch(1);
    adv->addRow(_t("Grenzen"), limitRow);

    if (content) {
        m_context = new QSpinBox(m_advanced);
        m_context->setRange(0, 20);
        m_context->setToolTip(_t("Zeilen vor/nach jedem Treffer"));
        adv->addRow(_t("Zeilen vor/nach jedem Treffer"), m_context);
    }

    layout->addLayout(form);
    layout->addWidget(m_advancedButton);
    layout->addWidget(m_advanced);

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
    opts.exclude = split(m_exclude->text());
    opts.excludeDir = split(m_excludeDir->text());
    opts.kind = m_kind->currentData().toString();
    opts.namesOnly = m_namesOnly->isChecked();
    opts.invert = m_invert->isChecked();
    // 0 bedeutet in beiden Spinboxen "unbegrenzt" (Sonderwert ∞).
    if (m_maxDepth->value() > 0)
        opts.maxDepth = m_maxDepth->value();
    if (m_minSize->value() > 0)
        opts.minSize = qint64(m_minSize->value()) * 1024;
    if (m_newerThan->value() > 0)
        opts.newerThanDays = double(m_newerThan->value());
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
            m_status->setText(_t("%1 Treffer …").arg(m_results->count()));
        },
        [this, opts] {
            QString text = _t("%1 Treffer").arg(m_results->count());
            // Beim Limit ehrlich sagen, dass die Liste abgeschnitten ist.
            if (m_results->count() >= opts.limit)
                text += _t(" — max. %1 angezeigt").arg(opts.limit);
            m_status->setText(text);
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
        m_status->setText(_t("%1 Treffer").arg(m_results->count()) + _t(" (abgebrochen)"));
        m_startBtn->setEnabled(true);
        m_task = nullptr;
    }
}

} // namespace ncssh::gui
