#include "ncssh/net/sudofs.hpp"
#include "ncssh/core/encodings.hpp"

#include "ncssh/core/lsparse.hpp"

#include <QDir>
#include <algorithm>
#include <stdexcept>

namespace ncssh::net {

using core::EntryType;
using core::FileEntry;

static QString shQuote(const QString &s)
{
    if (s.isEmpty())
        return QStringLiteral("''");
    QString q = s;
    q.replace(QLatin1Char('\''), QLatin1String("'\"'\"'"));
    return QLatin1Char('\'') + q + QLatin1Char('\'');
}

// --- Pane als anderer Benutzer ------------------------------------------------

QString RunAs::display() const
{
    if (method == Method::Su)
        return QStringLiteral("su: ") + targetUser();
    return user.isEmpty() ? QStringLiteral("sudo") : QStringLiteral("sudo: ") + user;
}

std::vector<UserAccount> parsePasswd(const QString &text)
{
    std::vector<UserAccount> out;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QStringList f = line.trimmed().split(QLatin1Char(':'));
        if (f.size() < 7 || f.at(0).isEmpty())
            continue;
        bool ok = false;
        UserAccount a;
        a.name = f.at(0);
        a.uid = f.at(2).toLongLong(&ok);
        if (!ok)
            continue;
        a.home = f.at(5);
        a.shell = f.at(6);
        out.push_back(a);
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const UserAccount &a, const UserAccount &b) { return a.uid < b.uid; });
    return out;
}

std::vector<UserAccount> listUsers(const SSHSessionPtr &session)
{
    const ExecResult r =
        session->exec(QStringLiteral("getent passwd 2>/dev/null || cat /etc/passwd"));
    return parsePasswd(QString::fromUtf8(r.out));
}

SwitchProbe probeSwitch(const SSHSessionPtr &session, const QString &user)
{
    // Ein einziger Roundtrip: wer bin ich, welche Gruppen, klappt sudo -u
    // ohne Passwort (NOPASSWD bzw. noch gueltiger Zeitstempel)?
    const QString target = user.isEmpty() ? QStringLiteral("root") : user;
    const ExecResult r = session->exec(
        QStringLiteral("id -un; id -u; id -Gn; "
                       "env LC_ALL=C sudo -n -u %1 true >/dev/null 2>&1 "
                       "&& echo SUDO-OK || echo SUDO-NO")
            .arg(shQuote(target)));
    const QStringList lines = QString::fromUtf8(r.out).split(QLatin1Char('\n'));
    SwitchProbe p;
    p.loginUser = lines.value(0).trimmed();
    p.loginIsRoot = lines.value(1).trimmed() == QLatin1String("0");
    const QStringList groups = lines.value(2).simplified().split(QLatin1Char(' '));
    p.sudoGroup = groups.contains(QStringLiteral("sudo")) || groups.contains(QStringLiteral("wheel"))
                  || groups.contains(QStringLiteral("admin"));
    p.sudoWithoutPassword = lines.value(3).trimmed() == QLatin1String("SUDO-OK");
    return p;
}

SudoCheck classifySudoFailure(const QString &stderrText)
{
    const QString e = stderrText.toLower();
    if (e.contains(QLatin1String("not in the sudoers")) || e.contains(QLatin1String("not allowed to"))
        || e.contains(QLatin1String("may not run sudo")) || e.contains(QLatin1String("not found"))
        || e.contains(QLatin1String("unknown user"))
        || e.contains(QLatin1String("i'm afraid i can't do that")))   // sudo-rs
        return SudoCheck::NotAllowed;
    return SudoCheck::WrongPassword;
}

SudoCheck checkSudoAs(const SSHSessionPtr &session, const QString &user, const QString &password)
{
    const QString target = user.isEmpty() ? QStringLiteral("root") : user;
    const ExecResult r =
        session->exec(QStringLiteral("env LC_ALL=C sudo -S -p '' -u %1 true").arg(shQuote(target)),
                      (password + QLatin1Char('\n')).toUtf8());
    if (r.exitStatus == 0)
        return SudoCheck::Ok;
    return classifySudoFailure(QString::fromUtf8(r.err));
}

namespace {
const QByteArray kSuReady = QByteArrayLiteral("\x1eSSHIT-READY\x1e");
const QByteArray kSuEnd = QByteArrayLiteral("\x1eSSHIT-END:");
} // namespace

namespace {

// Gemeinsamer PTY-Ablauf fuer su und sudo: das Programm fragt (ohne Echo) ggf.
// nach dem Passwort; danach schaltet das Skript das Terminal roh (keine
// Zeilen-/Steuerzeichen-Behandlung, kein Echo), meldet sich mit einer Marke
// und liest genau stdinData.size() Bytes als Nutzdaten. Die Endmarke traegt
// den Exit-Code. wrap baut aus dem (bereits gequoteten) Skript den Befehl.
ExecResult runMarked(const PtyExecFn &pty, const std::function<QString(const QString &)> &wrap,
                     const QString &password, bool answerPrompt, const QString &command,
                     const QByteArray &stdinData)
{
    const QString payload =
        stdinData.isEmpty()
            ? QStringLiteral("{ %1; } </dev/null").arg(command)
            : QStringLiteral("head -c %1 | { %2; }").arg(stdinData.size()).arg(command);
    const QString script =
        QStringLiteral("stty raw -echo -iexten 2>/dev/null; printf '\\036SSHIT-READY\\036'; ")
        + payload + QStringLiteral("; printf '\\036SSHIT-END:%d\\036' \"$?\"");

    qsizetype promptEnd = -1;   // Ausgabe-Position, an der das Passwort ging
    bool sentData = false;
    bool aborted = false;
    const auto step = [&](const QByteArray &out) -> QByteArray {
        if (out.contains(kSuReady)) {
            if (sentData)
                return {};
            sentData = true;
            return stdinData;
        }
        if (!answerPrompt || aborted)
            return {};
        if (promptEnd < 0) {
            // Passwort-Abfrage ("Password: ", "[sudo] password for x: ").
            if (out.trimmed().endsWith(':')) {
                promptEnd = out.size();
                return password.toUtf8() + '\n';
            }
            return {};
        }
        // Erneute Abfrage = Passwort falsch: abbrechen (Strg+C), statt bis
        // zur Zeitueberschreitung auf eine zweite Eingabe zu warten.
        if (out.mid(promptEnd).trimmed().endsWith(':')) {
            aborted = true;
            return QByteArray("\x03");
        }
        return {};
    };
    const ExecResult raw = pty(wrap(shQuote(script)), step);

    ExecResult result;
    const qsizetype ready = raw.out.indexOf(kSuReady);
    if (ready < 0) {
        // Anmeldung gescheitert: Ausgabe ohne die Passwort-Abfrage als Fehler.
        QString text = QString::fromUtf8(promptEnd >= 0 ? raw.out.mid(promptEnd) : raw.out);
        text.replace(QLatin1Char('\r'), QString());
        text = text.trimmed();
        result.exitStatus = raw.exitStatus != 0 ? raw.exitStatus : 1;
        result.err = (text.isEmpty() ? QStringLiteral("Anmeldung fehlgeschlagen") : text).toUtf8();
        return result;
    }
    QByteArray body = raw.out.mid(ready + kSuReady.size());
    const qsizetype end = body.lastIndexOf(kSuEnd);
    result.exitStatus = raw.exitStatus;
    if (end >= 0) {
        const QByteArray tail = body.mid(end + kSuEnd.size());
        bool ok = false;
        const int code = tail.left(tail.indexOf('\x1e')).toInt(&ok);
        result.exitStatus = ok ? code : -1;
        body.truncate(end);
    } else if (result.exitStatus == 0) {
        result.exitStatus = -1;   // Endmarke fehlt: Ausgabe unvollstaendig
    }
    result.out = body;
    if (result.exitStatus != 0)
        result.err = body.trimmed();   // im PTY sind stdout und stderr vereint
    return result;
}

} // namespace

ExecResult suExec(const PtyExecFn &pty, const QString &user, const QString &password,
                  const QString &command, const QByteArray &stdinData, bool loginIsRoot)
{
    // root darf eine Shell erzwingen (auch fuer Dienstkonten mit nologin);
    // sonst laeuft -c in der Login-Shell des Ziels -> dort /bin/sh starten.
    const auto wrap = [&](const QString &quotedScript) {
        return loginIsRoot
                   ? QStringLiteral("env LC_ALL=C su -s /bin/sh -c %1 %2")
                         .arg(quotedScript, shQuote(user))
                   : QStringLiteral("env LC_ALL=C su -c %1 %2")
                         .arg(shQuote(QStringLiteral("exec /bin/sh -c ") + quotedScript),
                              shQuote(user));
    };
    return runMarked(pty, wrap, password, !loginIsRoot, command, stdinData);
}

ExecResult sudoPtyExec(const PtyExecFn &pty, const QString &user, const QString &password,
                       const QString &command, const QByteArray &stdinData)
{
    const auto wrap = [&](const QString &quotedScript) {
        return QStringLiteral("env LC_ALL=C sudo %1/bin/sh -c %2")
            .arg(user.isEmpty() ? QString() : QStringLiteral("-u %1 ").arg(shQuote(user)),
                 quotedScript);
    };
    return runMarked(pty, wrap, password, /*answerPrompt=*/true, command, stdinData);
}

bool sudoNeedsAuthentication(const QString &stderrText)
{
    // sudo hat abgelehnt, BEVOR der Befehl lief (Wiederholen ist gefahrlos).
    // Klassisches sudo bzw. sudo-rs (Ubuntu ab 25.10).
    const QString e = stderrText.toLower();
    return e.contains(QLatin1String("a password is required"))
           || e.contains(QLatin1String("interactive authentication is required"));
}

// ---------------------------------------------------------------------------

SudoFileSystem::SudoFileSystem(SFTPFileSystem *sftpFs, SSHSessionPtr session, RunAs runAs)
    : m_pathFs(sftpFs), m_session(std::move(session)), m_runAs(std::move(runAs))
{
    isRemote = true;
    label = sftpFs->label + QStringLiteral(" (%1)").arg(m_runAs.display());
    SSHSessionPtr s = m_session;
    m_exec = [s](const QString &command, const QByteArray &stdinData) {
        return s->exec(command, stdinData);
    };
    if (m_runAs.method == RunAs::Method::Su) {
        const QString user = m_runAs.targetUser();
        m_password = [s, user]() {
            const auto it = s->userPasswords.find(user);
            return it == s->userPasswords.end() ? QString() : it->second;
        };
    } else {
        m_password = [s]() { return s->sudoPassword.value_or(QString()); };
    }
    m_pty = [s](const QString &command, const SSHSession::PtyStep &step) {
        return s->execPty(command, step);
    };
}

SudoFileSystem::SudoFileSystem(core::FileSystemProvider *pathFs, ExecFn exec,
                               PasswordFn password, RunAs runAs, PtyExecFn pty)
    : m_pathFs(pathFs), m_exec(std::move(exec)), m_password(std::move(password)),
      m_runAs(std::move(runAs)), m_pty(std::move(pty))
{
    isRemote = true;
    label = pathFs->label + QStringLiteral(" (%1)").arg(m_runAs.display());
}

QByteArray SudoFileSystem::run(const QString &commandIn, const QByteArray &stdinData)
{
    if (m_runAs.method == RunAs::Method::Su) {
        if (!m_pty)
            throw std::runtime_error("su: kein Terminal verfügbar");
        const ExecResult r = suExec(m_pty, m_runAs.targetUser(),
                                    m_password ? m_password() : QString(), commandIn,
                                    stdinData, m_runAs.loginIsRoot);
        if (r.exitStatus != 0) {
            const QString err = QString::fromUtf8(r.err).trimmed();
            throw std::runtime_error(
                (err.isEmpty() ? QStringLiteral("su: Exit %1").arg(r.exitStatus) : err)
                    .toStdString());
        }
        return r.out;
    }
    // sudo als anderer Benutzer: "-u USER" vor den Befehl.
    const QString command = m_runAs.user.isEmpty()
                                ? commandIn
                                : QStringLiteral("-u %1 %2").arg(shQuote(m_runAs.user), commandIn);
    // Schnell ohne Passwort (NOPASSWD bzw. gueltiger Zeitstempel).
    ExecResult r = m_exec(QStringLiteral("sudo -n ") + command, stdinData);
    if (r.exitStatus != 0 && sudoNeedsAuthentication(QString::fromUtf8(r.err))) {
        // sudo hat vor dem Befehl abgelehnt. Der Zeitstempel eines frueheren
        // "sudo -v" gilt NICHT: jeder SSH-Kanal ist eine eigene Sitzung. Daher
        // im Terminal anmelden (Antwort auf die Passwort-Abfrage) — das
        // Passwort teilt sich dabei NIE einen Strom mit den Nutzdaten.
        const QString password = m_password ? m_password() : QString();
        if (!password.isEmpty() && m_pty) {
            r = sudoPtyExec(m_pty, m_runAs.user, password, commandIn, stdinData);
        } else if (!password.isEmpty()) {
            // Ohne Terminal (Sonderfaelle/Tests): auffrischen und wiederholen —
            // hilft nur, wenn sudo Zeitstempel sitzungsuebergreifend teilt.
            m_exec(QStringLiteral("sudo -S -p '' -v"), (password + QLatin1Char('\n')).toUtf8());
            r = m_exec(QStringLiteral("sudo -n ") + command, stdinData);
        }
    }
    if (r.exitStatus != 0) {
        const QString err = QString::fromUtf8(r.err).trimmed();
        throw std::runtime_error(
            (err.isEmpty() ? QStringLiteral("sudo: Exit %1").arg(r.exitStatus) : err).toStdString());
    }
    return r.out;
}

std::vector<FileEntry> SudoFileSystem::listDir(const QString &path)
{
    std::vector<FileEntry> entries;
    if (parent(path) != path) {
        FileEntry up;
        up.name = QStringLiteral("..");
        up.type = EntryType::Parent;
        entries.push_back(up);
    }
    // "env TZ=UTC0": Zeiten in UTC, damit parseLsLong sie unabhaengig von der
    // Server-Zeitzone korrekt einordnet (sudo setzt die Umgebung zurueck, ein
    // direktes "sudo TZ=..." waere je nach sudoers verboten). Literal-Quoting,
    // damit Namen ohne Anfuehrungszeichen/Escapes erscheinen.
    const QByteArray out =
        run(QStringLiteral("env TZ=UTC0 ls -lnA --time-style=long-iso --quoting-style=literal -- ")
            + shQuote(path));
    std::vector<FileEntry> items = core::parseLsLong(QString::fromUtf8(out));
    std::sort(items.begin(), items.end(), [](const FileEntry &a, const FileEntry &b) {
        if (a.isDir() != b.isDir()) return a.isDir();
        return a.name.toLower() < b.name.toLower();
    });
    entries.insert(entries.end(), items.begin(), items.end());
    return entries;
}

bool SudoFileSystem::isDir(const QString &path)
{
    // Frueher "sudo -n test -d X && echo D || echo F": das "|| echo F" lief
    // AUSSERHALB von sudo — lehnte sudo ab (Timestamp abgelaufen), endete der
    // Befehl trotzdem mit Exit 0 und "F", run() frischte nie auf und ein
    // Ordner galt still als Datei. Jetzt laeuft alles unter sudo in einer
    // Shell; der Pfad geht als $1 hinein (kein Quoting im Skript noetig).
    const QByteArray out =
        run(QStringLiteral("sh -c 'test -d \"$1\" && echo D || echo F' sh ") + shQuote(path));
    return out.trimmed() == "D";
}

QByteArray SudoFileSystem::readBytes(const QString &path, qint64 maxBytes)
{
    // "head -c -N" hiesse bei GNU "alles AUSSER den letzten N Bytes" — ein
    // negativer Wert (= unbegrenzt) darf daher nie durchgereicht werden.
    if (maxBytes < 0)
        return run(QStringLiteral("cat -- ") + shQuote(path));
    return run(QStringLiteral("head -c %1 -- %2").arg(maxBytes).arg(shQuote(path)));
}

QString SudoFileSystem::readText(const QString &path, qint64 maxBytes)
{
    // Kodierung erkennen (UTF-8, UTF-16 mit/ohne BOM, ANSI) statt stur UTF-8.
    return core::decodeAuto(readBytes(path, maxBytes));
}

qint64 SudoFileSystem::size(const QString &path)
{
    try {
        const QByteArray out = run(QStringLiteral("stat -c %s -- ") + shQuote(path));
        bool ok = false;
        const qint64 n = QString::fromUtf8(out).trimmed().toLongLong(&ok);
        return (ok && n >= 0) ? n : 0;
    } catch (...) {
        return 0;
    }
}

void SudoFileSystem::writeBytes(const QString &path, const QByteArray &data)
{
    run(QStringLiteral("tee -- ") + shQuote(path) + QStringLiteral(" > /dev/null"), data);
}

void SudoFileSystem::writeText(const QString &path, const QString &content)
{
    writeBytes(path, content.toUtf8());
}

void SudoFileSystem::mkdir(const QString &path)
{
    run(QStringLiteral("mkdir -p -- ") + shQuote(path));
}

void SudoFileSystem::remove(const QString &path, bool recursive)
{
    run(QStringLiteral("rm %1 -- %2").arg(recursive ? QStringLiteral("-rf") : QStringLiteral("-f"),
                                          shQuote(path)));
}

void SudoFileSystem::rename(const QString &oldPath, const QString &newPath)
{
    // -T: ist newPath ein vorhandener Ordner, wuerde "mv a b" sonst nach b/a
    // verschieben statt umzubenennen (Datei landet an unerwarteter Stelle).
    run(QStringLiteral("mv -T -- %1 %2").arg(shQuote(oldPath), shQuote(newPath)));
}

void SudoFileSystem::chmod(const QString &path, quint32 mode)
{
    run(QStringLiteral("chmod %1 -- %2")
            .arg(QString::number(mode & 07777, 8), shQuote(path)));
}

QString SudoFileSystem::join(const QString &path, const QString &name) const
{
    return m_pathFs->join(path, name);
}

QString SudoFileSystem::parent(const QString &path) const
{
    return m_pathFs->parent(path);
}

QString SudoFileSystem::basename(const QString &path) const
{
    return m_pathFs->basename(path);
}

QString SudoFileSystem::home()
{
    return m_pathFs->home();
}

} // namespace ncssh::net
