// Virtuelles Dateisystem fuer den Netzwerkscanner-Modus.
//
// Bildet die Scan-Ergebnisse als drei navigierbare Ebenen ab:
//   net://                      -> Liste der gefundenen Hosts
//   net://<ip>                  -> Freigaben des Hosts
//   net://<ip>/<share>/<sub...> -> Dateien (ueber UNC \\ip\share lokal gelesen)
// (Port von core/netfs.py)
#pragma once

#include "ncssh/core/filesystem.hpp"
#include "ncssh/core/netscan.hpp"

#include <QHash>
#include <QString>
#include <vector>

namespace ncssh::core {

class NetworkScanProvider : public FileSystemProvider {
public:
    explicit NetworkScanProvider(const std::vector<HostResult> &hosts);

    bool isNetwork = true;

    // Host-Liste ersetzen (fuer "Erneut scannen").
    void setHosts(const std::vector<HostResult> &hosts);

    // True, solange die Host-Liste (oberste Ebene) angezeigt wird.
    bool isHostList(const QString &path) const;

    std::vector<FileEntry> listDir(const QString &path) override;
    bool isDir(const QString &path) override;
    void mkdir(const QString &path) override;
    void remove(const QString &path, bool recursive = false) override;
    QString readText(const QString &path, qint64 maxBytes = 200'000) override;
    void writeText(const QString &path, const QString &content) override;
    void writeBytes(const QString &path, const QByteArray &data) override;
    QByteArray readBytes(const QString &path, qint64 maxBytes = 25'000'000) override;
    void rename(const QString &oldPath, const QString &newPath) override;
    void chmod(const QString &path, quint32 mode) override;
    QString join(const QString &path, const QString &name) const override;
    QString parent(const QString &path) const override;
    QString basename(const QString &path) const override;
    QString home() override;

private:
    static QString displayOf(const HostResult &host);
    static QStringList segments(const QString &path);
    QString toUnc(const QString &path) const;
    std::vector<FileEntry> hostEntries() const;

    LocalFileSystem m_local;
    std::vector<HostResult> m_hosts;
    QHash<QString, HostResult> m_byId;
};

} // namespace ncssh::core
