// Einheitliche Dateisystem-Abstraktion.
//
// Die UI kennt nur dieses Interface — ob lokal oder via SFTP ist transparent.
// Alle Methoden sind BLOCKIEREND und laufen ueber die AsyncBridge auf
// Worker-Threads, damit Remote-Operationen die UI nie blockieren.
// Fehler werden als std::runtime_error (UTF-8-Meldung) geworfen.
#pragma once

#include "ncssh/core/models.hpp"

#include <QByteArray>
#include <QString>
#include <stdexcept>
#include <vector>

namespace ncssh::core {

// Vertrag, den lokale und Remote-Provider gleichermassen erfuellen.
class FileSystemProvider {
public:
    virtual ~FileSystemProvider() = default;

    QString label = QStringLiteral("local");  // z.B. "local" oder "user@host"
    bool isRemote = false;

    virtual std::vector<FileEntry> listDir(const QString &path) = 0;
    virtual bool isDir(const QString &path) = 0;
    virtual void mkdir(const QString &path) = 0;
    virtual void remove(const QString &path, bool recursive = false) = 0;
    virtual QString readText(const QString &path, qint64 maxBytes = 200'000) = 0;
    virtual void writeText(const QString &path, const QString &content) = 0;
    virtual void writeBytes(const QString &path, const QByteArray &data) = 0;
    virtual QByteArray readBytes(const QString &path, qint64 maxBytes = 25'000'000) = 0;
    virtual void rename(const QString &oldPath, const QString &newPath) = 0;
    virtual void chmod(const QString &path, quint32 mode) = 0;
    virtual QString join(const QString &path, const QString &name) const = 0;
    virtual QString parent(const QString &path) const = 0;
    virtual QString basename(const QString &path) const = 0;
    virtual QString home() = 0;

    // Legt einen symbolischen Link linkPath an, der auf target zeigt. Standard:
    // nicht unterstuetzt — nur lokale und SFTP-Provider ueberschreiben das.
    virtual void symlink(const QString &target, const QString &linkPath)
    {
        Q_UNUSED(target);
        Q_UNUSED(linkPath);
        throw std::runtime_error(
            "Symbolische Links werden von diesem Dateisystem nicht unterstützt.");
    }

    // Groesse in Bytes; 0 = unbekannt oder nicht vorhanden. Der Transfer nutzt
    // das fuer Gesamtfortschritt und Verifikation.
    virtual qint64 size(const QString &path) { Q_UNUSED(path); return 0; }

    // Kanonische Anzeige-/Navigationsform eines Pfads. Die Pane normalisiert
    // jedes Navigationsziel hierueber, damit ein Verzeichnis immer nur EINE
    // Schreibweise hat. Standard: unveraendert (POSIX/net:// sind bereits
    // eindeutig); lokal wird absolut + native Separatoren erzwungen — sonst
    // landet "." wortwoertlich in der Pfadleiste und "C:/" und "C:\" gelten
    // als zwei verschiedene Ebenen.
    virtual QString normalize(const QString &path) const { return path; }
};

// Lokales Dateisystem (blockierende OS-Calls; via Bridge auf Worker-Threads).
class LocalFileSystem : public FileSystemProvider {
public:
    LocalFileSystem();

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
    qint64 size(const QString &path) override;
    void symlink(const QString &target, const QString &linkPath) override;
    QString normalize(const QString &path) const override;
};

} // namespace ncssh::core
