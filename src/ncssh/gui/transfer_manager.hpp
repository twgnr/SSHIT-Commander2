// Transfer-Manager: fuehrt Uebertragungen aus, meldet Fortschritt via Qt-Signale.
#pragma once

#include "ncssh/core/filesystem.hpp"
#include "ncssh/gui/bridge.hpp"
#include "ncssh/net/transfer.hpp"

#include <QHash>
#include <QObject>
#include <QSet>
#include <vector>

namespace ncssh::gui {

class TransferManager : public QObject {
    Q_OBJECT
public:
    explicit TransferManager(AsyncBridge *bridge, QObject *parent = nullptr);

    // Stellt eine Uebertragung in die Warteschlange und startet sie.
    // deleteSource = Verschieben (Quelle nach Verifikation entfernen).
    int enqueue(const QString &name, core::FileSystemProvider *src, const QString &srcPath,
                core::FileSystemProvider *dst, const QString &dstPath,
                bool deleteSource = false);

    void retry(int jobId);
    void cancel(int jobId);
    // Pausiert eine laufende Uebertragung (Abbruch + Merken) und nimmt sie
    // spaeter am Ziel-Offset wieder auf — ohne einen Worker-Thread zu blockieren.
    void pause(int jobId);
    void resumePaused(int jobId);
    void clearFinished();

    const std::vector<net::TransferJob> &jobs() const { return m_jobs; }

signals:
    void jobAdded(int jobId);
    void jobUpdated(int jobId);

private:
    struct Params {
        core::FileSystemProvider *src = nullptr;
        QString srcPath;
        core::FileSystemProvider *dst = nullptr;
        QString dstPath;
        bool deleteSource = false;
    };

    void run(int jobId, bool resume);
    net::TransferJob *find(int jobId);

    AsyncBridge *m_bridge;
    std::vector<net::TransferJob> m_jobs;
    QHash<int, Params> m_params;
    QHash<int, BridgeTask *> m_tasks;
    QSet<int> m_pausing;  // Jobs, deren Abbruch als "pausiert" (nicht "abgebrochen") gilt
    int m_counter = 0;
};

} // namespace ncssh::gui
