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

// --- Pane als anderer Benutzer ------------------------------------------------

// Als wer und wie eine Pane arbeitet. Sudo: "sudo -u USER" mit dem EIGENEN
// Passwort (braucht sudo-Rechte). Su: "su USER" mit dem Passwort des ZIEL-
// Benutzers (braucht keine Admin-Rechte; als root ganz ohne Passwort).
struct RunAs {
    enum class Method { Sudo, Su };
    QString user;                 // leer = root
    Method method = Method::Sudo;
    bool loginIsRoot = false;     // angemeldet als root: su ohne Passwort

    QString targetUser() const { return user.isEmpty() ? QStringLiteral("root") : user; }
    // Kurzform fuer Chip und Status: "sudo", "sudo: www-data", "su: bob".
    QString display() const;
};

struct UserAccount {
    QString name;
    qint64 uid = -1;
    QString home;
    QString shell;
    // Systemkonto (Dienst): uid unter 1000 oder nobody — ausser root.
    bool system() const { return uid != 0 && (uid < 1000 || uid >= 65534); }
};

// Zeilen im passwd-Format (getent passwd) -> Konten, nach uid sortiert.
std::vector<UserAccount> parsePasswd(const QString &text);
std::vector<UserAccount> listUsers(const SSHSessionPtr &session);

// Vorpruefung (ohne Passwort) fuer den Wechsel zu user.
struct SwitchProbe {
    QString loginUser;
    bool loginIsRoot = false;
    bool sudoWithoutPassword = false;   // sudo -n -u USER klappt (NOPASSWD)
    bool sudoGroup = false;             // Mitglied in sudo/wheel/admin
};
SwitchProbe probeSwitch(const SSHSessionPtr &session, const QString &user);

// sudo -u USER mit dem eigenen Passwort. NotAllowed: sudo fehlt oder sudoers
// erlaubt es nicht — dann bleibt nur su mit dem Passwort des Ziel-Benutzers.
enum class SudoCheck { Ok, WrongPassword, NotAllowed };
SudoCheck classifySudoFailure(const QString &stderrText);
SudoCheck checkSudoAs(const SSHSessionPtr &session, const QString &user,
                      const QString &password);

// Fuehrt command per su als user aus. su liest das Passwort nur von einem
// Terminal — daher ueber ein PTY; Nutzdaten (stdinData) gehen erst NACH der
// Anmeldung und im Roh-Modus hinein, das Passwort landet nie in den Daten.
using PtyExecFn = std::function<ExecResult(const QString &command,
                                           const SSHSession::PtyStep &step)>;
ExecResult suExec(const PtyExecFn &pty, const QString &user, const QString &password,
                  const QString &command, const QByteArray &stdinData = {},
                  bool loginIsRoot = false);

// Wie suExec, aber per "sudo [-u USER]" mit dem EIGENEN Passwort im Terminal.
// Noetig, weil ein sudo-Zeitstempel nicht ueber SSH-Kanaele hinweg gilt.
ExecResult sudoPtyExec(const PtyExecFn &pty, const QString &user, const QString &password,
                       const QString &command, const QByteArray &stdinData = {});

// Hat sudo VOR dem Befehl eine Anmeldung verlangt (klassisch und sudo-rs)?
bool sudoNeedsAuthentication(const QString &stderrText);

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
    explicit SudoFileSystem(SFTPFileSystem *sftpFs, SSHSessionPtr session, RunAs runAs = {});
    // Variante mit eigener Ausfuehrung und Pfadsemantik (Tests, Sonderfaelle).
    // password liefert bei su das Passwort des Ziel-Benutzers.
    SudoFileSystem(core::FileSystemProvider *pathFs, ExecFn exec, PasswordFn password,
                   RunAs runAs = {}, PtyExecFn pty = {});

    const RunAs &runAs() const { return m_runAs; }

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
    RunAs m_runAs;
    PtyExecFn m_pty;                      // nur fuer su
};

} // namespace ncssh::net
