// Spaltenbreiten eines Tabs duerfen sich beim Navigieren nicht aendern — nur
// der Nutzer bestimmt sie ueber den Splitter. Ausloeser war ein Verzeichnis mit
// 64-stelligem Hash-Namen (z. B. ~/.cache/electron/<sha256>).
#include "tests/harness.hpp"

#include "ncssh/gui/bridge.hpp"
#include "ncssh/gui/console_panel.hpp"
#include "ncssh/gui/file_panel.hpp"
#include "ncssh/gui/transfer_manager.hpp"
#include "ncssh/gui/workspace.hpp"
#include "ncssh/net/session.hpp"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QSplitter>
#include <QTemporaryDir>
#include <QThread>

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

void settle()
{
    QDeadlineTimer deadline(300);
    while (!deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
}

} // namespace

TEST(workspace_layout, column_widths_stable_when_entering_long_dir)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    const QString hash = QStringLiteral(
        "0bb2a552b63feacad81a0c34a460be1e22038a1fa7d013aeb54e89ffa2280cd8");
    const QString deep = tmp.path() + QStringLiteral("/.cache/electron/") + hash;
    CHECK(QDir().mkpath(deep));
    QFile f(deep + QStringLiteral("/electron-v33.2.1-linux-x64.zip"));
    CHECK(f.open(QIODevice::WriteOnly));
    f.close();

    gui::AsyncBridge bridge;
    net::SessionManager sessions;
    gui::TransferManager transfers(&bridge);
    gui::Workspace ws(&bridge, &sessions, &transfers);
    ws.resize(1200, 800);
    ws.show();

    QSplitter *columns = nullptr;
    for (QSplitter *s : ws.findChildren<QSplitter *>())
        if (s->orientation() == Qt::Horizontal && s->count() == 2)
            columns = s;
    CHECK(columns != nullptr);
    if (!columns)
        return;

    gui::FilePanel *right = ws.rightPanel();
    right->navigateTo(tmp.path());
    CHECK(pump([&] { return right->currentPath() == QDir::toNativeSeparators(tmp.path()); }));
    settle();
    const QList<int> before = columns->sizes();

    right->navigateTo(deep);
    CHECK(pump([&] { return right->currentPath() == QDir::toNativeSeparators(deep); }));
    settle();
    const QList<int> after = columns->sizes();

    CHECK(before == after);
}
