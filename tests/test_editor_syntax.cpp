// Editor: Zeichensatz-Erkennung (Umlaute in ANSI-Dateien), Speichern im
// Original-Zeichensatz, Syntax-Erkennung und der String/Kommentar-Scanner.
// Dazu F8 ohne Rueckfrage.
#include "tests/harness.hpp"

#include "ncssh/core/filesystem.hpp"
#include "ncssh/core/settings.hpp"
#include "ncssh/gui/bridge.hpp"
#include "ncssh/gui/editor_dialog.hpp"
#include "ncssh/gui/file_panel.hpp"
#include "ncssh/gui/highlighter.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QShortcut>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QThread>
#include <QTimer>
#include <cstdio>

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

// Vordergrundfarbe an Position pos der ersten Zeile.
QColor colorAt(QTextDocument &doc, int pos)
{
    const QTextBlock block = doc.firstBlock();
    for (const QTextLayout::FormatRange &r : block.layout()->formats())
        if (pos >= r.start && pos < r.start + r.length)
            return r.format.foreground().color();
    return {};
}

} // namespace

TEST(editor_syntax, detects_language_from_name_and_content)
{
    using gui::SyntaxHighlighter;
    CHECK_EQ(SyntaxHighlighter::detectLanguage(QStringLiteral("main.cpp"), {}),
             QStringLiteral("cpp"));
    CHECK_EQ(SyntaxHighlighter::detectLanguage(QStringLiteral("Dockerfile"), {}),
             QStringLiteral("dockerfile"));
    CHECK_EQ(SyntaxHighlighter::detectLanguage(QStringLiteral("deploy"),
                                               QStringLiteral("#!/usr/bin/env bash\necho hi\n")),
             QStringLiteral("shell"));
    CHECK_EQ(SyntaxHighlighter::detectLanguage(QStringLiteral("tool"),
                                               QStringLiteral("#!/usr/bin/python3\nprint(1)\n")),
             QStringLiteral("python"));
    CHECK_EQ(SyntaxHighlighter::detectLanguage(QStringLiteral("data"),
                                               QStringLiteral("{\"a\": [1, 2]}")),
             QStringLiteral("json"));
    CHECK_EQ(SyntaxHighlighter::detectLanguage(QStringLiteral("page"),
                                               QStringLiteral("<?xml version=\"1.0\"?><a/>")),
             QStringLiteral("xml"));
    CHECK_EQ(SyntaxHighlighter::detectLanguage(QStringLiteral("README"), QStringLiteral("hallo")),
             QString());
}

TEST(editor_syntax, slashes_in_string_are_not_a_comment)
{
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("const u = \"http://x\"; // echt"));
    gui::SyntaxHighlighter hl(&doc, QStringLiteral("javascript"));
    hl.rehighlight();
    const QColor inString = colorAt(doc, 15);   // "http:/[/]x"
    const QColor inComment = colorAt(doc, 26);  // "// e[c]ht"
    CHECK(inString.isValid() && inComment.isValid());
    CHECK(inString != inComment);
}

TEST(editor_syntax, ansi_umlauts_shown_and_saved_in_original_encoding)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    const QString path = tmp.path() + QStringLiteral("/notiz.txt");
    QFile f(path);
    CHECK(f.open(QIODevice::WriteOnly));
    f.write("f\xFCr Gr\xF6\xDF" "e\n");   // Windows-1252: "für Größe"
    f.close();

    gui::AsyncBridge bridge;
    core::LocalFileSystem fs;
    gui::EditorDialog dlg(&bridge, &fs, path);
    auto *editor = dlg.findChild<gui::CodeEditor *>();
    CHECK(editor != nullptr);
    if (!editor)
        return;
    CHECK(pump([&] { return editor->toPlainText().contains(QStringLiteral("für Größe")); }));

    // Aendern + speichern: bleibt Windows-1252 (kein UTF-8-Umbau).
    editor->setPlainText(QStringLiteral("für Größe!\n"));
    QMetaObject::invokeMethod(&dlg, [&dlg] {
        // Strg+S ueber den registrierten Shortcut ausloesen.
        for (QObject *o : dlg.children())
            if (auto *sc = qobject_cast<QShortcut *>(o))
                if (sc->key() == QKeySequence(QStringLiteral("Ctrl+S")))
                    emit sc->activated();
    });
    CHECK(pump([&] {
        QFile in(path);
        return in.open(QIODevice::ReadOnly) && in.readAll() == QByteArray("f\xFCr Gr\xF6\xDF" "e!\n");
    }));
}

TEST(editor_syntax, delete_without_confirmation_when_disabled)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    const QString victim = tmp.path() + QStringLiteral("/weg.txt");
    QFile f(victim);
    CHECK(f.open(QIODevice::WriteOnly));
    f.close();

    core::setSetting(QStringLiteral("confirm_delete"), false);
    // Sicherheitsnetz: erschiene doch ein modaler Dialog, haengt der Test nicht.
    QTimer guard;
    QObject::connect(&guard, &QTimer::timeout, [] {
        if (QWidget *w = QApplication::activeModalWidget())
            w->close();
    });
    guard.start(200);

    gui::AsyncBridge bridge;
    core::LocalFileSystem fs;
    gui::FilePanel panel(&bridge, QStringLiteral("Test"));
    panel.setProvider(&fs, tmp.path());
    CHECK(pump([&] { return panel.currentPath() == QDir::toNativeSeparators(tmp.path()); }));
    auto *table = panel.findChild<QTableWidget *>();
    for (int r = 0; table && r < table->rowCount(); ++r)
        if (table->item(r, 0)->data(Qt::UserRole).toString() == QStringLiteral("weg.txt"))
            table->selectRow(r);
    panel.triggerOp(QStringLiteral("delete"));
    CHECK(pump([&] { return !QFile::exists(victim); }));
    core::setSetting(QStringLiteral("confirm_delete"), true);
}
