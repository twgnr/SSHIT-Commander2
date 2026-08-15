// sudo-Dateisystem: erfuellt den FileSystemProvider-Vertrag ueber sudo-Befehle.
//
// Im sudo-Modus einer Pane laufen alle Operationen ueber die bestehende SSH-
// Verbindung als root (bzw. via sudo), statt ueber SFTP als angemeldeter Nutzer.
// Schreiben/Passwort gehen ueber stdin (tee bzw. sudo -S), nie ueber die
// Kommandozeile.
#pragma once

#include "ncssh/core/filesystem.hpp"
#include "ncssh/net/ssh.hpp"

#include <functional>

namespace ncssh::net {

// True, wenn sudo ein Passwort verlangt (kein NOPASSWD).
bool sudoNeedsPassword(const SSHSessionPtr &session);

// Prueft das sudo-Passwort ueber "sudo -S -p '' true".
bool verifySudoPassword(const SSHSessionPtr &session, const QString &password);

class SudoFileSystem : public core::FileSystemProvider {
public:
    // Fuehrt einen fertigen Befehl aus (inkl. sudo-Praefix) und liefert das
    // Ergebnis. Ueber diese Naht laesst sich der Provider ohne echten Server
    // pruefen — insbesondere, dass das Passwort nie im Nutzdaten-stdin landet.
    using ExecFn = std::function<ExecResult(const QString &command,
                                            const QByteArray &stdinData)>;
    // Liefert das aktuelle sudo-Passwort; leer = NOPASSWD/unbekannt.
    using PasswordFn = std::function<QString()>;

    // sftpFs muss ein SFTPFileSystem derselben Session sein (fuer Pfadsemantik).
    explicit SudoFileSystem(SFTPFileSystem *sftpFs, SSHSessionPtr session);
    // Variante mit eigener Ausfuehrung und Pfadsemantik (Tests, Sonderfaelle).
    SudoFileSystem(core::FileSystemProvider *pathFs, ExecFn exec, PasswordFn password);

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

    qint64 size(const QString &path) override;

private:
    // Fuehrt command mit sudo aus; gibt stdout. Wirft bei Fehler.
    QByteArray run(const QString &command, const QByteArray &stdinData = {});

    core::FileSystemProvider *m_pathFs;   // Pfadsemantik (join/parent/basename/home)
    SSHSessionPtr m_session;              // nur im Normalfall gesetzt
    ExecFn m_exec;
    PasswordFn m_password;
};

} // namespace ncssh::net
