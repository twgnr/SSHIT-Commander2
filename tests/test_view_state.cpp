// Fenster merken sich Groesse und Spaltenansicht: Spalte verbreitern, Fenster
// vergroessern, schliessen, neu oeffnen -> alles wie zuvor.
#include "tests/harness.hpp"

#include "ncssh/gui/command_palette.hpp"
#include "ncssh/gui/view_state.hpp"

#include <QCoreApplication>
#include <QHeaderView>
#include <QTableWidget>
#include <QTemporaryDir>

using namespace ncssh;

TEST(view_state, dialog_reopens_with_same_size_and_column_widths)
{
    const QByteArray oldAppData = qgetenv("APPDATA");
    QTemporaryDir cfg;
    CHECK(cfg.isValid());
    qputenv("APPDATA", cfg.path().toUtf8());

    // Nur an den Testdialogen anmelden — andere Tests bleiben unberuehrt.
    gui::ViewStateKeeper keeper;
    int widthBefore = 0;
    {
        gui::CommandPalette palette(QStringLiteral("posix"));
        palette.installEventFilter(&keeper);
        palette.show();
        QCoreApplication::processEvents();
        auto *table = palette.findChild<QTableWidget *>();
        CHECK(table != nullptr);
        if (!table) {
            qputenv("APPDATA", oldAppData);
            return;
        }
        widthBefore = table->horizontalHeader()->sectionSize(0);
        table->horizontalHeader()->resizeSection(0, widthBefore + 123);
        palette.resize(731, 457);   // passt auch auf den 800x600-Testbildschirm
        palette.hide();   // -> speichern
    }
    {
        gui::CommandPalette palette(QStringLiteral("posix"));
        palette.installEventFilter(&keeper);
        palette.show();   // -> wiederherstellen
        QCoreApplication::processEvents();
        auto *table = palette.findChild<QTableWidget *>();
        CHECK(table != nullptr);
        if (table)
            CHECK_EQ(table->horizontalHeader()->sectionSize(0), widthBefore + 123);
        CHECK_EQ(palette.width(), 731);
        CHECK_EQ(palette.height(), 457);
    }
    qputenv("APPDATA", oldAppData);
}
