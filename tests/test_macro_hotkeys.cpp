// Globale Kuerzel der Makro-Tasten. Gemeldet als "Kuerzel (z. B. Strg+S)
// eingestellt, Druecken bewirkt nichts" — das Feld wurde gespeichert, aber nie
// registriert. Geprueft wird die Abbildung auf Windows-Tasten, die Registrierung
// und der Weg WM_HOTKEY -> Makro-Aktion.
#include "tests/harness.hpp"

#include "ncssh/core/macros.hpp"
#include "ncssh/gui/bridge.hpp"
#include "ncssh/gui/global_hotkeys.hpp"
#include "ncssh/gui/macro_manager_dialog.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

using namespace ncssh;

#ifdef Q_OS_WIN

TEST(macro_hotkeys, maps_key_sequences_to_windows_keys)
{
    using gui::GlobalHotkeys;
    const auto ctrlS = GlobalHotkeys::toNative(QKeySequence(QStringLiteral("Ctrl+S")));
    CHECK(ctrlS.has_value());
    if (ctrlS) {
        CHECK_EQ(ctrlS->first, unsigned(MOD_CONTROL));
        CHECK_EQ(ctrlS->second, unsigned('S'));
    }
    const auto f5 = GlobalHotkeys::toNative(QKeySequence(QStringLiteral("Ctrl+Alt+Shift+F5")));
    CHECK(f5.has_value());
    if (f5) {
        CHECK_EQ(f5->first, unsigned(MOD_CONTROL | MOD_ALT | MOD_SHIFT));
        CHECK_EQ(f5->second, unsigned(VK_F5));
    }
    const auto meta = GlobalHotkeys::toNative(QKeySequence(QStringLiteral("Meta+7")));
    CHECK(meta.has_value());
    if (meta) {
        CHECK_EQ(meta->first, unsigned(MOD_WIN));
        CHECK_EQ(meta->second, unsigned('7'));
    }
    CHECK(!GlobalHotkeys::toNative(QKeySequence()).has_value());
}

TEST(macro_hotkeys, registers_and_reports_activation)
{
    gui::GlobalHotkeys hotkeys;
    CHECK(hotkeys.nativeHandle() != 0);
    const QKeySequence seq(QStringLiteral("Ctrl+Alt+Shift+F23"));
    CHECK(hotkeys.add(7, seq));

    // Dieselbe Kombination ist danach belegt.
    gui::GlobalHotkeys other;
    CHECK(!other.add(1, seq));

    int got = -1;
    QObject::connect(&hotkeys, &gui::GlobalHotkeys::activated, [&got](int id) { got = id; });
    PostMessageW(reinterpret_cast<HWND>(hotkeys.nativeHandle()), WM_HOTKEY, 7, 0);
    QCoreApplication::processEvents();
    CHECK_EQ(got, 7);

    // Nach clear() ist sie wieder frei.
    hotkeys.clear();
    CHECK(other.add(1, seq));
}

TEST(macro_hotkeys, shortcut_runs_macro_action)
{
    const QByteArray oldAppData = qgetenv("APPDATA");
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    qputenv("APPDATA", tmp.path().toUtf8());

    // Taste mit Kuerzel in einem NICHT angezeigten Layer — globale Kuerzel
    // gelten fuer alle Layer.
    core::macros::MacroConfig config = core::macros::MacroConfig::makeDefault();
    core::macros::Layer &other = config.addLayer(QStringLiteral("other"));
    QJsonObject key = core::macros::newKey(QStringLiteral("ssh_command"),
                                           QStringLiteral("echo hotkey"));
    key.insert(QStringLiteral("shortcut"), QStringLiteral("Ctrl+Alt+Shift+F22"));
    other.setKey(3, key);
    core::macros::save(config);
    CHECK(gui::MacroManagerDialog::hasGlobalShortcuts());

    QString sent;
    {
        gui::AsyncBridge bridge;
        gui::MacroManagerDialog dlg(
            &bridge, [&sent](const QString &cmd, bool) { sent = cmd; }, {});
        auto *hotkeys = dlg.findChild<gui::GlobalHotkeys *>();
        CHECK(hotkeys != nullptr);
        if (hotkeys) {
            // Die einzige registrierte Kombination hat ID 0.
            PostMessageW(reinterpret_cast<HWND>(hotkeys->nativeHandle()), WM_HOTKEY, 0, 0);
            QElapsedTimer timer;
            timer.start();
            while (sent.isEmpty() && timer.elapsed() < 5000)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        CHECK_EQ(sent, QStringLiteral("echo hotkey"));
    }

    qputenv("APPDATA", oldAppData);
}

#endif
