// Runde 1 der Fehlersuche: Wege, auf denen Daten verloren gingen.
//  - F5 im selben lokalen Ordner leerte die Datei (Kopie auf sich selbst)
//  - lokal kam nie die Ueberschreib-Rueckfrage ("\\" statt "/" im Pfad)
//  - Loeschen folgte NTFS-Junctions und leerte deren Ziel
//  - Speichern leerte die Datei vorab (jetzt ueber eine Temp-Datei)
#include "tests/harness.hpp"

#include "ncssh/core/filesystem.hpp"
#include "ncssh/core/settings.hpp"
#include "ncssh/gui/bridge.hpp"
#include "ncssh/gui/file_panel.hpp"
#include "ncssh/gui/transfer_manager.hpp"
#include "ncssh/gui/workspace.hpp"
#include "ncssh/net/session.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QProcess>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

using namespace ncssh;

namespace {

template <typename Predicate>
bool pump(Predicate ready, int timeoutMs = 5000)
{
    QDeadlineTimer deadline(timeoutMs);
    while (!ready()) {
        if (deadline.hasExpired())
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return true;
}

void settle(int ms = 400)
{
    QDeadlineTimer deadline(ms);
    while (!deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
}

void writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(data);
}

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<fehlt>");
}

bool selectRow(gui::FilePanel *panel, const QString &name)
{
    auto *table = panel->findChild<QTableWidget *>();
    for (int r = 0; table && r < table->rowCount(); ++r)
        if (table->item(r, 0)->data(Qt::UserRole).toString() == name) {
            table->setCurrentCell(r, 0);
            table->selectRow(r);
            return true;
        }
    return false;
}

// Schliesst jeden auftauchenden modalen Dialog (Rueckfragen) und zaehlt sie.
// Schliessen = Abbrechen bzw. "Nein" — im Zweifel nichts ueberschreiben.
struct ModalCloser {
    QTimer timer;
    int seen = 0;
    QString lastText;
    ModalCloser()
    {
        QObject::connect(&timer, &QTimer::timeout, [this] {
            if (QWidget *w = QApplication::activeModalWidget()) {
                ++seen;
                if (auto *box = qobject_cast<QMessageBox *>(w))
                    lastText = box->text();
                w->close();
            }
        });
        timer.start(50);
    }
};

class ConfigGuard {
public:
    ConfigGuard() : m_old(qgetenv("APPDATA")) { qputenv("APPDATA", m_dir.path().toLocal8Bit()); }
    ~ConfigGuard() { qputenv("APPDATA", m_old); }

private:
    QTemporaryDir m_dir;
    QByteArray m_old;
};

} // namespace

TEST(dataloss, f5_into_same_local_folder_keeps_file)
{
    ConfigGuard cfg;
    core::setSetting(QStringLiteral("confirm_copy"), false);   // F5 ohne Dialog
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    const QString file = tmp.filePath(QStringLiteral("bericht.txt"));
    writeFile(file, "wertvoll");

    gui::AsyncBridge bridge;
    net::SessionManager sessions;
    gui::TransferManager transfers(&bridge);
    gui::Workspace ws(&bridge, &sessions, &transfers);
    ws.show();
    const QString native = QDir::toNativeSeparators(tmp.path());
    for (gui::FilePanel *p : {ws.leftPanel(), ws.rightPanel()})
        p->navigateTo(tmp.path());
    CHECK(pump([&] { return ws.leftPanel()->currentPath() == native
                            && ws.rightPanel()->currentPath() == native; }));
    CHECK(selectRow(ws.leftPanel(), QStringLiteral("bericht.txt")));

    ModalCloser closer;
    ws.leftPanel()->triggerOp(QStringLiteral("copy"));
    settle(800);
    CHECK_EQ(readFile(file), QByteArrayLiteral("wertvoll"));   // nicht geleert
    CHECK(closer.seen >= 1);                                     // "identisch" gemeldet
}

TEST(dataloss, local_overwrite_asks_first)
{
    ConfigGuard cfg;
    core::setSetting(QStringLiteral("confirm_copy"), false);
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    QDir().mkpath(tmp.filePath(QStringLiteral("a")));
    QDir().mkpath(tmp.filePath(QStringLiteral("b")));
    writeFile(tmp.filePath(QStringLiteral("a/x.txt")), "neu");
    writeFile(tmp.filePath(QStringLiteral("b/x.txt")), "alt");

    gui::AsyncBridge bridge;
    net::SessionManager sessions;
    gui::TransferManager transfers(&bridge);
    gui::Workspace ws(&bridge, &sessions, &transfers);
    ws.show();
    ws.leftPanel()->navigateTo(tmp.filePath(QStringLiteral("a")));
    ws.rightPanel()->navigateTo(tmp.filePath(QStringLiteral("b")));
    CHECK(pump([&] { return ws.leftPanel()->currentPath().endsWith(QLatin1Char('a'))
                            && ws.rightPanel()->currentPath().endsWith(QLatin1Char('b')); }));
    CHECK(selectRow(ws.leftPanel(), QStringLiteral("x.txt")));

    ModalCloser closer;   // Rueckfrage schliessen = Abbrechen
    ws.leftPanel()->triggerOp(QStringLiteral("copy"));
    CHECK(pump([&] { return closer.seen >= 1; }, 4000));
    settle(500);
    CHECK(closer.lastText.contains(QStringLiteral("x.txt")));    // Konflikt erkannt
    CHECK_EQ(readFile(tmp.filePath(QStringLiteral("b/x.txt"))), QByteArrayLiteral("alt"));
}

TEST(dataloss, deleting_junction_keeps_its_target)
{
#ifdef Q_OS_WIN
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    const QString target = tmp.filePath(QStringLiteral("quelle"));
    QDir().mkpath(target);
    writeFile(target + QStringLiteral("/code.cpp"), "int main(){}");
    const QString box = tmp.filePath(QStringLiteral("kiste"));
    QDir().mkpath(box);
    const QString junction = box + QStringLiteral("/link");
    const int rc = QProcess::execute(
        QStringLiteral("cmd.exe"),
        {QStringLiteral("/c"), QStringLiteral("mklink"), QStringLiteral("/J"),
         QDir::toNativeSeparators(junction), QDir::toNativeSeparators(target)});
    CHECK_EQ(rc, 0);
    CHECK(QFileInfo(junction).isJunction());

    core::LocalFileSystem fs;
    // Ordner MIT Junction darin rekursiv loeschen: das Ziel bleibt.
    fs.remove(box, true);
    CHECK(!QFileInfo::exists(box));
    CHECK_EQ(readFile(target + QStringLiteral("/code.cpp")), QByteArrayLiteral("int main(){}"));
#endif
}

TEST(dataloss, write_bytes_replaces_content)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    const QString file = tmp.filePath(QStringLiteral("c.txt"));
    writeFile(file, "lang lang lang");
    core::LocalFileSystem fs;
    fs.writeBytes(file, "kurz");
    CHECK_EQ(readFile(file), QByteArrayLiteral("kurz"));
    // Keine Temp-Reste neben der Datei.
    CHECK_EQ(QDir(tmp.path()).entryList(QDir::Files | QDir::Hidden).size(), 1);
}
