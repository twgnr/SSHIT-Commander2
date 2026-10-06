// Pane als anderer Benutzer (Rechtsklick auf den sudo-Chip): su bzw. sudo -u.
//
// Teil 1 laeuft immer: simuliertes Terminal (su-Ablauf), passwd-Parser,
// sudo-Fehlerklassen, sudo -u im Provider.
// Teil 2 (live) nur mit NCSSH_LIVE_SSH=host:port — gegen einen Testserver mit
// den Benutzern sshitadm (sudo-Gruppe), sshitbob (ohne sudo) und sshitcarl.
#include "tests/harness.hpp"

#include "ncssh/core/filesystem.hpp"
#include "ncssh/net/ssh.hpp"
#include "ncssh/net/sudofs.hpp"

#include "ncssh/gui/bridge.hpp"
#include "ncssh/gui/file_panel.hpp"
#include "ncssh/gui/transfer_manager.hpp"
#include "ncssh/gui/workspace.hpp"
#include "ncssh/net/session.hpp"

#include <QAction>
#include <QApplication>
#include <QDeadlineTimer>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QRandomGenerator>
#include <QTableWidget>
#include <QThread>
#include <QTimer>

using namespace ncssh;
using namespace ncssh::net;

namespace {

// Simuliert su in einem Terminal: Passwort-Abfrage, bei richtigem Passwort
// Bereit-Marke, dann die Nutzdaten lesen und Ausgabe + Endmarke liefern.
struct FakeSuTerminal {
    QString expectedPassword;
    QByteArray response = "ok";   // Ausgabe des Befehls
    int exitCode = 0;
    bool loginIsRoot = false;

    QString lastCommand;
    QByteArray typedBeforeReady;   // Eingaben vor der Bereit-Marke (Passwort)
    QByteArray typedAfterReady;    // Eingaben danach (Nutzdaten)

    ExecResult operator()(const QString &command, const SSHSession::PtyStep &step)
    {
        lastCommand = command;
        ExecResult r;
        if (!loginIsRoot) {
            r.out = "Password: ";
            typedBeforeReady += step(r.out);
            if (!typedBeforeReady.endsWith('\n')
                || QString::fromUtf8(typedBeforeReady.chopped(1)) != expectedPassword) {
                r.out += "\r\nsu: Authentication failure\r\n";
                r.exitStatus = 1;
                return r;
            }
            r.out += "\r\n";
        }
        r.out += "\x1eSSHIT-READY\x1e";
        typedAfterReady += step(r.out);
        r.out += response + QByteArray("\x1eSSHIT-END:") + QByteArray::number(exitCode) + "\x1e";
        step(r.out);
        r.exitStatus = 0;
        return r;
    }
};

} // namespace

TEST(runas, display_names_method_and_user)
{
    CHECK_EQ(RunAs{}.display(), QStringLiteral("sudo"));
    CHECK_EQ((RunAs{QStringLiteral("www-data"), RunAs::Method::Sudo}.display()),
             QStringLiteral("sudo: www-data"));
    CHECK_EQ((RunAs{QStringLiteral("bob"), RunAs::Method::Su}.display()), QStringLiteral("su: bob"));
    CHECK_EQ((RunAs{QString(), RunAs::Method::Su}.display()), QStringLiteral("su: root"));
}

TEST(runas, parses_passwd_and_flags_system_accounts)
{
    const auto users = parsePasswd(QStringLiteral(
        "bob:x:1001:1001:Bob,,,:/home/bob:/bin/bash\n"
        "root:x:0:0:root:/root:/bin/bash\n"
        "www-data:x:33:33:www-data:/var/www:/usr/sbin/nologin\n"
        "kaputt\n"
        "nobody:x:65534:65534:nobody:/nonexistent:/usr/sbin/nologin\n"));
    CHECK_EQ(users.size(), size_t(4));
    if (users.size() != 4)
        return;
    CHECK_EQ(users[0].name, QStringLiteral("root"));   // nach uid sortiert
    CHECK(!users[0].system());
    CHECK_EQ(users[1].name, QStringLiteral("www-data"));
    CHECK(users[1].system());
    CHECK_EQ(users[2].name, QStringLiteral("bob"));
    CHECK(!users[2].system());
    CHECK_EQ(users[2].home, QStringLiteral("/home/bob"));
    CHECK(users[3].system());   // nobody
}

TEST(runas, classifies_sudo_failures)
{
    CHECK(classifySudoFailure(QStringLiteral("bob is not in the sudoers file."))
          == SudoCheck::NotAllowed);
    CHECK(classifySudoFailure(QStringLiteral(
              "Sorry, user adm is not allowed to execute '/usr/bin/true' as carl on host."))
          == SudoCheck::NotAllowed);
    CHECK(classifySudoFailure(QStringLiteral("sh: 1: sudo: not found")) == SudoCheck::NotAllowed);
    CHECK(classifySudoFailure(QStringLiteral("Sorry, try again.\nsudo: 1 incorrect password attempt"))
          == SudoCheck::WrongPassword);
}

TEST(runas, su_sends_password_only_at_prompt_and_data_after_login)
{
    FakeSuTerminal term;
    term.expectedPassword = QStringLiteral("Geheim'1");
    term.response = "datei-inhalt";
    const QByteArray data("nutzdaten\x03\x04\r\n", 14);
    const ExecResult r =
        suExec(std::ref(term), QStringLiteral("carl"), QStringLiteral("Geheim'1"),
               QStringLiteral("tee -- '/tmp/x' > /dev/null"), data);
    CHECK_EQ(r.exitStatus, 0);
    CHECK_EQ(r.out, QByteArray("datei-inhalt"));
    CHECK_EQ(term.typedBeforeReady, QByteArray("Geheim'1\n"));
    CHECK_EQ(term.typedAfterReady, data);                 // Passwort nie in den Daten
    CHECK(!term.typedAfterReady.contains("Geheim"));
    CHECK(term.lastCommand.contains(QStringLiteral("su -c")));
    CHECK(term.lastCommand.contains(QStringLiteral("head -c 14")));
    CHECK(!term.lastCommand.contains(QStringLiteral("Geheim")));   // nicht auf der Kommandozeile
}

TEST(runas, su_reports_wrong_password_and_exit_codes)
{
    FakeSuTerminal term;
    term.expectedPassword = QStringLiteral("richtig");
    ExecResult r = suExec(std::ref(term), QStringLiteral("carl"), QStringLiteral("falsch"),
                          QStringLiteral("true"));
    CHECK(r.exitStatus != 0);
    CHECK(QString::fromUtf8(r.err).contains(QStringLiteral("Authentication failure")));

    FakeSuTerminal failing;
    failing.expectedPassword = QStringLiteral("pw");
    failing.response = "ls: cannot access '/x': Permission denied\n";
    failing.exitCode = 2;
    r = suExec(std::ref(failing), QStringLiteral("carl"), QStringLiteral("pw"),
               QStringLiteral("ls /x"));
    CHECK_EQ(r.exitStatus, 2);
    CHECK(QString::fromUtf8(r.err).contains(QStringLiteral("Permission denied")));

    // Als root: keine Passwort-Abfrage, erzwungene Shell (Dienstkonten).
    FakeSuTerminal asRoot;
    asRoot.loginIsRoot = true;
    r = suExec(std::ref(asRoot), QStringLiteral("www-data"), QString(), QStringLiteral("true"),
               {}, /*loginIsRoot=*/true);
    CHECK_EQ(r.exitStatus, 0);
    CHECK(asRoot.typedBeforeReady.isEmpty());
    CHECK(asRoot.lastCommand.contains(QStringLiteral("su -s /bin/sh")));
}

TEST(runas, sudo_filesystem_prefixes_target_user)
{
    QStringList commands;
    core::LocalFileSystem pathFs;
    SudoFileSystem fs(
        &pathFs,
        [&commands](const QString &command, const QByteArray &) {
            commands << command;
            ExecResult r;
            r.exitStatus = 0;
            r.out = "4\n";
            return r;
        },
        [] { return QString(); }, RunAs{QStringLiteral("www-data"), RunAs::Method::Sudo});
    CHECK_EQ(fs.size(QStringLiteral("/var/www/index.html")), qint64(4));
    CHECK_EQ(commands.size(), qsizetype(1));
    if (!commands.isEmpty())
        CHECK(commands.first().startsWith(QStringLiteral("sudo -n -u 'www-data' stat")));
    CHECK(fs.label.endsWith(QStringLiteral("(sudo: www-data)")));
}

TEST(runas, sudo_password_goes_through_terminal_not_stdin)
{
    // sudo -n verlangt eine Anmeldung (Zeitstempel gilt nicht ueber SSH-
    // Kanaele): der Provider meldet sich im Terminal an, die Daten folgen erst
    // nach der Bereit-Marke.
    QStringList execs;
    FakeSuTerminal term;
    term.expectedPassword = QStringLiteral("AdmPass1");
    term.response = "";
    core::LocalFileSystem pathFs;
    SudoFileSystem fs(
        &pathFs,
        [&execs](const QString &command, const QByteArray &) {
            execs << command;
            ExecResult r;
            r.exitStatus = 1;
            r.err = "sudo: interactive authentication is required";
            return r;
        },
        [] { return QStringLiteral("AdmPass1"); },
        RunAs{QStringLiteral("carl"), RunAs::Method::Sudo}, std::ref(term));
    const QByteArray data("inhalt\n");
    fs.writeBytes(QStringLiteral("/home/carl/a.txt"), data);
    CHECK_EQ(execs.size(), qsizetype(1));   // kein "sudo -v" mehr
    CHECK_EQ(term.typedBeforeReady, QByteArray("AdmPass1\n"));
    CHECK_EQ(term.typedAfterReady, data);
    CHECK(term.lastCommand.contains(QStringLiteral("sudo -u 'carl' /bin/sh -c")));
    CHECK(term.lastCommand.contains(QStringLiteral("tee -- ")));
}

TEST(runas, aborts_on_second_password_prompt)
{
    // Falsches Passwort: sudo-rs/su fragen erneut. Statt auf eine zweite
    // Eingabe zu warten (Zeitueberschreitung), wird mit Strg+C abgebrochen.
    QByteArray answers;
    const PtyExecFn pty = [&answers](const QString &, const SSHSession::PtyStep &step) {
        ExecResult r;
        r.out = "[sudo: authenticate] Password: ";
        answers += step(r.out);
        r.out += "\r\nsudo: Authentication failed, try again.\r\n[sudo: authenticate] Password: ";
        answers += step(r.out);
        r.out += "^C\r\n";
        r.exitStatus = 1;
        return r;
    };
    const ExecResult r = sudoPtyExec(pty, QString(), QStringLiteral("falsch"),
                                     QStringLiteral("true"));
    CHECK_EQ(answers, QByteArray("falsch\n\x03"));
    CHECK(r.exitStatus != 0);
    CHECK(QString::fromUtf8(r.err).contains(QStringLiteral("Authentication failed")));
    CHECK(!QString::fromUtf8(r.err).contains(QStringLiteral("falsch")));
}

TEST(runas, recognises_sudo_authentication_requests)
{
    CHECK(sudoNeedsAuthentication(QStringLiteral("sudo: a password is required")));
    CHECK(sudoNeedsAuthentication(QStringLiteral("sudo: interactive authentication is required")));
    CHECK(!sudoNeedsAuthentication(QStringLiteral("rm: cannot remove 'x': Permission denied")));
}

// ---------------------------------------------------------------------------
// Live-Tests gegen einen echten Server
// ---------------------------------------------------------------------------

namespace {

SSHSessionPtr liveSession(const QString &user, const QString &password)
{
    const QString target = qEnvironmentVariable("NCSSH_LIVE_SSH");
    if (target.isEmpty())
        return {};
    ServerProfile profile;
    profile.host = target.section(QLatin1Char(':'), 0, 0);
    profile.port = target.section(QLatin1Char(':'), 1, 1).toInt();
    profile.username = user;
    profile.authMethod = QStringLiteral("password");
    profile.password = password;
    profile.knownHostsPolicy = QStringLiteral("ignore");
    return connectSession(profile, nullptr);
}

PtyExecFn ptyOf(const SSHSessionPtr &session)
{
    return [session](const QString &command, const SSHSession::PtyStep &step) {
        return session->execPty(command, step);
    };
}

} // namespace

TEST(runas_live, su_without_sudo_rights)
{
    SSHSessionPtr bob = liveSession(QStringLiteral("sshitbob"), QStringLiteral("BobPass1"));
    if (!bob)
        return;   // kein Testserver konfiguriert

    const SwitchProbe probe = probeSwitch(bob, QStringLiteral("sshitcarl"));
    CHECK_EQ(probe.loginUser, QStringLiteral("sshitbob"));
    CHECK(!probe.loginIsRoot);
    CHECK(!probe.sudoGroup);
    CHECK(!probe.sudoWithoutPassword);
    CHECK(checkSudoAs(bob, QStringLiteral("sshitcarl"), QStringLiteral("BobPass1"))
          == SudoCheck::NotAllowed);

    // Falsches Passwort wird erkannt.
    ExecResult r = suExec(ptyOf(bob), QStringLiteral("sshitcarl"), QStringLiteral("falsch"),
                          QStringLiteral("true"));
    CHECK(r.exitStatus != 0);
    CHECK(QString::fromUtf8(r.err).contains(QStringLiteral("Authentication failure")));

    r = suExec(ptyOf(bob), QStringLiteral("sshitcarl"), QStringLiteral("CarlPass1"),
               QStringLiteral("id -un"));
    CHECK_EQ(r.exitStatus, 0);
    CHECK_EQ(r.out.trimmed(), QByteArray("sshitcarl"));

    // Provider als carl: Datei lesen, die nur carl lesen darf.
    bob->userPasswords[QStringLiteral("sshitcarl")] = QStringLiteral("CarlPass1");
    std::unique_ptr<SFTPFileSystem> sftp = bob->filesystem();
    SudoFileSystem fs(sftp.get(), bob, RunAs{QStringLiteral("sshitcarl"), RunAs::Method::Su});
    bool found = false;
    for (const core::FileEntry &e : fs.listDir(QStringLiteral("/home/sshitcarl")))
        found = found || e.name == QLatin1String("nur-carl.txt");
    CHECK(found);
    CHECK_EQ(fs.readBytes(QStringLiteral("/home/sshitcarl/nur-carl.txt")),
             QByteArray("geheim von carl\n"));
    CHECK(fs.isDir(QStringLiteral("/home/sshitcarl")));

    // Binaerdaten (alle Bytewerte inkl. Steuerzeichen) und eine groessere
    // Datei muessen unveraendert durch das Terminal gehen.
    QByteArray binary;
    for (int round = 0; round < 64; ++round)
        for (int b = 0; b < 256; ++b)
            binary.append(char(b));
    const QString binPath = QStringLiteral("/home/sshitcarl/bin.dat");
    fs.writeBytes(binPath, binary);
    CHECK_EQ(fs.size(binPath), qint64(binary.size()));
    CHECK(fs.readBytes(binPath) == binary);

    QByteArray big(2 * 1024 * 1024, Qt::Uninitialized);
    for (qsizetype i = 0; i < big.size(); ++i)
        big[i] = char(QRandomGenerator::global()->generate() & 0xff);
    const QString bigPath = QStringLiteral("/home/sshitcarl/big.dat");
    fs.writeBytes(bigPath, big);
    CHECK(fs.readBytes(bigPath, -1) == big);

    // Datei gehoert carl (uid aus der Benutzerliste).
    qint64 carlUid = -1;
    for (const UserAccount &a : listUsers(bob))
        if (a.name == QLatin1String("sshitcarl"))
            carlUid = a.uid;
    bool ownedByCarl = false;
    for (const core::FileEntry &e : fs.listDir(QStringLiteral("/home/sshitcarl")))
        if (e.name == QLatin1String("bin.dat"))
            ownedByCarl = e.owner == QString::number(carlUid);
    CHECK(ownedByCarl);

    fs.mkdir(QStringLiteral("/home/sshitcarl/neu"));
    fs.rename(QStringLiteral("/home/sshitcarl/neu"), QStringLiteral("/home/sshitcarl/neu2"));
    CHECK(fs.isDir(QStringLiteral("/home/sshitcarl/neu2")));
    fs.remove(QStringLiteral("/home/sshitcarl/neu2"), true);
    fs.remove(binPath);
    fs.remove(bigPath);
    CHECK(!fs.isDir(QStringLiteral("/home/sshitcarl/neu2")));
}

TEST(runas_live, sudo_u_with_own_password)
{
    SSHSessionPtr adm = liveSession(QStringLiteral("sshitadm"), QStringLiteral("AdmPass1"));
    if (!adm)
        return;
    const SwitchProbe probe = probeSwitch(adm, QStringLiteral("sshitcarl"));
    CHECK(probe.sudoGroup);
    CHECK(!probe.loginIsRoot);
    CHECK(checkSudoAs(adm, QStringLiteral("sshitcarl"), QStringLiteral("falsch"))
          == SudoCheck::WrongPassword);
    CHECK(checkSudoAs(adm, QStringLiteral("sshitcarl"), QStringLiteral("AdmPass1"))
          == SudoCheck::Ok);

    adm->sudoPassword = QStringLiteral("AdmPass1");
    std::unique_ptr<SFTPFileSystem> sftp = adm->filesystem();
    SudoFileSystem fs(sftp.get(), adm, RunAs{QStringLiteral("sshitcarl"), RunAs::Method::Sudo});
    CHECK_EQ(fs.readBytes(QStringLiteral("/home/sshitcarl/nur-carl.txt")),
             QByteArray("geheim von carl\n"));

    const auto users = listUsers(adm);
    bool root = false, carl = false;
    for (const UserAccount &a : users) {
        root = root || (a.name == QLatin1String("root") && a.uid == 0 && !a.system());
        carl = carl || (a.name == QLatin1String("sshitcarl") && !a.system());
    }
    CHECK(root);
    CHECK(carl);
}

TEST(runas_live, sudo_root_chip_with_password)
{
    // Der normale sudo-Chip (root) mit Passwort — scheiterte frueher, weil
    // der Zeitstempel von "sudo -v" im naechsten SSH-Kanal nicht mehr galt.
    SSHSessionPtr adm = liveSession(QStringLiteral("sshitadm"), QStringLiteral("AdmPass1"));
    if (!adm)
        return;
    adm->sudoPassword = QStringLiteral("AdmPass1");
    std::unique_ptr<SFTPFileSystem> sftp = adm->filesystem();
    SudoFileSystem fs(sftp.get(), adm);
    CHECK(fs.isDir(QStringLiteral("/root")));
    fs.writeText(QStringLiteral("/root/sshit-test.txt"), QStringLiteral("als root"));
    CHECK_EQ(fs.readText(QStringLiteral("/root/sshit-test.txt")), QStringLiteral("als root"));
    bool ownedByRoot = false;
    for (const core::FileEntry &e : fs.listDir(QStringLiteral("/root")))
        if (e.name == QLatin1String("sshit-test.txt"))
            ownedByRoot = e.owner == QLatin1String("0");
    CHECK(ownedByRoot);
    fs.remove(QStringLiteral("/root/sshit-test.txt"));

    // Falsches Passwort: klare Fehlermeldung statt Haengen.
    adm->sudoPassword = QStringLiteral("falsch");
    SudoFileSystem wrong(sftp.get(), adm);
    bool threw = false;
    try {
        wrong.listDir(QStringLiteral("/root"));
    } catch (const std::exception &) {
        threw = true;
    }
    CHECK(threw);
}

namespace {

template <typename Predicate>
bool pumpUntil(Predicate ready, int timeoutMs)
{
    QDeadlineTimer deadline(timeoutMs);
    while (!ready()) {
        if (deadline.hasExpired())
            return false;
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return true;
}

// Bedient Menues und Dialoge, waehrend der Workspace sie modal zeigt:
// waehlt im Benutzer-Menue den Eintrag, gibt im Passwort-Dialog das Passwort
// ein und merkt sich Warnungen (statt daran haengen zu bleiben).
struct UiDriver {
    QString pick;        // Menue-Eintrag (Anfang des Textes)
    QString password;    // fuer QInputDialog
    QStringList menuEntries;
    QStringList prompts;
    QStringList warnings;
    QTimer timer;

    UiDriver()
    {
        timer.setInterval(30);
        QObject::connect(&timer, &QTimer::timeout, [this] {
            if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
                menuEntries.clear();
                QAction *chosen = nullptr;
                for (QAction *a : menu->actions()) {
                    menuEntries << a->text();
                    if (!pick.isEmpty() && a->isEnabled() && a->text().startsWith(pick))
                        chosen = a;
                }
                menu->close();
                if (chosen)
                    chosen->trigger();
                return;
            }
            QWidget *modal = QApplication::activeModalWidget();
            if (auto *input = qobject_cast<QInputDialog *>(modal)) {
                prompts << input->labelText();
                input->setTextValue(password);
                input->accept();
            } else if (auto *box = qobject_cast<QMessageBox *>(modal)) {
                warnings << box->text();
                box->accept();
            }
        });
        timer.start();
    }
};

gui::FilePanel *connectedPanel(gui::Workspace &ws)
{
    for (gui::FilePanel *p : ws.findChildren<gui::FilePanel *>())
        if (p->provider() && p->provider()->isRemote)
            return p;
    return nullptr;
}

QPushButton *sudoChip(gui::FilePanel *panel)
{
    for (QPushButton *b : panel->findChildren<QPushButton *>())
        if (b->objectName() == QLatin1String("Chip") && b->isVisible()
            && (b->text() == QLatin1String("sudo") || b->text().startsWith(QLatin1String("su"))))
            return b;
    return nullptr;
}

bool listsFile(gui::FilePanel *panel, const QString &name)
{
    auto *table = panel->findChild<QTableWidget *>();
    for (int r = 0; table && r < table->rowCount(); ++r)
        if (table->item(r, 0) && table->item(r, 0)->data(Qt::UserRole).toString() == name)
            return true;
    return false;
}

} // namespace

TEST(runas_live, chip_menu_switches_pane_user)
{
    const QString target = qEnvironmentVariable("NCSSH_LIVE_SSH");
    if (target.isEmpty())
        return;
    for (const auto &[login, loginPassword, chipText, password] :
         {std::tuple{QStringLiteral("sshitbob"), QStringLiteral("BobPass1"),
                     QStringLiteral("su: sshitcarl"), QStringLiteral("CarlPass1")},
          std::tuple{QStringLiteral("sshitadm"), QStringLiteral("AdmPass1"),
                     QStringLiteral("sudo: sshitcarl"), QStringLiteral("AdmPass1")}}) {
        gui::AsyncBridge bridge;
        net::SessionManager sessions;
        gui::TransferManager transfers(&bridge);
        gui::Workspace ws(&bridge, &sessions, &transfers);
        ws.resize(1200, 800);
        ws.show();

        core::ServerProfile profile;
        profile.name = QStringLiteral("live-") + login;
        profile.host = target.section(QLatin1Char(':'), 0, 0);
        profile.port = target.section(QLatin1Char(':'), 1, 1).toInt();
        profile.username = login;
        profile.authMethod = QStringLiteral("password");
        profile.password = loginPassword;
        profile.knownHostsPolicy = QStringLiteral("ignore");
        profile.startPath = QStringLiteral("/tmp");
        UiDriver ui;
        ws.connectTo(profile);
        CHECK(pumpUntil([&] { return ws.isConnected() && connectedPanel(ws); }, 20000));
        gui::FilePanel *panel = connectedPanel(ws);
        if (!panel)
            continue;
        CHECK(pumpUntil([&] { return sudoChip(panel) != nullptr; }, 5000));
        QPushButton *chip = sudoChip(panel);
        if (!chip)
            continue;

        // Rechtsklick -> Menue -> sshitcarl -> Passwort -> Pane laeuft als carl.
        ui.pick = QStringLiteral("sshitcarl");
        ui.password = password;
        emit chip->customContextMenuRequested(QPoint(1, 1));
        CHECK(pumpUntil([&] { return panel->sudoLabel() == chipText; }, 30000));
        CHECK(ui.warnings.isEmpty());
        CHECK(ui.menuEntries.contains(login + QStringLiteral(" (angemeldet)")));
        CHECK(ui.menuEntries.contains(QStringLiteral("root")));
        CHECK_EQ(ui.prompts.size(), qsizetype(1));
        CHECK(panel->sudoActive());

        panel->navigateTo(QStringLiteral("/home/sshitcarl"));
        CHECK(pumpUntil([&] { return listsFile(panel, QStringLiteral("nur-carl.txt")); }, 20000));

        // Zurueck zum angemeldeten Benutzer ueber dasselbe Menue.
        ui.pick = login + QStringLiteral(" (angemeldet)");
        emit chip->customContextMenuRequested(QPoint(1, 1));
        CHECK(pumpUntil([&] { return !panel->sudoActive(); }, 10000));
        CHECK_EQ(panel->sudoLabel(), QStringLiteral("sudo"));
        CHECK(!panel->provider()->label.contains(QStringLiteral("(su")));
        if (!ui.warnings.isEmpty())
            CHECK_EQ(ui.warnings.join(QStringLiteral(" | ")), QString());
        ws.disconnectSession();
        pumpUntil([] { return false; }, 300);
    }
}
