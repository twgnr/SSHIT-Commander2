// Erweiterter Pane-Filter und mehrstufige Sortierung (Filter-Knopf der Pane).
#include "tests/harness.hpp"

#include "ncssh/core/filesystem.hpp"
#include "ncssh/core/panefilter.hpp"
#include "ncssh/gui/bridge.hpp"
#include "ncssh/gui/file_panel.hpp"
#include "ncssh/gui/pane_filter_dialog.hpp"

#include <QApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>

using namespace ncssh;
using core::EntryType;
using core::FileEntry;
using core::PaneFilter;

namespace {

const QDateTime kNow(QDate(2026, 10, 6), QTime(12, 0));

FileEntry file(const QString &name, qint64 size = 0, int daysAgo = 0)
{
    FileEntry e;
    e.name = name;
    e.type = EntryType::File;
    e.size = size;
    e.modified = kNow.addDays(-daysAgo);
    e.created = kNow.addDays(-daysAgo - 100);
    return e;
}

FileEntry dir(const QString &name, int daysAgo = 0)
{
    FileEntry e = file(name, 0, daysAgo);
    e.type = EntryType::Dir;
    return e;
}

bool passes(const PaneFilter &f, const FileEntry &e)
{
    return core::CompiledPaneFilter(f, kNow).matches(e);
}

QStringList names(const std::vector<FileEntry> &entries)
{
    QStringList out;
    for (const FileEntry &e : entries)
        out << e.name;
    return out;
}

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

} // namespace

TEST(pane_filter, empty_filter_passes_everything)
{
    PaneFilter f;
    CHECK(!f.isActive());
    CHECK(passes(f, file(QStringLiteral("a.txt"))));
    CHECK(passes(f, dir(QStringLiteral("ordner"))));
}

TEST(pane_filter, kind_files_or_dirs_only)
{
    PaneFilter f;
    f.kind = PaneFilter::Kind::FilesOnly;
    CHECK(passes(f, file(QStringLiteral("a.txt"))));
    CHECK(!passes(f, dir(QStringLiteral("ordner"))));
    f.kind = PaneFilter::Kind::DirsOnly;
    CHECK(!passes(f, file(QStringLiteral("a.txt"))));
    CHECK(passes(f, dir(QStringLiteral("ordner"))));
    // ".." bleibt immer, sonst kaeme man nicht mehr nach oben.
    FileEntry parent = dir(QStringLiteral(".."));
    parent.type = EntryType::Parent;
    f.kind = PaneFilter::Kind::FilesOnly;
    CHECK(passes(f, parent));
}

TEST(pane_filter, name_rules)
{
    PaneFilter f;
    f.names = QStringLiteral("bericht; *.log");
    CHECK(passes(f, file(QStringLiteral("Jahresbericht.pdf"))));   // enthaelt
    CHECK(passes(f, file(QStringLiteral("server.log"))));          // Platzhalter
    CHECK(!passes(f, file(QStringLiteral("server.log.1"))));
    CHECK(!passes(f, file(QStringLiteral("notiz.txt"))));

    PaneFilter s;
    s.startsWith = QStringLiteral("IMG_; DSC");
    s.endsWith = QStringLiteral(".jpg");
    CHECK(passes(s, file(QStringLiteral("img_0001.JPG"))));        // ohne Gross/klein
    CHECK(passes(s, file(QStringLiteral("DSC9.jpg"))));
    CHECK(!passes(s, file(QStringLiteral("IMG_1.png"))));
    s.caseSensitive = true;
    CHECK(!passes(s, file(QStringLiteral("img_0001.JPG"))));

    PaneFilter r;
    r.regex = QStringLiteral("^v\\d+\\.\\d+$");
    CHECK(passes(r, file(QStringLiteral("v1.20"))));
    CHECK(!passes(r, file(QStringLiteral("v1.20-beta"))));
    r.regex = QStringLiteral("(");
    CHECK(!r.validate().isEmpty());
}

TEST(pane_filter, extensions_and_size_only_affect_files)
{
    PaneFilter f;
    f.extensions = QStringLiteral(".txt, LOG;cpp");
    CHECK(passes(f, file(QStringLiteral("a.TXT"))));
    CHECK(passes(f, file(QStringLiteral("b.log"))));
    CHECK(!passes(f, file(QStringLiteral("c.md"))));
    CHECK(!passes(f, file(QStringLiteral("Makefile"))));
    CHECK(passes(f, dir(QStringLiteral("src"))));   // Ordner bleiben navigierbar

    PaneFilter s;
    s.minSize = 1024;
    s.maxSize = 4096;
    CHECK(!passes(s, file(QStringLiteral("klein"), 100)));
    CHECK(passes(s, file(QStringLiteral("mittel"), 2048)));
    CHECK(passes(s, file(QStringLiteral("grenze"), 4096)));
    CHECK(!passes(s, file(QStringLiteral("gross"), 5000)));
    CHECK(passes(s, dir(QStringLiteral("ordner"))));
    s.minSize = 5000;
    CHECK(!s.validate().isEmpty());   // ab > bis
}

TEST(pane_filter, date_rules)
{
    PaneFilter newer;
    newer.dateMode = PaneFilter::DateMode::NewerThan;
    newer.amount = 7;
    newer.unit = QStringLiteral("days");
    CHECK(passes(newer, file(QStringLiteral("neu"), 0, 2)));
    CHECK(!passes(newer, file(QStringLiteral("alt"), 0, 30)));

    PaneFilter older = newer;
    older.dateMode = PaneFilter::DateMode::OlderThan;
    older.unit = QStringLiteral("weeks");
    older.amount = 2;
    CHECK(!passes(older, file(QStringLiteral("neu"), 0, 2)));
    CHECK(passes(older, file(QStringLiteral("alt"), 0, 30)));

    PaneFilter range;
    range.dateMode = PaneFilter::DateMode::Between;
    range.from = kNow.addDays(-10);
    range.to = kNow.addDays(-5);
    CHECK(passes(range, file(QStringLiteral("drin"), 0, 7)));
    CHECK(!passes(range, file(QStringLiteral("zu neu"), 0, 1)));
    // Erstelldatum statt Aenderung: file() legt es 100 Tage frueher an.
    range.dateField = QStringLiteral("created");
    CHECK(!passes(range, file(QStringLiteral("drin"), 0, 7)));
    CHECK(passes(range, file(QStringLiteral("alt geaendert"), 0, -93)));

    // Ordner: nur mit applyToDirs betroffen.
    CHECK(passes(newer, dir(QStringLiteral("alter Ordner"), 30)));
    newer.applyToDirs = true;
    CHECK(!passes(newer, dir(QStringLiteral("alter Ordner"), 30)));

    // Unbekanntes Datum erfuellt keinen Zeitraum.
    FileEntry unknown = file(QStringLiteral("x"));
    unknown.modified = QDateTime();
    newer.applyToDirs = false;
    CHECK(!passes(newer, unknown));
}

TEST(pane_filter, multi_level_sort)
{
    std::vector<FileEntry> entries{
        file(QStringLiteral("b"), 10, 1), file(QStringLiteral("a"), 10, 3),
        file(QStringLiteral("c"), 5, 2),  dir(QStringLiteral("z")),
        file(QStringLiteral("a2"), 5, 1),
    };
    FileEntry parent = dir(QStringLiteral(".."));
    parent.type = EntryType::Parent;
    entries.push_back(parent);

    // Erst Groesse absteigend, dann Name aufsteigend.
    core::sortEntries(entries, {{QStringLiteral("size"), false}, {QStringLiteral("name"), true}});
    CHECK_EQ(names(entries), (QStringList{QStringLiteral(".."), QStringLiteral("z"),
                                          QStringLiteral("a"), QStringLiteral("b"),
                                          QStringLiteral("a2"), QStringLiteral("c")}));

    // Erst Groesse aufsteigend, dann Datum (aelteste zuerst); Ordner nicht vorn.
    core::sortEntries(entries, {{QStringLiteral("size"), true}, {QStringLiteral("modified"), true}},
                      /*dirsFirst=*/false);
    CHECK_EQ(names(entries), (QStringList{QStringLiteral(".."), QStringLiteral("z"),
                                          QStringLiteral("c"), QStringLiteral("a2"),
                                          QStringLiteral("a"), QStringLiteral("b")}));
}

TEST(pane_filter, dialog_round_trips_settings)
{
    PaneFilter f;
    f.kind = PaneFilter::Kind::FilesOnly;
    f.names = QStringLiteral("*.log");
    f.startsWith = QStringLiteral("srv");
    f.endsWith = QStringLiteral(".log");
    f.regex = QStringLiteral("\\d");
    f.caseSensitive = true;
    f.extensions = QStringLiteral("log");
    f.dateMode = PaneFilter::DateMode::OlderThan;
    f.dateField = QStringLiteral("created");
    f.amount = 3;
    f.unit = QStringLiteral("months");
    f.minSize = 1536;
    f.maxSize = 2 * 1024 * 1024;
    f.applyToDirs = true;
    const QList<core::SortKey> keys{{QStringLiteral("modified"), false},
                                    {QStringLiteral("name"), true}};
    const QList<QPair<QString, QString>> columns{{QStringLiteral("name"), QStringLiteral("Name")},
                                                 {QStringLiteral("size"), QStringLiteral("Größe")},
                                                 {QStringLiteral("modified"), QStringLiteral("Geändert")}};
    gui::PaneFilterDialog dlg(f, keys, false, columns);
    CHECK(dlg.filter() == f);
    CHECK(dlg.sortKeys() == keys);
    CHECK(!dlg.dirsFirst());
}

TEST(pane_filter, pane_applies_filter_and_highlights_chip)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    for (const QString &name : {QStringLiteral("a.txt"), QStringLiteral("b.log"),
                                QStringLiteral("c.txt")}) {
        QFile f(tmp.filePath(name));
        CHECK(f.open(QIODevice::WriteOnly));
        f.write("x");
    }
    CHECK(QDir(tmp.path()).mkdir(QStringLiteral("sub")));

    gui::AsyncBridge bridge;
    core::LocalFileSystem fs;
    gui::FilePanel panel(&bridge, QStringLiteral("Test"));
    panel.setProvider(&fs, tmp.path());
    const QString path = QDir::toNativeSeparators(QDir::cleanPath(tmp.path()));
    CHECK(pump([&] { return panel.currentPath() == path; }));

    auto *table = panel.findChild<QTableWidget *>();
    CHECK(table != nullptr);
    if (!table)
        return;
    const int all = table->rowCount();

    QPushButton *chip = nullptr;
    for (QPushButton *b : panel.findChildren<QPushButton *>()) {
        if (b->objectName() == QLatin1String("Chip") && b->isCheckable()
            && b->icon().isNull() == false)
            chip = b;
    }
    CHECK(chip != nullptr);
    if (chip)
        CHECK(!chip->isChecked());

    PaneFilter f;
    f.extensions = QStringLiteral("txt");
    panel.setViewOptions(f, {{QStringLiteral("name"), false}}, true);
    // "sub" bleibt (Ordner), b.log verschwindet.
    CHECK_EQ(table->rowCount(), all - 1);
    if (chip)
        CHECK(chip->isChecked());
    // Absteigend nach Name: c.txt vor a.txt (Ordner und ".." davor).
    QStringList shown;
    for (int r = 0; r < table->rowCount(); ++r)
        shown << table->item(r, 0)->data(Qt::UserRole).toString();
    CHECK(shown.indexOf(QStringLiteral("c.txt")) < shown.indexOf(QStringLiteral("a.txt")));
    CHECK(!shown.contains(QStringLiteral("b.log")));

    // Filter bleibt beim Ordnerwechsel erhalten.
    panel.navigateTo(tmp.filePath(QStringLiteral("sub")));
    CHECK(pump([&] { return panel.currentPath().endsWith(QStringLiteral("sub")); }));
    CHECK(panel.paneFilter() == f);

    panel.setViewOptions(PaneFilter{}, {core::SortKey{}}, true);
    if (chip)
        CHECK(!chip->isChecked());
}
