// Panes -> Status anzeigen: ohne Zwischenauswahl zeigt die aktive Pane den
// Status des Eintrags unter dem Cursor-Balken der anderen Pane und rechnet neu,
// sobald der Balken dort wandert. Zweiter Aufruf schaltet ab.
#include "tests/harness.hpp"

#include "ncssh/gui/bridge.hpp"
#include "ncssh/gui/file_panel.hpp"
#include "ncssh/gui/main_window.hpp"
#include "ncssh/gui/workspace.hpp"

#include <QAction>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextBrowser>
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

bool selectRowNamed(gui::FilePanel *panel, const QString &name)
{
    auto *table = panel->findChild<QTableWidget *>();
    for (int r = 0; table && r < table->rowCount(); ++r) {
        if (table->item(r, 0)->data(Qt::UserRole).toString() == name) {
            table->setCurrentCell(r, 0);
            return true;
        }
    }
    return false;
}

} // namespace

TEST(pane_status, follows_cursor_of_other_pane_without_chooser)
{
    const QByteArray oldAppData = qgetenv("APPDATA");
    QTemporaryDir cfg;
    CHECK(cfg.isValid());
    qputenv("APPDATA", cfg.path().toUtf8());

    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    CHECK(QDir().mkpath(tmp.path() + QStringLiteral("/projekt-ordner")));
    QFile f(tmp.path() + QStringLiteral("/bericht.pdf"));
    CHECK(f.open(QIODevice::WriteOnly));
    f.write(QByteArray(1234, 'x'));
    f.close();

    {
        gui::AsyncBridge bridge;
        gui::MainWindow main(&bridge);
        main.show();
        auto *ws = main.findChild<gui::Workspace *>();
        CHECK(ws != nullptr);
        if (!ws) {
            qputenv("APPDATA", oldAppData);
            return;
        }
        gui::FilePanel *left = ws->leftPanel();
        gui::FilePanel *right = ws->rightPanel();
        const QString native = QDir::toNativeSeparators(tmp.path());
        right->navigateTo(tmp.path());
        CHECK(pump([&] { return right->currentPath() == native; }));
        left->focusView();   // linke Pane aktiv -> sie zeigt den Status
        CHECK(selectRowNamed(right, QStringLiteral("bericht.pdf")));

        QAction *statusAction = nullptr;
        for (QAction *a : main.findChildren<QAction *>())
            if (a->text() == QStringLiteral("Status anzeigen"))
                statusAction = a;
        CHECK(statusAction != nullptr);
        if (!statusAction) {
            qputenv("APPDATA", oldAppData);
            return;
        }

        statusAction->trigger();
        auto *view = left->findChild<QTextBrowser *>();
        CHECK(view != nullptr);
        CHECK(left->statusShown());          // sofort, ohne Auswahlfenster
        CHECK(!right->statusShown());
        CHECK(pump([&] { return view->toPlainText().contains(QStringLiteral("bericht.pdf")); }));

        // Cursor in der anderen Pane auf den Ordner -> Status folgt.
        CHECK(selectRowNamed(right, QStringLiteral("projekt-ordner")));
        CHECK(pump([&] { return view->toPlainText().contains(QStringLiteral("projekt-ordner"))
                                && !view->toPlainText().contains(QStringLiteral("Wird berechnet")); }));

        // Zweiter Aufruf schaltet ab.
        statusAction->trigger();
        CHECK(!left->statusShown());
    }
    qputenv("APPDATA", oldAppData);
}
