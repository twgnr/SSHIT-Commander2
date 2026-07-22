#include "ncssh/gui/diff_dialog.hpp"

#include "ncssh/core/i18n.hpp"
#include "ncssh/gui/transfer_manager.hpp"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;
using core::DiffEntry;

static QString statusLabel(const QString &status)
{
    if (status == QLatin1String("left_only")) return _t("nur links");
    if (status == QLatin1String("right_only")) return _t("nur rechts");
    if (status == QLatin1String("newer_left")) return _t("links neuer");
    if (status == QLatin1String("newer_right")) return _t("rechts neuer");
    if (status == QLatin1String("same")) return _t("gleich");
    if (status == QLatin1String("dir")) return _t("Ordner");
    return status;
}

static QColor statusColor(const QString &status)
{
    if (status == QLatin1String("left_only")) return QColor(QStringLiteral("#4f8cff"));
    if (status == QLatin1String("right_only")) return QColor(QStringLiteral("#d19a66"));
    if (status.startsWith(QLatin1String("newer"))) return QColor(QStringLiteral("#3fb950"));
    return QColor(QStringLiteral("#8b90a0"));
}

DiffDialog::DiffDialog(AsyncBridge *bridge, TransferManager *transfers,
                       core::FileSystemProvider *left, const QString &leftPath,
                       core::FileSystemProvider *right, const QString &rightPath,
                       QWidget *parent)
    : QDialog(parent), m_bridge(bridge), m_transfers(transfers),
      m_left(left), m_leftPath(leftPath), m_right(right), m_rightPath(rightPath)
{
    setWindowTitle(_t("Verzeichnis-Vergleich"));
    resize(900, 600);

    auto *layout = new QVBoxLayout(this);
    auto *header = new QLabel(QStringLiteral("%1  ↔  %2").arg(leftPath, rightPath), this);
    header->setObjectName(QStringLiteral("Muted"));
    header->setWordWrap(true);
    layout->addWidget(header);

    auto *optRow = new QHBoxLayout();
    m_recursive = new QCheckBox(_t("rekursiv"), this);
    m_onlyDifferences = new QCheckBox(_t("nur Unterschiede"), this);
    m_onlyDifferences->setChecked(true);
    connect(m_recursive, &QCheckBox::toggled, this, &DiffDialog::compare);
    connect(m_onlyDifferences, &QCheckBox::toggled, this, &DiffDialog::compare);
    optRow->addWidget(m_recursive);
    optRow->addWidget(m_onlyDifferences);
    optRow->addStretch(1);
    layout->addLayout(optRow);

    m_table = new QTableWidget(0, 4, this);
    m_table->setHorizontalHeaderLabels({_t("Name"), _t("Links"), _t("Rechts"), _t("Status")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    layout->addWidget(m_table, 1);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("Muted"));
    layout->addWidget(m_status);

    auto *buttons = new QHBoxLayout();
    auto *toRight = new QPushButton(_t("→ nach rechts kopieren"), this);
    auto *toLeft = new QPushButton(_t("← nach links kopieren"), this);
    auto *refresh = new QPushButton(_t("Neu vergleichen"), this);
    auto *closeBtn = new QPushButton(_t("Schließen"), this);
    closeBtn->setDefault(true);
    connect(toRight, &QPushButton::clicked, this, [this] { copySelected(true); });
    connect(toLeft, &QPushButton::clicked, this, [this] { copySelected(false); });
    connect(refresh, &QPushButton::clicked, this, &DiffDialog::compare);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(toRight);
    buttons->addWidget(toLeft);
    buttons->addWidget(refresh);
    buttons->addStretch(1);
    buttons->addWidget(closeBtn);
    layout->addLayout(buttons);

    compare();
}

void DiffDialog::compare()
{
    m_status->setText(_t("Vergleiche …"));
    core::FileSystemProvider *left = m_left;
    core::FileSystemProvider *right = m_right;
    const QString leftPath = m_leftPath;
    const QString rightPath = m_rightPath;
    const bool recursive = m_recursive->isChecked();

    m_bridge->run<std::vector<DiffEntry>>(
        [left, leftPath, right, rightPath, recursive] {
            if (recursive)
                return core::compareRecursive(left, leftPath, right, rightPath);
            return core::compare(left->listDir(leftPath), right->listDir(rightPath));
        },
        [this](const std::vector<DiffEntry> &entries) {
            m_entries = entries;
            m_table->setRowCount(0);
            int shown = 0, diffs = 0;
            for (const DiffEntry &e : entries) {
                const bool isSame = (e.status == QLatin1String("same")
                                     || e.status == QLatin1String("dir"));
                if (!isSame)
                    ++diffs;
                if (m_onlyDifferences->isChecked() && isSame)
                    continue;
                m_table->insertRow(shown);
                auto *nameItem = new QTableWidgetItem(e.name);
                nameItem->setData(Qt::UserRole, e.name);
                m_table->setItem(shown, 0, nameItem);
                m_table->setItem(shown, 1, new QTableWidgetItem(
                                               e.left ? QString::number(e.left->size) : QString()));
                m_table->setItem(shown, 2, new QTableWidgetItem(
                                               e.right ? QString::number(e.right->size) : QString()));
                auto *statusItem = new QTableWidgetItem(statusLabel(e.status));
                statusItem->setForeground(statusColor(e.status));
                m_table->setItem(shown, 3, statusItem);
                ++shown;
            }
            m_status->setText(QStringLiteral("%1 Einträge · %2 Unterschiede")
                                  .arg(entries.size()).arg(diffs));
        },
        [this](const QString &err) { m_status->setText(err); });
}

void DiffDialog::copySelected(bool toRight)
{
    QSet<int> rows;
    for (auto *item : m_table->selectedItems())
        rows.insert(item->row());
    if (rows.isEmpty())
        return;

    core::FileSystemProvider *src = toRight ? m_left : m_right;
    core::FileSystemProvider *dst = toRight ? m_right : m_left;
    const QString srcRoot = toRight ? m_leftPath : m_rightPath;
    const QString dstRoot = toRight ? m_rightPath : m_leftPath;

    int queued = 0;
    for (int row : rows) {
        const QString name = m_table->item(row, 0)->data(Qt::UserRole).toString();
        // Bei rekursivem Vergleich ist name ein relativer Pfad mit '/'.
        QString srcPath = srcRoot;
        QString dstPath = dstRoot;
        for (const QString &part : name.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
            srcPath = src->join(srcPath, part);
            dstPath = dst->join(dstPath, part);
        }
        m_transfers->enqueue(name, src, srcPath, dst, dstPath);
        ++queued;
    }
    m_status->setText(QStringLiteral("%1 Übertragung(en) eingereiht.").arg(queued));
}

} // namespace ncssh::gui
