#include "ncssh/gui/confirm_dialog.hpp"

#include "ncssh/core/i18n.hpp"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;

QPushButton *makeBookmarkButton(const QStringList &paths,
                                const std::function<void(const QString &)> &onPick,
                                QWidget *parent)
{
    auto *button = new QPushButton(QStringLiteral("★ ▾"), parent);
    button->setToolTip(_t("Lesezeichen"));
    auto *menu = new QMenu(button);
    for (const QString &path : paths)
        menu->addAction(path, button, [onPick, path] { onPick(path); });
    button->setMenu(menu);
    button->setEnabled(!paths.isEmpty());
    return button;
}

// ---------------------------------------------------------------------------
// PathConfirmDialog
// ---------------------------------------------------------------------------

PathConfirmDialog::PathConfirmDialog(const QString &title, const QString &intro,
                                     const std::vector<PathPair> &pairs,
                                     const Options &options, QWidget *parent)
    : QDialog(parent)
{
    const QString confirmText = options.confirmText.isEmpty() ? _t("Ausführen")
                                                              : options.confirmText;
    const QString sourceHeader = options.sourceHeader.isEmpty() ? _t("Quelle")
                                                                : options.sourceHeader;
    const QString targetHeader = options.targetHeader.isEmpty() ? _t("Ziel")
                                                                : options.targetHeader;
    setWindowTitle(title);
    resize(options.showTarget ? 760 : 560, 420);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(intro, this));

    const int cols = options.showTarget ? 2 : 1;
    auto *table = new QTableWidget(0, cols, this);
    table->setHorizontalHeaderLabels(options.showTarget
                                         ? QStringList{sourceHeader, targetHeader}
                                         : QStringList{sourceHeader});
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    for (int c = 0; c < cols; ++c)
        table->horizontalHeader()->setSectionResizeMode(c, QHeaderView::Stretch);
    for (const auto &[src, dst] : pairs) {
        const int row = table->rowCount();
        table->insertRow(row);
        table->setItem(row, 0, new QTableWidgetItem(src));
        if (options.showTarget)
            table->setItem(row, 1, new QTableWidgetItem(dst));
    }
    layout->addWidget(table);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    buttons->button(QDialogButtonBox::Ok)->setText(confirmText);
    buttons->button(QDialogButtonBox::Cancel)->setText(_t("Abbrechen"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

bool PathConfirmDialog::confirm(const QString &title, const QString &intro,
                                const std::vector<PathPair> &pairs,
                                const Options &options, QWidget *parent)
{
    PathConfirmDialog dlg(title, intro, pairs, options, parent);
    return dlg.exec() == QDialog::Accepted;
}

// ---------------------------------------------------------------------------
// TransferConfirmDialog
// ---------------------------------------------------------------------------

TransferConfirmDialog::TransferConfirmDialog(const QString &title, const QString &verb,
                                             const QStringList &names,
                                             const QStringList &sourcePaths,
                                             Joiner joiner, const QString &targetDir,
                                             BrowseFn onBrowse,
                                             const QStringList &bookmarks, QWidget *parent)
    : QDialog(parent), m_names(names), m_sources(sourcePaths),
      m_joiner(std::move(joiner)), m_onBrowse(std::move(onBrowse)),
      m_single(names.size() == 1)
{
    setWindowTitle(title);
    resize(820, 460);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(
        QStringLiteral("%1 Objekt(e) %2:").arg(m_names.size()).arg(verb.toLower()), this));

    // Ziel-Dateiname (nur bei genau einem Objekt umbenennbar)
    if (m_single) {
        auto *nameRow = new QHBoxLayout();
        nameRow->addWidget(new QLabel(_t("Name:"), this));
        m_nameEdit = new QLineEdit(m_names.first(), this);
        connect(m_nameEdit, &QLineEdit::textChanged, this, &TransferConfirmDialog::refresh);
        nameRow->addWidget(m_nameEdit, 1);
        layout->addLayout(nameRow);
    }

    auto *dirRow = new QHBoxLayout();
    dirRow->addWidget(new QLabel(_t("Zielordner:"), this));
    m_dirEdit = new QLineEdit(targetDir, this);
    connect(m_dirEdit, &QLineEdit::textChanged, this, &TransferConfirmDialog::refresh);
    dirRow->addWidget(m_dirEdit, 1);
    dirRow->addWidget(makeBookmarkButton(
        bookmarks, [this](const QString &p) { m_dirEdit->setText(p); }, this));
    auto *browseBtn = new QPushButton(_t("Durchsuchen…"), this);
    connect(browseBtn, &QPushButton::clicked, this, [this] {
        if (!m_onBrowse)
            return;
        const QString chosen = m_onBrowse(m_dirEdit->text());
        if (!chosen.isEmpty())
            m_dirEdit->setText(chosen);
    });
    dirRow->addWidget(browseBtn);
    layout->addLayout(dirRow);

    m_table = new QTableWidget(0, 2, this);
    m_table->setHorizontalHeaderLabels({_t("Quelle"), _t("Ziel")});
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    layout->addWidget(m_table, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    buttons->button(QDialogButtonBox::Ok)->setText(verb);
    buttons->button(QDialogButtonBox::Cancel)->setText(_t("Abbrechen"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    refresh();
}

void TransferConfirmDialog::refresh()
{
    m_table->setRowCount(0);
    const QString dir = m_dirEdit->text();
    for (int i = 0; i < m_names.size(); ++i) {
        const QString name = (m_single && m_nameEdit) ? m_nameEdit->text() : m_names[i];
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setItem(row, 0, new QTableWidgetItem(m_sources.value(i)));
        m_table->setItem(row, 1, new QTableWidgetItem(
                                     m_joiner ? m_joiner(dir, name) : name));
    }
}

std::vector<PathPair> TransferConfirmDialog::results() const
{
    std::vector<PathPair> out;
    const QString dir = m_dirEdit->text();
    for (int i = 0; i < m_names.size(); ++i) {
        const QString name = (m_single && m_nameEdit) ? m_nameEdit->text() : m_names[i];
        out.emplace_back(m_sources.value(i), m_joiner ? m_joiner(dir, name) : name);
    }
    return out;
}

QString TransferConfirmDialog::targetDir() const
{
    return m_dirEdit->text();
}

} // namespace ncssh::gui
