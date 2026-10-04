// 1.0.3: Inline-Umbenennen zeigt den Namen ganz, Suchdialog ohne lose
// Kaestchen, Vergleichen nur mit Datei links UND rechts.
#include "tests/harness.hpp"

#include "ncssh/core/filesystem.hpp"
#include "ncssh/gui/bridge.hpp"
#include "ncssh/gui/file_panel.hpp"
#include "ncssh/gui/search_dialog.hpp"
#include "ncssh/gui/transfer_manager.hpp"
#include "ncssh/gui/workspace.hpp"
#include "ncssh/net/session.hpp"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QTableWidget>
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

int rowOf(QTableWidget *table, const QString &name)
{
    for (int r = 0; table && r < table->rowCount(); ++r)
        if (table->item(r, 0)->data(Qt::UserRole).toString() == name)
            return r;
    return -1;
}

bool selectByName(gui::FilePanel *panel, const QString &name)
{
    auto *table = panel->findChild<QTableWidget *>();
    if (!pump([&] { return rowOf(table, name) >= 0; }))
        return false;
    const int row = rowOf(table, name);
    table->setCurrentCell(row, 0);
    table->selectRow(row);
    return true;
}

void touch(const QString &path)
{
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write("x");
}

} // namespace

TEST(round103, inline_rename_field_fits_the_name)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    QDir(tmp.path()).mkdir(QStringLiteral("AMD"));
    gui::AsyncBridge bridge;
    core::LocalFileSystem fs;
    gui::FilePanel panel(&bridge, QStringLiteral("Test"));
    panel.resize(600, 400);
    panel.show();
    panel.setProvider(&fs, tmp.path());
    CHECK(selectByName(&panel, QStringLiteral("AMD")));
    auto *table = panel.findChild<QTableWidget *>();
    panel.beginInlineRename(table->currentRow());
    auto *editor = table->viewport()->findChild<QLineEdit *>(QStringLiteral("InlineRenameEditor"));
    CHECK(editor != nullptr);
    if (!editor)
        return;
    // Hoch genug fuer die Schrift samt Rand (vorher schnitt das Padding ab).
    CHECK(editor->height() >= editor->fontMetrics().height() + 8);
    CHECK(editor->contentsRect().height() >= editor->fontMetrics().height());
    CHECK_EQ(editor->text(), QStringLiteral("AMD"));
}

TEST(round103, name_search_has_no_floating_content_options)
{
    gui::AsyncBridge bridge;
    gui::SearchDialog name(&bridge, QStringLiteral("name"), QDir::homePath());
    name.show();
    for (QCheckBox *box : name.findChildren<QCheckBox *>()) {
        // Jedes sichtbare Kaestchen muss in einem Layout stecken (nicht bei 0,0).
        if (box->isVisible())
            CHECK(box->parentWidget()->layout() != nullptr && box->pos() != QPoint(0, 0));
    }
    bool binaryShown = false;
    for (QCheckBox *box : name.findChildren<QCheckBox *>())
        binaryShown = binaryShown || (box->isVisible() && box->text().contains(QStringLiteral("Bin")));
    CHECK(!binaryShown);

    gui::SearchDialog content(&bridge, QStringLiteral("content"), QDir::homePath());
    content.show();
    bool binaryInContent = false;
    for (QCheckBox *box : content.findChildren<QCheckBox *>())
        binaryInContent = binaryInContent || (box->isVisible() && box->text().contains(QStringLiteral("Bin")));
    CHECK(binaryInContent);
}

TEST(round103, compare_needs_a_file_on_both_sides)
{
    QTemporaryDir left, right;
    CHECK(left.isValid() && right.isValid());
    touch(left.path() + QStringLiteral("/a.txt"));
    touch(right.path() + QStringLiteral("/b.txt"));
    QDir(right.path()).mkdir(QStringLiteral("ordner"));

    gui::AsyncBridge bridge;
    net::SessionManager sessions;
    gui::TransferManager transfers(&bridge);
    gui::Workspace ws(&bridge, &sessions, &transfers);
    core::LocalFileSystem fs;
    ws.leftPanel()->setProvider(&fs, left.path());
    ws.rightPanel()->setProvider(&fs, right.path());
    int changes = 0;
    QObject::connect(&ws, &gui::Workspace::fileSelectionChanged, [&] { ++changes; });

    CHECK(selectByName(ws.leftPanel(), QStringLiteral("a.txt")));
    CHECK(!ws.fileSelectedInBothPanes());            // rechts noch nichts
    CHECK(selectByName(ws.rightPanel(), QStringLiteral("ordner")));
    CHECK(!ws.fileSelectedInBothPanes());            // Ordner zaehlt nicht
    CHECK(selectByName(ws.rightPanel(), QStringLiteral("b.txt")));
    CHECK(ws.fileSelectedInBothPanes());
    CHECK(changes >= 2);                         // Knopf wird nachgefuehrt
}
