// Pane: Sortierpfeil im Spaltenkopf und Rechtsklick-Verlauf an Vor/Zurueck.
#include "tests/harness.hpp"

#include "ncssh/core/filesystem.hpp"
#include "ncssh/gui/bridge.hpp"
#include "ncssh/gui/file_panel.hpp"

#include <QAction>
#include <QApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QHeaderView>
#include <QMenu>
#include <QPushButton>
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
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return true;
}

QString native(const QString &path)
{
    return QDir::toNativeSeparators(QDir::cleanPath(path));
}

QString headerText(QTableWidget *table, int column)
{
    QTableWidgetItem *item = table->horizontalHeaderItem(column);
    return item ? item->text() : QString();
}

} // namespace

TEST(pane_sort_history, header_shows_sort_column_and_direction)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    gui::AsyncBridge bridge;
    core::LocalFileSystem fs;
    gui::FilePanel panel(&bridge, QStringLiteral("Test"));
    panel.setProvider(&fs, tmp.path());
    CHECK(pump([&] { return panel.currentPath() == native(tmp.path()); }));

    auto *table = panel.findChild<QTableWidget *>();
    CHECK(table != nullptr);
    if (!table || table->columnCount() < 2)
        return;
    // Standard: nach Name aufsteigend.
    CHECK(headerText(table, 0).endsWith(QStringLiteral(" ▲")));
    CHECK(!headerText(table, 1).contains(QStringLiteral("▲")));

    QHeaderView *header = table->horizontalHeader();
    emit header->sectionClicked(0);   // gleiche Spalte -> absteigend
    CHECK(headerText(table, 0).endsWith(QStringLiteral(" ▼")));

    emit header->sectionClicked(1);   // andere Spalte -> dort aufsteigend
    CHECK(!headerText(table, 0).contains(QStringLiteral("▼")));
    CHECK(!headerText(table, 0).contains(QStringLiteral("▲")));
    CHECK(headerText(table, 1).endsWith(QStringLiteral(" ▲")));
}

TEST(pane_sort_history, history_buttons_list_recent_targets)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    const QString a = tmp.path();
    const QString b = a + QStringLiteral("/b");
    const QString c = b + QStringLiteral("/c");
    CHECK(QDir().mkpath(c));

    gui::AsyncBridge bridge;
    core::LocalFileSystem fs;
    gui::FilePanel panel(&bridge, QStringLiteral("Test"));
    panel.setProvider(&fs, a);
    CHECK(pump([&] { return panel.currentPath() == native(a); }));
    panel.navigateTo(b);
    CHECK(pump([&] { return panel.currentPath() == native(b); }));
    panel.navigateTo(c);
    CHECK(pump([&] { return panel.currentPath() == native(c); }));

    // Zurueck: naechstliegendes Ziel zuerst.
    CHECK_EQ(panel.backHistory(), (QStringList{native(b), native(a)}));
    CHECK(panel.forwardHistory().isEmpty());

    // Rechtsklick auf "Zurueck" zeigt die Ziele; das zweite waehlen.
    auto *back = panel.findChild<QPushButton *>(QStringLiteral("PaneBack"));
    CHECK(back != nullptr);
    if (!back)
        return;
    QStringList shown;
    QTimer::singleShot(0, [&shown] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!menu)
            return;
        const QList<QAction *> actions = menu->actions();
        for (QAction *action : actions)
            shown << action->toolTip();
        if (actions.size() > 1)
            actions.at(1)->trigger();
        menu->close();
    });
    emit back->customContextMenuRequested(QPoint(1, 1));
    CHECK_EQ(shown, (QStringList{native(b), native(a)}));
    CHECK(pump([&] { return panel.currentPath() == native(a); }));

    // Jetzt liegen beide Ziele vorn, das naechste zuerst.
    CHECK(panel.backHistory().isEmpty());
    CHECK_EQ(panel.forwardHistory(), (QStringList{native(b), native(c)}));
    panel.goHistory(2);
    CHECK(pump([&] { return panel.currentPath() == native(c); }));
}
