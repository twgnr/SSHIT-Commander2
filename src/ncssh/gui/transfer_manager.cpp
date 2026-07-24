#include "ncssh/gui/transfer_manager.hpp"

#include "ncssh/core/settings.hpp"

#include <QElapsedTimer>
#include <QThread>
#include <algorithm>

namespace ncssh::gui {

using net::TransferJob;

TransferManager::TransferManager(AsyncBridge *bridge, QObject *parent)
    : QObject(parent), m_bridge(bridge)
{
}

TransferJob *TransferManager::find(int jobId)
{
    auto it = std::find_if(m_jobs.begin(), m_jobs.end(),
                           [jobId](const TransferJob &j) { return j.id == jobId; });
    return it == m_jobs.end() ? nullptr : &(*it);
}

int TransferManager::enqueue(const QString &name, core::FileSystemProvider *src,
                             const QString &srcPath, core::FileSystemProvider *dst,
                             const QString &dstPath, bool deleteSource)
{
    TransferJob job;
    job.id = m_counter++;
    job.name = name;
    job.srcLabel = src->label;
    job.dstLabel = dst->label;
    job.direction = net::directionOf(src, dst);
    m_jobs.push_back(job);
    m_params.insert(job.id, Params{src, srcPath, dst, dstPath, deleteSource});
    emit jobAdded(job.id);
    run(job.id, false);
    return job.id;
}

void TransferManager::retry(int jobId)
{
    TransferJob *job = find(jobId);
    if (!job || !m_params.contains(jobId))
        return;
    if (job->status != QLatin1String("error") && job->status != QLatin1String("cancelled"))
        return;
    job->status = QStringLiteral("running");
    job->error.clear();
    emit jobUpdated(jobId);
    run(jobId, true);  // deleteSource wird aus den Params uebernommen
}

void TransferManager::run(int jobId, bool resume)
{
    const Params p = m_params.value(jobId);
    TransferJob *job = find(jobId);
    if (!job || !p.src || !p.dst)
        return;
    job->status = QStringLiteral("running");
    emit jobUpdated(jobId);

    // Fortschritt kommt vom Worker-Thread; nur bei Prozent-Aenderung melden.
    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();
    auto lastPct = std::make_shared<int>(-1);

    // Bandbreiten-Limit (KB/s, 0 = unbegrenzt) beim Start festhalten.
    const qint64 rateBps =
        qint64(core::getSettingInt(QStringLiteral("transfer_rate_limit_kbps"), 0)) * 1024;
    auto throttleBaseBytes = std::make_shared<qint64>(-1);
    auto throttleBaseMs = std::make_shared<qint64>(0);

    BridgeTask *task = m_bridge->stream(
        [p, resume, timer, rateBps, throttleBaseBytes, throttleBaseMs, jobId,
         this](const AsyncBridge::EmitLine &emitLine, const CancelTokenPtr &cancel) {
            net::transferWithProgress(
                p.src, p.srcPath, p.dst, p.dstPath,
                [&emitLine, &cancel, timer, rateBps, throttleBaseBytes,
                 throttleBaseMs](qint64 copied, qint64 total) {
                    if (cancel && cancel->isCancelled())
                        throw std::runtime_error("cancelled");
                    // Bandbreiten-Drosselung: schlafen, bis der Soll-Takt erreicht
                    // ist; in kleinen Schritten, damit Pause/Abbruch schnell greift.
                    if (rateBps > 0) {
                        if (*throttleBaseBytes < 0) {
                            *throttleBaseBytes = copied;
                            *throttleBaseMs = timer->elapsed();
                        }
                        const qint64 sent = copied - *throttleBaseBytes;
                        const qint64 expectedMs = sent * 1000 / rateBps;
                        qint64 sleepMs = expectedMs - (timer->elapsed() - *throttleBaseMs);
                        while (sleepMs > 0) {
                            if (cancel && cancel->isCancelled())
                                throw std::runtime_error("cancelled");
                            const qint64 step = std::min<qint64>(sleepMs, 50);
                            QThread::msleep(static_cast<unsigned long>(step));
                            sleepMs -= step;
                        }
                    }
                    // Fortschritt als "copied/total/ms" ueber den Line-Kanal.
                    emitLine(QStringLiteral("%1/%2/%3")
                                 .arg(copied).arg(total).arg(timer->elapsed()));
                },
                resume);
            // Verifikation + optionales Loeschen der Quelle (Verschieben).
            if (!net::pathIsDir(p.src, p.srcPath)) {
                const qint64 ssize = net::pathSize(p.src, p.srcPath);
                const qint64 dsize = net::pathSize(p.dst, p.dstPath);
                if (ssize != dsize)
                    throw std::runtime_error(
                        QStringLiteral("Größe weicht ab: Quelle %1, Ziel %2")
                            .arg(ssize).arg(dsize).toStdString());
                emitLine(QStringLiteral("verified"));
            } else if (p.deleteSource) {
                if (!net::verifyTree(p.src, p.srcPath, p.dst, p.dstPath))
                    throw std::runtime_error("Ziel unvollständig — Quelle NICHT gelöscht");
                emitLine(QStringLiteral("verified"));
            }
            if (p.deleteSource)
                p.src->remove(p.srcPath, net::pathIsDir(p.src, p.srcPath));
        },
        // onLine: Fortschritts-Ticks im GUI-Thread
        [this, jobId, lastPct](const QString &line) {
            TransferJob *j = find(jobId);
            if (!j)
                return;
            if (line == QLatin1String("verified")) {
                j->verified = true;
                emit jobUpdated(jobId);
                return;
            }
            const QStringList parts = line.split(QLatin1Char('/'));
            if (parts.size() != 3)
                return;
            j->copied = parts[0].toLongLong();
            j->total = parts[1].toLongLong();
            const double elapsed = parts[2].toDouble() / 1000.0;
            if (elapsed > 0.2) {
                j->speed = j->copied / elapsed;
                j->eta = (j->speed > 0 && j->total) ? (j->total - j->copied) / j->speed : 0.0;
            }
            const int pct = j->percent();
            if (pct != *lastPct) {  // drosseln: nur bei Prozent-Aenderung
                *lastPct = pct;
                emit jobUpdated(jobId);
            }
        },
        // onFinished
        [this, jobId] {
            if (TransferJob *j = find(jobId)) {
                j->status = QStringLiteral("done");
                emit jobUpdated(jobId);
            }
            m_pausing.remove(jobId);
            m_tasks.remove(jobId);
        },
        // onError
        [this, jobId](const QString &err) {
            if (TransferJob *j = find(jobId)) {
                if (err == QLatin1String("cancelled")) {
                    // Abbruch, der von pause() ausgeloest wurde, gilt als "pausiert".
                    j->status = m_pausing.contains(jobId) ? QStringLiteral("paused")
                                                          : QStringLiteral("cancelled");
                } else {
                    j->status = QStringLiteral("error");
                    j->error = err;
                }
                m_pausing.remove(jobId);
                emit jobUpdated(jobId);
            }
            m_tasks.remove(jobId);
        });
    m_tasks.insert(jobId, task);  // Handle merken, damit cancel()/pause() greifen
}

void TransferManager::cancel(int jobId)
{
    m_pausing.remove(jobId);  // ausdruecklicher Abbruch, nicht Pause
    if (BridgeTask *task = m_tasks.value(jobId, nullptr))
        m_bridge->cancel(task);
}

void TransferManager::pause(int jobId)
{
    TransferJob *job = find(jobId);
    if (!job || job->status != QLatin1String("running"))
        return;
    BridgeTask *task = m_tasks.value(jobId, nullptr);
    if (!task)
        return;
    m_pausing.insert(jobId);   // der folgende "cancelled" wird zu "paused"
    m_bridge->cancel(task);
}

void TransferManager::resumePaused(int jobId)
{
    TransferJob *job = find(jobId);
    if (!job || job->status != QLatin1String("paused") || !m_params.contains(jobId))
        return;
    job->status = QStringLiteral("running");
    emit jobUpdated(jobId);
    run(jobId, true);  // am Ziel-Offset fortsetzen
}

void TransferManager::clearFinished()
{
    m_jobs.erase(std::remove_if(m_jobs.begin(), m_jobs.end(),
                                [](const TransferJob &j) {
                                    return j.status != QLatin1String("pending")
                                           && j.status != QLatin1String("running");
                                }),
                 m_jobs.end());
}

} // namespace ncssh::gui
