// Lebensdauer-Tests rund ums Tab-Schliessen: nichts darf nach dem Abbau eines
// Workspace noch auf seine Provider/Panes zeigen (interne Zwischenablage,
// nicht-modale Dialoge, wiederholbare Transfers). Offscreen, ohne Netzwerk.

#include "tests/harness.hpp"

#include "ncssh/core/filesystem.hpp"
#include "ncssh/gui/bridge.hpp"
#include "ncssh/gui/file_panel.hpp"
#include "ncssh/gui/transfer_manager.hpp"
#include "ncssh/gui/workspace.hpp"
#include "ncssh/net/session.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDialog>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QKeyEvent>
#include <QPointer>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <algorithm>

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

QString jobStatus(const gui::TransferManager &transfers, int jobId)
{
    const auto &jobs = transfers.jobs();
    auto it = std::find_if(jobs.begin(), jobs.end(),
                           [jobId](const net::TransferJob &j) { return j.id == jobId; });
    return it == jobs.end() ? QString() : it->status;
}

class ConfigGuard {
public:
    ConfigGuard() : m_old(qgetenv("APPDATA")) { qputenv("APPDATA", m_dir.path().toLocal8Bit()); }
    ~ConfigGuard() { qputenv("APPDATA", m_old); }

private:
    QTemporaryDir m_dir;
    QByteArray m_old;
};

} // namespace

// Strg+X in Tab A, Tab A schliessen: die Zwischenablage darf nicht mehr auf
// den freigegebenen Provider zeigen (Strg+V in Tab B griffe sonst ins Leere).
TEST(fix_lifetime, clipboard_forgets_provider_of_closed_tab)
{
    ConfigGuard cfg;
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    {
        QFile f(tmp.filePath(QStringLiteral("notiz.txt")));
        CHECK(f.open(QIODevice::WriteOnly));
        f.write("x");
    }

    gui::AsyncBridge bridge;
    net::SessionManager sessions;
    gui::TransferManager transfers(&bridge);
    auto *ws = new gui::Workspace(&bridge, &sessions, &transfers);
    ws->show();
    gui::FilePanel *panel = ws->leftPanel();
    panel->navigateTo(tmp.path());
    CHECK(pump([&] { return panel->currentPath() == QDir::toNativeSeparators(tmp.path()); }));
    CHECK(selectRow(panel, QStringLiteral("notiz.txt")));

    auto *table = panel->findChild<QTableWidget *>();
    CHECK(table != nullptr);
    if (!table) {
        delete ws;
        return;
    }
    QKeyEvent cut(QEvent::KeyPress, Qt::Key_X, Qt::ControlModifier);
    QApplication::sendEvent(table, &cut);
    CHECK(gui::FilePanel::clipboardProvider() == panel->provider());
    CHECK(!gui::FilePanel::clipboardPaths().isEmpty());
    CHECK(gui::FilePanel::clipboardIsMove());

    delete ws;
    CHECK(gui::FilePanel::clipboardProvider() == nullptr);
    CHECK(gui::FilePanel::clipboardPaths().isEmpty());
    CHECK(!gui::FilePanel::clipboardIsMove());
}

// Ein an den Tab gebundener nicht-modaler Dialog (Suche, Vergleich, Encoding)
// verschwindet mit dem Tab, statt mit toten Zeigern offen zu bleiben.
TEST(fix_lifetime, bound_dialog_closes_with_workspace)
{
    ConfigGuard cfg;
    gui::AsyncBridge bridge;
    net::SessionManager sessions;
    gui::TransferManager transfers(&bridge);
    auto *ws = new gui::Workspace(&bridge, &sessions, &transfers);

    auto *dlg = new QDialog;   // wie im Hauptfenster: NICHT Kind des Tabs
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
    ws->bindDialog(dlg);
    const QPointer<QDialog> guard(dlg);

    delete ws;
    CHECK(!guard || !guard->isVisible());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    CHECK(guard.isNull());
    delete guard.data();   // Aufraeumen, falls der Test fehlschlaegt
}

// Jobs eines geschlossenen Tabs duerfen nicht mehr wiederholt werden — ihre
// Provider-Zeiger gehoeren dem Tab.
TEST(fix_lifetime, transfers_of_closed_tab_are_not_retryable)
{
    ConfigGuard cfg;
    QTemporaryDir tmp;
    CHECK(tmp.isValid());

    gui::AsyncBridge bridge;
    net::SessionManager sessions;
    gui::TransferManager transfers(&bridge);
    auto *ws = new gui::Workspace(&bridge, &sessions, &transfers);
    core::FileSystemProvider *local = ws->localFs();
    CHECK(local != nullptr);
    CHECK(ws->ownedProviders().contains(local));

    // Quelle == Ziel -> transferWithProgress verweigert, der Job endet
    // deterministisch als Fehler (und waere wiederholbar).
    const QString file = QDir::toNativeSeparators(tmp.filePath(QStringLiteral("daten.bin")));
    {
        QFile f(file);
        CHECK(f.open(QIODevice::WriteOnly));
        f.write("inhalt");
    }
    const int id = transfers.enqueue(QStringLiteral("daten.bin"), local, file, local, file);
    CHECK(pump([&] {
        const QString s = jobStatus(transfers, id);
        return s == QLatin1String("error") || s == QLatin1String("done");
    }));
    CHECK_EQ(jobStatus(transfers, id), QStringLiteral("error"));
    CHECK(transfers.canRestart(id));
    CHECK_EQ(transfers.activeJobsFor(ws->ownedProviders()), 0);

    delete ws;
    CHECK(!transfers.canRestart(id));
    transfers.retry(id);   // darf nichts mehr starten
    CHECK_EQ(jobStatus(transfers, id), QStringLiteral("error"));
}
