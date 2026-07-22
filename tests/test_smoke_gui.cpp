// GUI-Rauchtest: baut die echten Widgets auf der Offscreen-Plattform auf und
// fuehrt die wichtigsten Bedienpfade durch.  (Port von smoke_gui.py)
//
// Anders als das Original testet diese Datei NUR die Oberflaeche — die
// Kernlogik (bulkrename, diff, secaudit, ansi, ppk …) ist bereits durch die
// uebrigen Testdateien abgedeckt und wird hier nicht doppelt geprueft.
#include "tests/harness.hpp"

#include "ncssh/core/filesystem.hpp"
#include "ncssh/core/i18n.hpp"
#include "ncssh/core/settings.hpp"
#include "ncssh/gui/ansi.hpp"
#include "ncssh/gui/bridge.hpp"
#include "ncssh/gui/bulk_rename_dialog.hpp"
#include "ncssh/gui/file_panel.hpp"
#include "ncssh/gui/settings_dialog.hpp"
#include "ncssh/gui/style.hpp"
#include "ncssh/gui/terminal_widget.hpp"

#include <QApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QHeaderView>
#include <QLineEdit>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>

using namespace ncssh;

namespace {

// Ereignisschleife pumpen, bis fertig() wahr ist (oder die Zeit ablaeuft).
// Die Bridge liefert Ergebnisse ueber Queued Connections — ohne Pumpen kaeme
// im Test nie etwas an.
template <typename Predicate>
bool pumpUntil(Predicate ready, int timeoutMs = 5000)
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

// Temp-Verzeichnis mit vorhersagbarem Inhalt.
void makeTree(const QString &dir)
{
    QDir().mkpath(dir + QStringLiteral("/unterordner"));
    const char *names[] = {"datei1.txt", "datei2.txt", "datei10.txt", "bild.png", ".versteckt"};
    for (const char *name : names) {
        QFile f(dir + QLatin1Char('/') + QString::fromLatin1(name));
        f.open(QIODevice::WriteOnly);
        f.write("x");
        f.close();
    }
}

// Zeilen der Tabelle als Namensliste (Spalte 0 traegt den Rohnamen in UserRole).
QStringList rowNames(QTableWidget *table)
{
    QStringList out;
    for (int r = 0; r < table->rowCount(); ++r)
        out << table->item(r, 0)->data(Qt::UserRole).toString();
    return out;
}

} // namespace

TEST(smoke_gui, theme_is_applied)
{
    gui::applyTheme(qobject_cast<QApplication *>(QCoreApplication::instance()));
    CHECK(!qobject_cast<QApplication *>(QCoreApplication::instance())->styleSheet().isEmpty());
    CHECK(!gui::themeNames().isEmpty());
}

TEST(smoke_gui, file_panel_loads_and_marks)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    makeTree(tmp.path());

    gui::AsyncBridge bridge;
    core::LocalFileSystem fs;
    gui::FilePanel panel(&bridge, QStringLiteral("Test"));
    panel.setProvider(&fs, tmp.path());
    CHECK(pumpUntil([&] { return panel.currentPath() == tmp.path(); }));

    auto *table = panel.findChild<QTableWidget *>();
    CHECK(table != nullptr);
    if (!table)
        return;
    CHECK(table->rowCount() > 0);

    // Alles markieren -> ".." bleibt aussen vor.
    panel.selectAllMarks();
    const auto marked = panel.selectedPaths();
    CHECK(marked.size() > 0);
    for (const QString &p : marked)
        CHECK(!p.endsWith(QLatin1String("..")));

    panel.clearMarks();
    CHECK_EQ(panel.selectedPaths().size(), size_t(0));

    // Umkehren nach leerer Auswahl markiert alles Markierbare.
    panel.invertMarks();
    CHECK_EQ(panel.selectedPaths().size(), marked.size());
}

TEST(smoke_gui, file_panel_natural_sort_and_columns)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    makeTree(tmp.path());
    core::setSetting(QStringLiteral("natural_sort"), true);

    gui::AsyncBridge bridge;
    core::LocalFileSystem fs;
    gui::FilePanel panel(&bridge, QStringLiteral("Test"));
    panel.setProvider(&fs, tmp.path());
    CHECK(pumpUntil([&] { return panel.currentPath() == tmp.path(); }));

    auto *table = panel.findChild<QTableWidget *>();
    CHECK(table != nullptr);
    if (!table)
        return;

    // datei2 muss vor datei10 stehen (natuerliche Sortierung).
    const QStringList names = rowNames(table);
    const int i2 = names.indexOf(QStringLiteral("datei2.txt"));
    const int i10 = names.indexOf(QStringLiteral("datei10.txt"));
    CHECK(i2 >= 0);
    CHECK(i10 >= 0);
    CHECK(i2 < i10);

    // Die Spaltenzahl folgt der Auswahl: Name + sichtbare Spalten.
    CHECK_EQ(table->columnCount(), panel.visibleColumns().size() + 1);
}

TEST(smoke_gui, file_panel_filter_and_hidden)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    makeTree(tmp.path());

    gui::AsyncBridge bridge;
    core::LocalFileSystem fs;
    gui::FilePanel panel(&bridge, QStringLiteral("Test"));
    panel.setProvider(&fs, tmp.path());
    CHECK(pumpUntil([&] { return panel.currentPath() == tmp.path(); }));

    auto *table = panel.findChild<QTableWidget *>();
    CHECK(table != nullptr);
    if (!table)
        return;
    const int all = table->rowCount();

    // Filter ueber das (versteckte) Filterfeld setzen.
    auto edits = panel.findChildren<QLineEdit *>();
    QLineEdit *filterEdit = nullptr;
    for (QLineEdit *edit : edits) {
        if (edit->placeholderText().contains(QStringLiteral("Filter")))
            filterEdit = edit;
    }
    CHECK(filterEdit != nullptr);
    if (!filterEdit)
        return;
    filterEdit->setText(QStringLiteral("*.txt"));
    CHECK(table->rowCount() < all);
    for (const QString &name : rowNames(table)) {
        if (name != QLatin1String(".."))
            CHECK(name.endsWith(QStringLiteral(".txt")));
    }
    filterEdit->clear();
    CHECK_EQ(table->rowCount(), all);
}

TEST(smoke_gui, file_panel_history_navigation)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    makeTree(tmp.path());
    const QString sub = tmp.path() + QStringLiteral("/unterordner");

    gui::AsyncBridge bridge;
    core::LocalFileSystem fs;
    gui::FilePanel panel(&bridge, QStringLiteral("Test"));
    panel.setProvider(&fs, tmp.path());
    CHECK(pumpUntil([&] { return panel.currentPath() == tmp.path(); }));
    CHECK(!panel.canGoBack());

    panel.navigateTo(sub);
    CHECK(pumpUntil([&] { return panel.currentPath() == sub; }));
    CHECK(panel.canGoBack());
    CHECK(!panel.canGoForward());

    panel.goBack();
    CHECK(pumpUntil([&] { return panel.currentPath() == tmp.path(); }));
    CHECK(panel.canGoForward());

    panel.goForward();
    CHECK(pumpUntil([&] { return panel.currentPath() == sub; }));
    CHECK(!panel.canGoForward());
}

TEST(smoke_gui, bulk_rename_dialog_previews)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    makeTree(tmp.path());

    gui::AsyncBridge bridge;
    core::LocalFileSystem fs;
    const std::vector<QString> names = {QStringLiteral("datei1.txt"),
                                        QStringLiteral("datei2.txt"),
                                        QStringLiteral("datei10.txt")};
    gui::BulkRenameDialog dlg(&bridge, &fs, tmp.path(), names);

    // Die Vorschau-Tabelle muss fuer jeden Namen eine Zeile haben.
    auto *preview = dlg.findChild<QTableWidget *>();
    CHECK(preview != nullptr);
    if (!preview)
        return;
    CHECK_EQ(preview->rowCount(), int(names.size()));
    // Ohne Regeln aendert sich nichts — alt == neu.
    for (int r = 0; r < preview->rowCount(); ++r)
        CHECK_EQ(preview->item(r, 0)->text(), preview->item(r, 1)->text());
}

TEST(smoke_gui, settings_dialog_builds)
{
    // Baut alle drei Reiter samt Farbwaehler und Modell-Download-Bereich auf.
    gui::SettingsDialog dlg(nullptr, nullptr);
    CHECK(!dlg.windowTitle().isEmpty());
    CHECK(dlg.findChildren<QWidget *>().size() > 20);
}

TEST(smoke_gui, terminal_renders_search_and_logs)
{
    gui::AsyncBridge bridge;
    gui::TerminalWidget terminal(&bridge);
    terminal.resize(600, 300);

    // Ohne Backend nimmt das Widget keine Ausgabe an — deshalb hier den
    // Renderer direkt fuettern, wie es der Backend-Callback taete.
    gui::AnsiRenderer renderer(&terminal);
    renderer.feed(QStringLiteral("\x1b[32mgruener text\x1b[0m\r\nzweite zeile\r\n"));
    const QString shown = terminal.toPlainText();
    CHECK(shown.contains(QStringLiteral("gruener text")));
    CHECK(!shown.contains(QStringLiteral("\x1b")));   // Steuerzeichen gefiltert

    // Suche im Rollpuffer
    terminal.setSearch(QStringLiteral("zeile"));
    CHECK_EQ(terminal.searchStatus(), QStringLiteral("1/1"));
    terminal.setSearch(QStringLiteral("nicht-vorhanden"));
    CHECK_EQ(terminal.searchStatus(), core::_t("keine Treffer"));
    terminal.clearSearch();
    CHECK(terminal.searchStatus().isEmpty());

    // Mitschnitt
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    const QString logPath = tmp.filePath(QStringLiteral("term.log"));
    CHECK(terminal.startLogging(logPath));
    CHECK(terminal.isLogging());
    CHECK_EQ(terminal.logPath(), logPath);
    terminal.stopLogging();
    CHECK(!terminal.isLogging());
    CHECK(QFile::exists(logPath));
}
