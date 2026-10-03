// Konsole: Tab schaltet durch mehrere Treffer, weitere Terminals als Reiter,
// und der orange sudo-Rahmen der Pane.
#include "tests/harness.hpp"

#include "ncssh/core/filesystem.hpp"
#include "ncssh/gui/bridge.hpp"
#include "ncssh/gui/console_panel.hpp"
#include "ncssh/gui/file_panel.hpp"
#include "ncssh/gui/terminal_widget.hpp"
#include "ncssh/gui/transfer_manager.hpp"
#include "ncssh/gui/workspace.hpp"
#include "ncssh/net/session.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QAction>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
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

void pressTab(QLineEdit *input)
{
    QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    QApplication::sendEvent(input, &tab);
}

QPushButton *buttonWithText(QWidget *parent, const QString &text)
{
    for (QPushButton *b : parent->findChildren<QPushButton *>())
        if (b->text() == text)
            return b;
    return nullptr;
}

} // namespace

TEST(console_tabs, tab_cycles_through_matching_directories)
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    QDir(dir.path()).mkdir(QStringLiteral("logs"));
    QDir(dir.path()).mkdir(QStringLiteral("lager"));
    QDir(dir.path()).mkdir(QStringLiteral("other"));
    QFile file(dir.filePath(QStringLiteral("liste.txt")));
    CHECK(file.open(QIODevice::WriteOnly));
    file.close();

    gui::AsyncBridge bridge;
    core::LocalFileSystem fs;
    gui::ConsolePanel console(&bridge, QStringLiteral("Test"));
    console.setCompletionProvider(&fs);
    console.setCwd(dir.path());
    auto *input = console.findChild<QLineEdit *>();
    CHECK(input != nullptr);

    const QString sep = QString(QDir::separator());
    input->setText(QStringLiteral("cd l"));
    input->setCursorPosition(input->text().size());
    pressTab(input);
    CHECK(pump([&] { return input->text() != QLatin1String("cd l"); }));
    // Alphabetisch, nur Verzeichnisse (cd): lager -> logs -> wieder lager.
    CHECK_EQ(input->text(), QStringLiteral("cd lager") + sep);
    pressTab(input);
    CHECK_EQ(input->text(), QStringLiteral("cd logs") + sep);
    pressTab(input);
    CHECK_EQ(input->text(), QStringLiteral("cd lager") + sep);

    // Anderer Befehl: Verzeichnisse zuerst, dann Dateien.
    input->setText(QStringLiteral("cat l"));
    input->setCursorPosition(input->text().size());
    pressTab(input);
    CHECK(pump([&] { return input->text() != QLatin1String("cat l"); }));
    CHECK_EQ(input->text(), QStringLiteral("cat lager") + sep);
    pressTab(input);
    pressTab(input);
    CHECK_EQ(input->text(), QStringLiteral("cat liste.txt "));
}

TEST(console_tabs, plus_opens_additional_terminal_tabs)
{
    gui::AsyncBridge bridge;
    gui::ConsolePanel console(&bridge, QStringLiteral("Test"));
    console.resize(600, 400);
    console.show();
    auto *tabs = console.findChild<QTabWidget *>();
    CHECK(tabs != nullptr);
    CHECK_EQ(tabs->count(), 1);
    QPushButton *plus = buttonWithText(&console, QStringLiteral("+"));
    CHECK(plus != nullptr);
    CHECK(!plus->isVisible());   // nur im Terminal-Modus

    QPushButton *mode = buttonWithText(&console, QStringLiteral("Terminal"));
    CHECK(mode != nullptr);
    mode->setChecked(true);
    CHECK(plus->isVisible());
    plus->click();
    CHECK_EQ(tabs->count(), 2);
    CHECK_EQ(tabs->currentIndex(), 1);
    for (int i = 0; i < tabs->count(); ++i)
        CHECK(static_cast<gui::TerminalWidget *>(tabs->widget(i))->isRunning());

    // Systemwechsel: zusaetzliche Terminals gehoeren zur alten Verbindung.
    console.setSession(nullptr);
    CHECK_EQ(tabs->count(), 1);
    console.shutdownShell();
}

TEST(console_tabs, active_sudo_pane_gets_sudo_property)
{
    gui::AsyncBridge bridge;
    gui::FilePanel panel(&bridge, QStringLiteral("Test"));
    CHECK(!panel.property("sudo").toBool());
    panel.setSudoAvailable(true);
    panel.setSudoActive(true);
    CHECK(panel.property("sudo").toBool());
    // Verbindung weg -> sudo aus -> Rahmen wieder normal.
    panel.setSudoAvailable(false);
    CHECK(!panel.property("sudo").toBool());
}

TEST(console_tabs, sudo_marks_console_of_the_same_side)
{
    gui::AsyncBridge bridge;
    net::SessionManager sessions;
    gui::TransferManager transfers(&bridge);
    gui::Workspace ws(&bridge, &sessions, &transfers);
    auto sudoConsoles = [&] {
        int n = 0;
        for (gui::ConsolePanel *c : ws.findChildren<gui::ConsolePanel *>())
            n += c->property("sudo").toBool() ? 1 : 0;
        return n;
    };
    gui::FilePanel *panel = ws.activePanel();
    CHECK(panel != nullptr);
    CHECK_EQ(sudoConsoles(), 0);
    panel->setSudoAvailable(true);
    panel->setSudoActive(true);
    CHECK_EQ(sudoConsoles(), 1);
    panel->setSudoActive(false);
    CHECK_EQ(sudoConsoles(), 0);
}

TEST(console_tabs, double_click_renames_terminal_tab)
{
    gui::AsyncBridge bridge;
    gui::ConsolePanel console(&bridge, QStringLiteral("Test"));
    auto *tabs = console.findChild<QTabWidget *>();
    CHECK(tabs != nullptr);
    const auto editor = [&] {
        return tabs->tabBar()->findChild<QLineEdit *>(QStringLiteral("TerminalTabEditor"));
    };

    emit tabs->tabBarDoubleClicked(0);
    QLineEdit *edit = editor();
    CHECK(edit != nullptr);
    CHECK_EQ(edit->text(), tabs->tabText(0));
    edit->setText(QStringLiteral("  Logs  "));
    emit edit->editingFinished();
    CHECK_EQ(tabs->tabText(0), QStringLiteral("Logs"));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    // Esc verwirft die Eingabe.
    emit tabs->tabBarDoubleClicked(0);
    edit = editor();
    CHECK(edit != nullptr);
    edit->setText(QStringLiteral("Verworfen"));
    CHECK(!edit->actions().isEmpty());
    edit->actions().first()->trigger();
    CHECK_EQ(tabs->tabText(0), QStringLiteral("Logs"));

    // Systemwechsel behaelt einen selbst vergebenen Namen.
    console.setSession(nullptr);
    CHECK_EQ(tabs->tabText(0), QStringLiteral("Logs"));
}
