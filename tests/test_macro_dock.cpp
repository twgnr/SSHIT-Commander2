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
