// sudo-Dateisystem: erfuellt den FileSystemProvider-Vertrag ueber sudo-Befehle.
//
// Im sudo-Modus einer Pane laufen alle Operationen ueber die bestehende SSH-
// Verbindung als root (bzw. via sudo), statt ueber SFTP als angemeldeter Nutzer.
// Schreiben/Passwort gehen ueber stdin (tee bzw. sudo -S), nie ueber die
// Kommandozeile.  (Port von net/sudofs.py)
#pragma once

#include "ncssh/core/filesystem.hpp"
#include "ncssh/net/ssh.hpp"

namespace ncssh::net {

// True, wenn sudo ein Passwort verlangt (kein NOPASSWD).
bool sudoNeedsPassword(const SSHSessionPtr &session);

// Prueft das sudo-Passwort ueber "sudo -S -p '' true".
bool verifySudoPassword(const SSHSessionPtr &session, const QString &password);

class SudoFileSystem : public core::FileSystemProvider {
public:
    // sftpFs muss ein SFTPFileSystem derselben Session sein (fuer Pfadsemantik).
    explicit SudoFileSystem(SFTPFileSystem *sftpFs, SSHSessionPtr session);

    std::vector<core::FileEntry> listDir(const QString &path) override;
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

    qint64 size(const QString &path);

private:
    // Fuehrt command mit sudo aus; gibt stdout. Wirft bei Fehler.
    QByteArray run(const QString &command, const QByteArray &stdinData = {});

    SFTPFileSystem *m_sftpFs;
    SSHSessionPtr m_session;
};

} // namespace ncssh::net
