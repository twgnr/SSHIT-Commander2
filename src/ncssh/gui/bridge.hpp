// Async-Bruecke: Worker-Thread-Pool fuer blockierende Arbeit.
//
// Die gesamte Logik-/Netzwerk-Schicht (libssh2, lokale Subprozesse) laeuft auf
// Worker-Threads. Ergebnisse werden ueber Qt-Queued-Invokes thread-sicher ins
// GUI gereicht. So bleibt das Qt-Fenster jederzeit responsiv.
//
// Einstiegspunkte:
//   bridge.run<T>(job, onDone, onError)   — Ergebnis-Job
//   bridge.stream(job, onLine, ...)       — zeilenweise Ausgabe
//   bridge.cancel(task)                   — kooperativer Abbruch
#pragma once

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QThreadPool>
#include <atomic>
#include <exception>
#include <functional>
#include <memory>

namespace ncssh::gui {

// Kooperatives Abbruch-Token; wird an blockierende Jobs durchgereicht.
class CancelToken {
public:
    void cancel() { m_cancelled.store(true); }
    bool isCancelled() const { return m_cancelled.load(); }
private:
    std::atomic_bool m_cancelled{false};
};
using CancelTokenPtr = std::shared_ptr<CancelToken>;

// Handle eines laufenden Jobs (lebt im GUI-Thread, wird nach Abschluss geloescht).
class BridgeTask : public QObject {
    Q_OBJECT
public:
    explicit BridgeTask(QObject *parent = nullptr) : QObject(parent) {}
    CancelTokenPtr cancelToken = std::make_shared<CancelToken>();
signals:
    void line(const QString &text);
    void finished();
    void failed(const QString &error);
};

class AsyncBridge : public QObject {
    Q_OBJECT
public:
    explicit AsyncBridge(QObject *parent = nullptr);

    void start();
    void stop();

    // Fuehrt job auf einem Worker-Thread aus; onDone/onError kommen im
    // GUI-Thread an. Rueckgabewert-Variante.
    //
    // owner (optional, aber WICHTIG fuer nicht-modale Dialoge): Objekt, dem die
    // Rueckrufe gehoeren. Ist es beim Eintreffen des Ergebnisses zerstoert,
    // werden sie NICHT mehr aufgerufen. Ohne diese Bindung greifen Lambdas, die
    // `this` eines inzwischen geschlossenen Fensters halten, auf freigegebenen
    // Speicher zu.
    template <typename T>
    BridgeTask *run(std::function<T()> job,
                    std::function<void(const T &)> onDone = {},
                    std::function<void(const QString &)> onError = {},
                    QObject *owner = nullptr)
    {
        BridgeTask *task = makeTask();
        QPointer<BridgeTask> guard(task);
        const bool hasOwner = owner != nullptr;
        QPointer<QObject> ownerGuard(owner);
        m_pool.start([this, task, guard, hasOwner, ownerGuard, job = std::move(job),
                      onDone = std::move(onDone), onError = std::move(onError)] {
            try {
                T result = job();
                QMetaObject::invokeMethod(this, [this, guard, hasOwner, ownerGuard, onDone,
                                                 result = std::move(result)] {
                    if (onDone && guard && !(hasOwner && !ownerGuard)) onDone(result);
                    releaseTask(guard.data());
                }, Qt::QueuedConnection);
            } catch (const std::exception &exc) {
                deliverError(guard, QString::fromUtf8(exc.what()), onError, hasOwner, ownerGuard);
            } catch (...) {
                deliverError(guard, QStringLiteral("Unbekannter Fehler"), onError, hasOwner,
                             ownerGuard);
            }
        });
        return task;
    }

    // Void-Variante (owner wie oben).
    BridgeTask *run(std::function<void()> job,
                    std::function<void()> onDone = {},
                    std::function<void(const QString &)> onError = {},
                    QObject *owner = nullptr);

    // Streaming: job liefert Zeilen ueber emitLine (beliebiger Thread);
    // sie kommen als task->line(...) im GUI-Thread an. Abbruch ueber cancel().
    using EmitLine = std::function<void(const QString &)>;
    using StreamJob = std::function<void(const EmitLine &, const CancelTokenPtr &)>;
    BridgeTask *stream(StreamJob job,
                       std::function<void(const QString &)> onLine,
                       std::function<void()> onFinished = {},
                       std::function<void(const QString &)> onError = {},
                       QObject *owner = nullptr);

    // Bricht einen laufenden stream()/run()-Job kooperativ ab.
    void cancel(BridgeTask *task);

private:
    BridgeTask *makeTask();
    void releaseTask(BridgeTask *task);
    void deliverError(QPointer<BridgeTask> guard, const QString &msg,
                      const std::function<void(const QString &)> &onError,
                      bool hasOwner = false, QPointer<QObject> ownerGuard = {});

    QThreadPool m_pool;
    QSet<BridgeTask *> m_tasks;
};

} // namespace ncssh::gui
