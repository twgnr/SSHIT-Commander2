// Absturz auf einem Arbeitslaptop: Beim Ziehen der Trennlinie wurde
// settings.json pro Pixel geschrieben; war sie (Virenscanner) kurz gesperrt,
// warf atomicWriteText durch Qt hindurch -> App beendet. Zudem hatte ein
// gesperrtes LESEN alle uebrigen Einstellungen geloescht.
#include "tests/harness.hpp"

#include "ncssh/config.hpp"
#include "ncssh/core/settings.hpp"
#include "ncssh/gui/bridge.hpp"
#include "ncssh/gui/transfer_manager.hpp"
#include "ncssh/gui/workspace.hpp"
#include "ncssh/net/session.hpp"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSplitter>
#include <QTemporaryDir>
#include <QThread>
#include <thread>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

using namespace ncssh;

namespace {

class IsolatedConfig {
public:
    IsolatedConfig() : m_old(qgetenv("APPDATA")) { qputenv("APPDATA", m_dir.path().toUtf8()); }
    ~IsolatedConfig() { qputenv("APPDATA", m_old); }
    bool valid() const { return m_dir.isValid(); }

private:
    QTemporaryDir m_dir;
    QByteArray m_old;
};

QString settingsFile()
{
    return ncssh::configDir() + QStringLiteral("/settings.json");
}

#ifdef Q_OS_WIN
// Wie ein Virenscanner: Datei exklusiv oeffnen (kein Lesen/Schreiben fuer andere).
HANDLE lockExclusive(const QString &path)
{
    return CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ, 0, nullptr,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
}
#endif

void pumpFor(int ms)
{
    QDeadlineTimer deadline(ms);
    while (!deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
}

} // namespace

#ifdef Q_OS_WIN
TEST(settings_lock, locked_file_is_never_overwritten_with_a_single_key)
{
    IsolatedConfig cfg;
    CHECK(cfg.valid());
    core::setSetting(QStringLiteral("theme"), QStringLiteral("Hell"));
    core::setSetting(QStringLiteral("language"), QStringLiteral("en"));

    HANDLE lock = lockExclusive(settingsFile());
    CHECK(lock != INVALID_HANDLE_VALUE);
    bool threw = false;
    try {
        core::setSetting(QStringLiteral("console_splits"), QJsonArray{1, 2});
    } catch (const std::exception &) {
        threw = true;   // lieber Fehler als Datenverlust
    }
    CloseHandle(lock);
    CHECK(threw);
    // Alle bisherigen Einstellungen sind noch da.
    CHECK_EQ(core::getSettingString(QStringLiteral("theme")), QStringLiteral("Hell"));
    CHECK_EQ(core::getSettingString(QStringLiteral("language")), QStringLiteral("en"));
}

TEST(settings_lock, short_lock_is_waited_out)
{
    IsolatedConfig cfg;
    CHECK(cfg.valid());
    core::setSetting(QStringLiteral("theme"), QStringLiteral("Hell"));
    HANDLE lock = lockExclusive(settingsFile());
    CHECK(lock != INVALID_HANDLE_VALUE);
    // Sperre nach kurzer Zeit wieder freigeben (wie ein Scan, der endet).
    std::thread release([lock] {
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        CloseHandle(lock);
    });
    bool threw = false;
    try {
        core::setSetting(QStringLiteral("language"), QStringLiteral("en"));
    } catch (const std::exception &) {
        threw = true;
    }
    release.join();
    CHECK(!threw);
    CHECK_EQ(core::getSettingString(QStringLiteral("theme")), QStringLiteral("Hell"));
    CHECK_EQ(core::getSettingString(QStringLiteral("language")), QStringLiteral("en"));
}
#endif

TEST(settings_lock, corrupt_settings_file_is_backed_up)
{
    IsolatedConfig cfg;
    CHECK(cfg.valid());
    QFile f(settingsFile());
    CHECK(f.open(QIODevice::WriteOnly));
    f.write("{ kaputt");
    f.close();
    core::setSetting(QStringLiteral("theme"), QStringLiteral("Hell"));
    CHECK_EQ(core::getSettingString(QStringLiteral("theme")), QStringLiteral("Hell"));
    const QStringList backups =
        QDir(ncssh::configDir()).entryList({QStringLiteral("settings.json.kaputt-*")});
    CHECK_EQ(backups.size(), 1);
}

TEST(settings_lock, splitter_drag_saves_once_after_it_rests)
{
    IsolatedConfig cfg;
    CHECK(cfg.valid());
    gui::AsyncBridge bridge;
    net::SessionManager sessions;
    gui::TransferManager transfers(&bridge);
    gui::Workspace ws(&bridge, &sessions, &transfers);
    ws.resize(1000, 700);
    ws.show();
    QSplitter *column = nullptr;
    for (QSplitter *s : ws.findChildren<QSplitter *>())
        if (s->orientation() == Qt::Vertical)
            column = s;
    CHECK(column != nullptr);
    if (!column)
        return;
    core::setSetting(QStringLiteral("console_splits"), QJsonArray());
    // Ziehen: viele splitterMoved in kurzer Folge.
    for (int y = 200; y < 400; y += 5)
        emit column->splitterMoved(y, 1);
    QCoreApplication::processEvents();
    CHECK(core::getSetting(QStringLiteral("console_splits")).toList().isEmpty());   // noch nicht
    pumpFor(800);
    CHECK(!core::getSetting(QStringLiteral("console_splits")).toList().isEmpty());  // jetzt einmal
}
