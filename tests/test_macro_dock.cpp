// Prueft das Andocken der Makroleiste: "Rechts" waehlen und in den Ausfuehren-
// Modus wechseln muss ein sichtbares QDockWidget mit dem Tastenraster erzeugen.
// Genau dieser Pfad wurde als "angedockte Tasten erscheinen nicht" gemeldet.
#include "tests/harness.hpp"

#include "ncssh/gui/bridge.hpp"
#include "ncssh/gui/macro_manager_dialog.hpp"

#include <QComboBox>
#include <QCoreApplication>
#include <QDockWidget>
#include <QMainWindow>
#include <QPushButton>
#include <QTemporaryDir>

using namespace ncssh;

TEST(macro_dock, right_dock_shows_keys_in_run_mode)
{
    // Konfiguration in ein Temp-Verzeichnis isolieren (keine echte macros.json).
    const QByteArray oldAppData = qgetenv("APPDATA");
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    qputenv("APPDATA", tmp.path().toUtf8());

    gui::AsyncBridge bridge;
    QMainWindow main;
    main.show();

    auto *dlg = new gui::MacroManagerDialog(&bridge, {}, {}, &main);
    dlg->present();  // startet schwebend (Bearbeiten-Modus)
    QCoreApplication::processEvents();

    // Andockseite "Rechts" waehlen — soll sofort andocken (wechselt selbst in
    // den Ausfuehren-Modus, da Bearbeiten immer schwebend ist).
    auto *combo = dlg->findChild<QComboBox *>(QStringLiteral("MacroDockCombo"));
    CHECK(combo != nullptr);
    if (!combo) {
        qputenv("APPDATA", oldAppData);
        return;
    }
    combo->setCurrentIndex(combo->findData(QStringLiteral("right")));
    QCoreApplication::processEvents();

    auto *dock = main.findChild<QDockWidget *>(QStringLiteral("MacroManagerDock"));
    CHECK(dock != nullptr);
    if (dock) {
        CHECK(main.dockWidgetArea(dock) == Qt::RightDockWidgetArea);
        CHECK(dock->widget() != nullptr);
        // Das Tastenraster muss im angedockten Inhalt vorhanden sein.
        CHECK(dock->findChildren<gui::KeyTile *>().size() > 0);
        // Angedockt: nur Tasten — Modus-Knopf ausgeblendet.
        auto *mb = dock->findChild<QPushButton *>(QStringLiteral("MacroModeButton"));
        CHECK(mb != nullptr);
        if (mb)
            CHECK(!mb->isVisible());
    }

    qputenv("APPDATA", oldAppData);
}

namespace {

// Dockt die Makroleiste an side an und verkleinert das Hauptfenster auf
// size. Liefert true, wenn das Fenster die Groesse annehmen konnte und Tasten
// am Rand abgeschnitten (statt das Fenster festzuhalten) werden.
bool shrinksAndClips(const QString &side, const QSize &size)
{
    gui::AsyncBridge bridge;
    QMainWindow main;
    main.setCentralWidget(new QWidget(&main));
    main.resize(1400, 1000);
    main.show();

    auto *dlg = new gui::MacroManagerDialog(&bridge, {}, {}, &main);
    dlg->present();
    QCoreApplication::processEvents();
    auto *combo = dlg->findChild<QComboBox *>(QStringLiteral("MacroDockCombo"));
    if (!combo)
        return false;
    combo->setCurrentIndex(combo->findData(side));
    QCoreApplication::processEvents();
    auto *dock = main.findChild<QDockWidget *>(QStringLiteral("MacroManagerDock"));
    if (!dock)
        return false;

    main.resize(size);
    for (int i = 0; i < 5; ++i)
        QCoreApplication::processEvents();

    // Fenster hat die kleine Groesse wirklich angenommen ...
    const bool shrunk = main.height() <= size.height() + 2 && main.width() <= size.width() + 2;
    // ... und mindestens eine Taste ragt ueber den sichtbaren Dock-Bereich.
    bool clipped = false;
    for (gui::KeyTile *tile : dock->findChildren<gui::KeyTile *>()) {
        const QRect r(tile->mapTo(dock, QPoint(0, 0)), tile->size());
        if (r.bottom() > dock->height() || r.right() > dock->width())
            clipped = true;
    }
    return shrunk && clipped;
}

} // namespace

TEST(macro_dock, docked_keys_clip_instead_of_blocking_window_height)
{
    const QByteArray oldAppData = qgetenv("APPDATA");
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    qputenv("APPDATA", tmp.path().toUtf8());
    // Standardraster 4 x 8 Tasten a 96 px ist deutlich hoeher als 200 px.
    CHECK(shrinksAndClips(QStringLiteral("right"), QSize(1400, 200)));
    qputenv("APPDATA", oldAppData);
}

TEST(macro_dock, docked_keys_clip_instead_of_blocking_window_width)
{
    const QByteArray oldAppData = qgetenv("APPDATA");
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    qputenv("APPDATA", tmp.path().toUtf8());
    // Oben angedockt: 8 Spalten a 96 px passen nicht in 300 px Breite.
    CHECK(shrinksAndClips(QStringLiteral("top"), QSize(300, 1000)));
    qputenv("APPDATA", oldAppData);
}
