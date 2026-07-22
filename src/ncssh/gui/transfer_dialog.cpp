#include "ncssh/gui/transfer_dialog.hpp"

#include "ncssh/core/i18n.hpp"
#include "ncssh/gui/transfer_manager.hpp"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;
using net::TransferJob;

static QString humanSize(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    double v = bytes;
    const char *units[] = {"KB", "MB", "GB", "TB"};
    int i = -1;
    do { v /= 1024.0; ++i; } while (v >= 1024.0 && i < 3);
    return QStringLiteral("%1 %2").arg(v, 0, 'f', 1).arg(QString::fromLatin1(units[i]));
}

static QString formatEta(double seconds)
{
    if (seconds <= 0 || seconds > 86400 * 7)
        return QStringLiteral("—");
    const int s = int(seconds);
    if (s < 60)
        return QStringLiteral("%1 s").arg(s);
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

static QString statusLabel(const QString &status)
{
    if (status == QLatin1String("pending")) return _t("wartet");
    if (status == QLatin1String("running")) return _t("Läuft");
    if (status == QLatin1String("done")) return _t("fertig");
    if (status == QLatin1String("error")) return _t("Fehler");
    if (status == QLatin1String("cancelled")) return _t("abgebrochen");
    return status;
}

// Richtung mit Pfeil — auf einen Blick erkennbar, wohin es geht.
static QString directionLabel(const QString &direction)
{
    if (direction == QLatin1String("upload")) return _t("↑ Upload");
    if (direction == QLatin1String("download")) return _t("↓ Download");
    if (direction == QLatin1String("remote")) return _t("↻ Remote");
    return _t("→ Lokal");
}

TransferDialog::TransferDialog(TransferManager *manager, QWidget *parent)
    : QDialog(parent), m_manager(manager)
{
    setWindowTitle(_t("Übertragungen"));
    resize(820, 420);

    auto *layout = new QVBoxLayout(this);
    m_table = new QTableWidget(0, 6, this);
    m_table->setHorizontalHeaderLabels({_t("Name"), _t("Richtung"), _t("Fortschritt"),
                                        _t("Größe"), _t("Tempo"), _t("Status")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_table, 1);

    auto *buttons = new QHBoxLayout();
    auto *cancelBtn = new QPushButton(_t("Abbrechen"), this);
    auto *retryBtn = new QPushButton(_t("Wiederaufnehmen"), this);
    retryBtn->setToolTip(_t("Fehlgeschlagene oder abgebrochene Übertragung erneut starten"));
    auto *clearBtn = new QPushButton(_t("Abgeschlossene entfernen"), this);
    auto *closeBtn = new QPushButton(_t("Schließen"), this);
    closeBtn->setDefault(true);
    connect(cancelBtn, &QPushButton::clicked, this, [this] {
        const int row = m_table->currentRow();
        if (row >= 0)
            m_manager->cancel(m_table->item(row, 0)->data(Qt::UserRole).toInt());
    });
    connect(retryBtn, &QPushButton::clicked, this, [this] {
        const int row = m_table->currentRow();
        if (row >= 0)
            m_manager->retry(m_table->item(row, 0)->data(Qt::UserRole).toInt());
    });
    connect(clearBtn, &QPushButton::clicked, this, [this] {
        m_manager->clearFinished();
        rebuild();
    });
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(cancelBtn);
    buttons->addWidget(retryBtn);
    buttons->addWidget(clearBtn);
    buttons->addStretch(1);
    buttons->addWidget(closeBtn);
    layout->addLayout(buttons);

    connect(manager, &TransferManager::jobAdded, this, [this](int) { rebuild(); });
    connect(manager, &TransferManager::jobUpdated, this, &TransferDialog::updateRow);
    rebuild();
}

int TransferDialog::rowForJob(int jobId) const
{
    for (int r = 0; r < m_table->rowCount(); ++r) {
        if (m_table->item(r, 0)->data(Qt::UserRole).toInt() == jobId)
            return r;
    }
    return -1;
}

void TransferDialog::rebuild()
{
    m_table->setRowCount(0);
    for (const TransferJob &job : m_manager->jobs()) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        auto *nameItem = new QTableWidgetItem(job.name);
        nameItem->setData(Qt::UserRole, job.id);
        m_table->setItem(row, 0, nameItem);
        auto *dirItem = new QTableWidgetItem(directionLabel(job.direction));
        dirItem->setToolTip(QStringLiteral("%1 → %2").arg(job.srcLabel, job.dstLabel));
        m_table->setItem(row, 1, dirItem);
        auto *bar = new QProgressBar(m_table);
        bar->setRange(0, 100);
        bar->setValue(job.percent());
        m_table->setCellWidget(row, 2, bar);
        m_table->setItem(row, 3, new QTableWidgetItem());
        m_table->setItem(row, 4, new QTableWidgetItem());
        m_table->setItem(row, 5, new QTableWidgetItem());
        updateRow(job.id);
    }
}

void TransferDialog::updateRow(int jobId)
{
    const int row = rowForJob(jobId);
    if (row < 0)
        return;
    const auto &jobs = m_manager->jobs();
    auto it = std::find_if(jobs.begin(), jobs.end(),
                           [jobId](const TransferJob &j) { return j.id == jobId; });
    if (it == jobs.end())
        return;
    const TransferJob &job = *it;

    if (auto *bar = qobject_cast<QProgressBar *>(m_table->cellWidget(row, 2)))
        bar->setValue(job.percent());
    m_table->item(row, 3)->setText(
        QStringLiteral("%1 / %2").arg(humanSize(job.copied), humanSize(job.total)));
    m_table->item(row, 1)->setText(directionLabel(job.direction));
    m_table->item(row, 4)->setText(
        job.speed > 0 ? QStringLiteral("%1/s").arg(humanSize(qint64(job.speed)))
                            + _t(" · ETA %1").arg(formatEta(job.eta))
                      : QStringLiteral("—"));
    QString status = statusLabel(job.status);
    if (job.verified)
        status = _t("fertig ✓");
    if (!job.error.isEmpty())
        status += QStringLiteral(" — ") + job.error;
    m_table->item(row, 5)->setText(status);
}

} // namespace ncssh::gui
