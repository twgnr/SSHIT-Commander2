#include "ncssh/gui/bridge.hpp"

namespace ncssh::gui {

AsyncBridge::AsyncBridge(QObject *parent) : QObject(parent)
{
    // Genug Threads fuer parallele Transfers + Listings + Streams. Die
    // Untergrenze ist bewusst grosszuegig: Konsolen-Streams und Transfers
    // belegen ihren Thread fuer die gesamte Laufzeit — ist der Pool voll,
    // reiht sich z. B. ein Verbinden-Klick still ein und "nichts passiert".
    m_pool.setMaxThreadCount(qMax(16, QThread::idealThreadCount() * 2));
}

void AsyncBridge::start() {}

void AsyncBridge::stop()
{
    for (BridgeTask *t : std::as_const(m_tasks))
        t->cancelToken->cancel();
    m_pool.waitForDone(3000);
}

BridgeTask *AsyncBridge::makeTask()
{
    auto *task = new BridgeTask(this);
    m_tasks.insert(task);
    return task;
}

void AsyncBridge::releaseTask(BridgeTask *task)
{
    if (!task)
        return;
    m_tasks.remove(task);
    task->deleteLater();
}

void AsyncBridge::deliverError(QPointer<BridgeTask> guard, const QString &msg,
                               const std::function<void(const QString &)> &onError)
{
    QMetaObject::invokeMethod(this, [this, guard, msg, onError] {
        if (guard) {
            emit guard->failed(msg);
            if (onError) onError(msg);
        }
        releaseTask(guard.data());
    }, Qt::QueuedConnection);
}

BridgeTask *AsyncBridge::run(std::function<void()> job,
                             std::function<void()> onDone,
                             std::function<void(const QString &)> onError)
{
    BridgeTask *task = makeTask();
    QPointer<BridgeTask> guard(task);
    m_pool.start([this, guard, job = std::move(job), onDone = std::move(onDone),
                  onError = std::move(onError)] {
        try {
            job();
            QMetaObject::invokeMethod(this, [this, guard, onDone] {
                if (onDone && guard) onDone();
                releaseTask(guard.data());
            }, Qt::QueuedConnection);
        } catch (const std::exception &exc) {
            deliverError(guard, QString::fromUtf8(exc.what()), onError);
        } catch (...) {
            deliverError(guard, QStringLiteral("Unbekannter Fehler"), onError);
        }
    });
    return task;
}

BridgeTask *AsyncBridge::stream(StreamJob job,
                                std::function<void(const QString &)> onLine,
                                std::function<void()> onFinished,
                                std::function<void(const QString &)> onError)
{
    BridgeTask *task = makeTask();
    QPointer<BridgeTask> guard(task);
    if (onLine)
        connect(task, &BridgeTask::line, this, [onLine](const QString &l) { onLine(l); });
    if (onFinished)
        connect(task, &BridgeTask::finished, this, [onFinished] { onFinished(); });
    if (onError)
        connect(task, &BridgeTask::failed, this, [onError](const QString &e) { onError(e); });

    const CancelTokenPtr token = task->cancelToken;
    m_pool.start([this, guard, token, job = std::move(job)] {
        const EmitLine emitLine = [guard](const QString &text) {
            if (guard)
                QMetaObject::invokeMethod(guard.data(), [guard, text] {
                    if (guard) emit guard->line(text);
                }, Qt::QueuedConnection);
        };
        try {
            job(emitLine, token);
        } catch (const std::exception &exc) {
            // Abbruch durch den Nutzer: als normales Ende behandeln
            if (!token->isCancelled()) {
                const QString msg = QString::fromUtf8(exc.what());
                QMetaObject::invokeMethod(this, [this, guard, msg] {
                    if (guard) emit guard->failed(msg);
                    releaseTask(guard.data());
                }, Qt::QueuedConnection);
                return;
            }
        } catch (...) {
            if (!token->isCancelled()) {
                QMetaObject::invokeMethod(this, [this, guard] {
                    if (guard) emit guard->failed(QStringLiteral("Unbekannter Fehler"));
                    releaseTask(guard.data());
                }, Qt::QueuedConnection);
                return;
            }
        }
        QMetaObject::invokeMethod(this, [this, guard] {
            if (guard) emit guard->finished();
            releaseTask(guard.data());
        }, Qt::QueuedConnection);
    });
    return task;
}

void AsyncBridge::cancel(BridgeTask *task)
{
    if (task && m_tasks.contains(task))
        task->cancelToken->cancel();
}

} // namespace ncssh::gui
