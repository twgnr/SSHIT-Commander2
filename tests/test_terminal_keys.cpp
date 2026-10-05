// Terminal-Bedienung und Pane-Navigation:
//  - Tab bleibt im Terminal (Vervollstaendigung der Shell statt Fokuswechsel)
//  - relativer Startpfad (".") wird aufgeloest -> ".." vorhanden
//  - Vor/Zurueck beginnt beim Wechsel des Dateisystems neu
#include "tests/harness.hpp"

#include "ncssh/core/filesystem.hpp"
#include "ncssh/gui/bridge.hpp"
#include "ncssh/gui/file_panel.hpp"
#include "ncssh/gui/terminal_widget.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QVBoxLayout>

using namespace ncssh;

namespace {

template <typename Predicate>
bool pump(Predicate ready, int timeoutMs = 8000)
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

void press(QWidget *w, int key, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QKeyEvent ev(QEvent::KeyPress, key, mods);
    QApplication::sendEvent(w, &ev);
}

// Lokales Dateisystem, das relative Pfade auf ein festes Verzeichnis abbildet
// — so verhaelt sich SFTP ("." -> Login-Verzeichnis).
class RelativeFs : public core::LocalFileSystem {
public:
    explicit RelativeFs(QString base) : m_base(std::move(base)) {}
    QString normalize(const QString &path) const override { return path; }
    QString resolve(const QString &path) override
    {
        return QDir::isAbsolutePath(path) ? path : QDir::toNativeSeparators(m_base);
    }

private:
    QString m_base;
};

bool hasParentRow(gui::FilePanel &panel)
{
    auto *table = panel.findChild<QTableWidget *>();
    for (int r = 0; table && r < table->rowCount(); ++r)
        if (table->item(r, 0)->data(Qt::UserRole).toString() == QStringLiteral(".."))
            return true;
    return false;
}

} // namespace

// Merkt sich, ob Tab bei keyPressEvent ankommt (dort geht er an die Shell).
class ProbeTerminal : public gui::TerminalWidget {
public:
    using gui::TerminalWidget::TerminalWidget;
    bool sawTab = false;

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Tab)
            sawTab = true;
        event->accept();   // wie mit laufender Shell: Taste verbraucht
    }
};

TEST(terminal_keys, tab_reaches_terminal_instead_of_focus_chain)
{
    // Bisher nahm Qt Tab VOR keyPressEvent fuer den Fokuswechsel — die Shell
    // bekam ihn nie, Vervollstaendigung (cd b<Tab>) ging nicht.
    gui::AsyncBridge bridge;
    QWidget window;
    auto *layout = new QVBoxLayout(&window);
    auto *terminal = new ProbeTerminal(&bridge, &window);
    auto *other = new QLineEdit(&window);
    layout->addWidget(terminal);
    layout->addWidget(other);
    window.resize(600, 400);
    window.show();
    QApplication::setActiveWindow(&window);
    terminal->setFocus();
    CHECK(pump([&] { return terminal->hasFocus(); }, 2000));
    press(terminal, Qt::Key_Tab);
    QCoreApplication::processEvents();
    CHECK(terminal->sawTab);
    CHECK(terminal->hasFocus());
}

TEST(terminal_keys, relative_start_path_resolved_and_history_reset)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    const QString base = tmp.path() + QStringLiteral("/home/user");
    CHECK(QDir().mkpath(base + QStringLiteral("/backup")));

    gui::AsyncBridge bridge;
    RelativeFs remoteLike(base);
    gui::FilePanel panel(&bridge, QStringLiteral("Test"));
    panel.setProvider(&remoteLike, QStringLiteral("."));
    CHECK(pump([&] { return panel.currentPath() == QDir::toNativeSeparators(base); }));
    CHECK(hasParentRow(panel));   // eine Ebene hoeher moeglich

    // Verlauf aufbauen, dann System wechseln -> kein Zurueck mehr.
    panel.navigateTo(base + QStringLiteral("/backup"));
    CHECK(pump([&] { return panel.currentPath().endsWith(QStringLiteral("backup")); }));
    CHECK(panel.canGoBack());
    core::LocalFileSystem local;
    panel.setProvider(&local, tmp.path());
    CHECK(pump([&] { return panel.currentPath() == QDir::toNativeSeparators(tmp.path()); }));
    CHECK(!panel.canGoBack());
    CHECK(!panel.canGoForward());
}

TEST(pane_keys, enter_opens_directory_under_cursor)
{
    // Enter in der Liste tat nichts (nur Doppelklick war verdrahtet). Er muss
    // den Ordner unter dem CURSOR betreten — auch wenn woanders markiert ist —
    // und ".." eine Ebene hoeher gehen.
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    CHECK(QDir().mkpath(tmp.path() + QStringLiteral("/alpha")));
    CHECK(QDir().mkpath(tmp.path() + QStringLiteral("/beta")));

    gui::AsyncBridge bridge;
    core::LocalFileSystem local;
    gui::FilePanel panel(&bridge, QStringLiteral("Test"));
    panel.setProvider(&local, tmp.path());
    const QString root = QDir::toNativeSeparators(tmp.path());
    CHECK(pump([&] { return panel.currentPath() == root && hasParentRow(panel); }));
    auto *table = panel.findChild<QTableWidget *>();
    CHECK(table != nullptr);
    if (!table)
        return;
    auto rowOf = [&](const QString &name) {
        for (int r = 0; r < table->rowCount(); ++r)
            if (table->item(r, 0)->data(Qt::UserRole).toString() == name)
                return r;
        return -1;
    };
    const int alpha = rowOf(QStringLiteral("alpha"));
    const int beta = rowOf(QStringLiteral("beta"));
    CHECK(alpha >= 0 && beta >= 0);

    // "alpha" markiert, Cursor auf "beta".
    QItemSelectionModel *sel = table->selectionModel();
    sel->select(table->model()->index(alpha, 0),
                QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    sel->setCurrentIndex(table->model()->index(beta, 0), QItemSelectionModel::NoUpdate);
    press(table, Qt::Key_Return);
    CHECK(pump([&] { return panel.currentPath().endsWith(QStringLiteral("beta")); }));

    // ".." per Enter (Ziffernblock-Enter) -> zurueck nach oben.
    CHECK(pump([&] { return hasParentRow(panel); }));
    table->setCurrentCell(rowOf(QStringLiteral("..")), 0);
    press(table, Qt::Key_Enter, Qt::KeypadModifier);
    CHECK(pump([&] { return panel.currentPath() == root; }));
}
