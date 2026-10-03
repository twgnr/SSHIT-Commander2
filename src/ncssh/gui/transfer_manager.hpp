// Transfer-Manager: fuehrt Uebertragungen aus, meldet Fortschritt via Qt-Signale.
#pragma once

#include "ncssh/core/filesystem.hpp"
#include "ncssh/gui/bridge.hpp"
#include "ncssh/net/transfer.hpp"

#include <QHash>
#include <QObject>
#include <QSet>
#include <memory>
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

    // --- Provider-Lebensdauer ---
    // Jobs halten nur rohe Provider-Zeiger; die Provider gehoeren den Tabs.
    // Laufende, wartende oder pausierte Jobs, die einen der Provider nutzen
    // (als Quelle ODER Ziel — Drag & Drop geht auch ueber Tab-Grenzen).
    int activeJobsFor(const QSet<const core::FileSystemProvider *> &providers) const;
    // Der Tab bzw. die Verbindung verschwindet: laufende Jobs dieser Provider
    // abbrechen, pausierte als abgebrochen markieren und ALLE ihre Parameter
    // vergessen — Wiederholen/Fortsetzen ist danach nicht mehr moeglich.
    void releaseProviders(const QSet<const core::FileSystemProvider *> &providers);
    // false, wenn der Job nicht mehr neu gestartet werden kann (Provider weg).
    bool canRestart(int jobId) const { return m_params.contains(jobId); }
    // Haelt Provider-Objekte eines geschlossenen Tabs bis zum Programmende am
    // Leben: Worker-Threads (Transfers, Listings, Vorschau) koennen sie nach
    // dem Abbruch noch kurz benutzen — freigeben waere ein Use-after-free.
    void keepAlive(std::shared_ptr<void> holder);

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
    std::vector<std::shared_ptr<void>> m_keepAlive;   // Provider geschlossener Tabs
};

} // namespace ncssh::gui
