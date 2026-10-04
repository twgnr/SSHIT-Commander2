// UTF-16-Dateien (Windows-INI ohne BOM) lesbar in Ansehen/Editor, und
// Umbenennen direkt in der Pane per langsamem Doppelklick.
#include "tests/harness.hpp"

#include "ncssh/core/encodings.hpp"
#include "ncssh/core/filesystem.hpp"
#include "ncssh/gui/bridge.hpp"
#include "ncssh/gui/editor_dialog.hpp"
#include "ncssh/gui/file_panel.hpp"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QMouseEvent>
#include <QShortcut>
#include <QStyleHints>
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

void settle(int ms)
{
    QDeadlineTimer deadline(ms);
    while (!deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
}

QByteArray utf16le(const QString &text)
{
    return QByteArray(reinterpret_cast<const char *>(text.utf16()), text.size() * 2);
}

const QString kIni = QStringLiteral("partnerScanTime = 300\r\ncloudFilesActiveLog = 1\r\n");

int rowOf(QTableWidget *table, const QString &name)
{
    for (int r = 0; r < table->rowCount(); ++r)
        if (table->item(r, 0)->data(Qt::UserRole).toString() == name)
            return r;
    return -1;
}

void mouse(QWidget *w, QEvent::Type type, const QPoint &pos)
{
    QMouseEvent ev(type, QPointF(pos), QPointF(w->mapToGlobal(pos)), Qt::LeftButton,
                   type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                   Qt::NoModifier);
    QApplication::sendEvent(w, &ev);
}

} // namespace

TEST(utf16_rename, detects_utf16_without_bom)
{
    CHECK_EQ(core::detectEncoding(utf16le(kIni)), QStringLiteral("utf-16-le"));
    QByteArray be;
    for (const QChar c : kIni) {
        be.append(char(c.unicode() >> 8));
        be.append(char(c.unicode() & 0xFF));
    }
    CHECK_EQ(core::detectEncoding(be), QStringLiteral("utf-16-be"));
    CHECK_EQ(core::decodeAuto(utf16le(kIni)), kIni);
    // Mit BOM wie bisher; normales UTF-8/ANSI unveraendert erkannt.
    CHECK_EQ(core::decodeAuto(QByteArray("\xFF\xFE") + utf16le(kIni)), kIni);
    CHECK_EQ(core::detectEncoding(QByteArray("plain = ascii\n")), QStringLiteral("utf-8"));
    CHECK_EQ(core::detectEncoding(QByteArray("f\xFCr\n")), QStringLiteral("cp1252"));
}

TEST(utf16_rename, view_and_editor_read_utf16_ini)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    const QString path = tmp.path() + QStringLiteral("/logUploaderSettings.ini");
    QFile f(path);
    CHECK(f.open(QIODevice::WriteOnly));
    f.write(utf16le(kIni));
    f.close();

    core::LocalFileSystem fs;
    // Ansehen (F3) und Vorschau lesen ueber readText.
    CHECK_EQ(fs.readText(path), kIni);

    gui::AsyncBridge bridge;
    gui::EditorDialog dlg(&bridge, &fs, path);
    auto *editor = dlg.findChild<gui::CodeEditor *>();
    CHECK(editor != nullptr);
    if (!editor)
        return;
    CHECK(pump([&] { return editor->toPlainText().contains(QStringLiteral("partnerScanTime = 300")); }));

    // Speichern bleibt UTF-16 LE ohne BOM (die Anwendung liest sie sonst evtl. nicht).
    editor->setPlainText(QStringLiteral("partnerScanTime = 600\n"));
    for (QObject *o : dlg.children())
        if (auto *sc = qobject_cast<QShortcut *>(o))
            if (sc->key() == QKeySequence(QStringLiteral("Ctrl+S")))
                emit sc->activated();
    CHECK(pump([&] {
        QFile in(path);
        if (!in.open(QIODevice::ReadOnly))
            return false;
        const QByteArray bytes = in.readAll();
        // Zeilenenden der Originaldatei (CRLF) bleiben erhalten.
        return !bytes.startsWith("\xFF\xFE")
               && core::detectEncoding(bytes) == QLatin1String("utf-16-le")
               && core::decodeAuto(bytes).trimmed() == QLatin1String("partnerScanTime = 600");
    }));
}

TEST(utf16_rename, inline_rename_selects_base_name_and_renames)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    QFile f(tmp.path() + QStringLiteral("/alt.txt"));
    CHECK(f.open(QIODevice::WriteOnly));
    f.close();

    gui::AsyncBridge bridge;
    core::LocalFileSystem fs;
    gui::FilePanel panel(&bridge, QStringLiteral("Test"));
    panel.resize(600, 400);
    panel.show();
    panel.setProvider(&fs, tmp.path());
    auto *table = panel.findChild<QTableWidget *>();
    CHECK(pump([&] { return rowOf(table, QStringLiteral("alt.txt")) >= 0; }));
    const int row = rowOf(table, QStringLiteral("alt.txt"));
    table->setCurrentCell(row, 0);
    table->selectRow(row);

    panel.beginInlineRename(row);
    auto *editor = table->viewport()->findChild<QLineEdit *>(QStringLiteral("InlineRenameEditor"));
    CHECK(editor != nullptr);
    if (!editor)
        return;
    CHECK_EQ(editor->selectedText(), QStringLiteral("alt"));   // Endung bleibt
    editor->setText(QStringLiteral("neu.txt"));
    emit editor->editingFinished();
    CHECK(pump([&] { return QFileInfo::exists(tmp.path() + QStringLiteral("/neu.txt")); }));
    CHECK(!QFileInfo::exists(tmp.path() + QStringLiteral("/alt.txt")));
}

TEST(utf16_rename, slow_double_click_starts_rename_fast_one_does_not)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    QFile f(tmp.path() + QStringLiteral("/datei.txt"));
    CHECK(f.open(QIODevice::WriteOnly));
    f.close();

    gui::AsyncBridge bridge;
    core::LocalFileSystem fs;
    gui::FilePanel panel(&bridge, QStringLiteral("Test"));
    panel.resize(600, 400);
    panel.show();
    panel.setProvider(&fs, tmp.path());
    auto *table = panel.findChild<QTableWidget *>();
    CHECK(pump([&] { return rowOf(table, QStringLiteral("datei.txt")) >= 0; }));
    const int row = rowOf(table, QStringLiteral("datei.txt"));
    table->setCurrentCell(row, 0);
    table->selectRow(row);
    QWidget *vp = table->viewport();
    const QPoint pos = table->visualRect(table->model()->index(row, 0)).center();
    const auto editor = [&] { return vp->findChild<QLineEdit *>(QStringLiteral("InlineRenameEditor")); };
    const int interval = QGuiApplication::styleHints()->mouseDoubleClickInterval();

    // Klick auf den markierten Eintrag, dann Pause -> Feld erscheint.
    mouse(vp, QEvent::MouseButtonPress, pos);
    mouse(vp, QEvent::MouseButtonRelease, pos);
    CHECK(pump([&] { return editor() != nullptr; }, interval + 2000));
    editor()->setText(QStringLiteral("verworfen.txt"));
    for (QAction *a : editor()->actions())
        a->trigger();   // Esc-Aktion: verwerfen
    CHECK(pump([&] { return editor() == nullptr || editor()->property("done").toBool(); }));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    CHECK(QFileInfo::exists(tmp.path() + QStringLiteral("/datei.txt")));   // verworfen

    // Schneller Doppelklick (oeffnen) -> kein Feld.
    table->setCurrentCell(row, 0);
    table->selectRow(row);
    mouse(vp, QEvent::MouseButtonPress, pos);
    mouse(vp, QEvent::MouseButtonRelease, pos);
    mouse(vp, QEvent::MouseButtonDblClick, pos);
    mouse(vp, QEvent::MouseButtonRelease, pos);
    settle(interval + 300);
    CHECK(editor() == nullptr);
}
