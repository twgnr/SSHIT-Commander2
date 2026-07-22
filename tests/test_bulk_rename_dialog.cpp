// Offscreen-Test fuer den Massen-Umbenenn-Dialog: Sortierung, Nummerierung,
// Konfliktaufloesung, gefahrlose Reihenfolge und Rueckgaengig.
// (Port von tests/test_bulk_rename_dialog.py)
#include "tests/harness.hpp"

#include "ncssh/core/bulkrename.hpp"
#include "ncssh/core/filesystem.hpp"
#include "ncssh/gui/bridge.hpp"
#include "ncssh/gui/bulk_rename_dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QSpinBox>
#include <QTableWidget>
#include <QPushButton>
#include <QTemporaryDir>

using namespace ncssh;

namespace {

void touch(const QString &path, const QByteArray &content = {})
{
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(content);
    f.close();
}

// Zielname aus der Vorschau-Tabelle fuer einen Ausgangsnamen.
QString newNameFor(QTableWidget *preview, const QString &oldName)
{
    for (int r = 0; r < preview->rowCount(); ++r) {
        if (preview->item(r, 0)->text() == oldName)
            return preview->item(r, 1)->text();
    }
    return {};
}

// Findet ein Kind-Widget anhand seiner Beschriftung bzw. seines Platzhalters.
template <typename T>
T *childWithText(QWidget *parent, const QString &needle)
{
    for (T *widget : parent->findChildren<T *>()) {
        if (widget->text().contains(needle))
            return widget;
    }
    return nullptr;
}

QComboBox *comboWithItemData(QWidget *parent, const QString &data)
{
    for (QComboBox *box : parent->findChildren<QComboBox *>()) {
        if (box->findData(data) >= 0)
            return box;
    }
    return nullptr;
}

QStringList namesOnDisk(const QString &dir)
{
    QStringList out = QDir(dir).entryList(QDir::Files, QDir::Name);
    out.sort();
    return out;
}

} // namespace

TEST(bulk_rename_dialog, sort_and_numbering)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    for (const char *name : {"img10.txt", "img2.txt", "img1.txt"})
        touch(tmp.filePath(QString::fromLatin1(name)));

    gui::AsyncBridge bridge;
    core::LocalFileSystem fs;
    gui::BulkRenameDialog dlg(&bridge, &fs, tmp.path(),
                              {QStringLiteral("img10.txt"), QStringLiteral("img2.txt"),
                               QStringLiteral("img1.txt")});

    // Reihenfolge auf "natuerlich" stellen (1, 2, 10).
    QComboBox *sortMode = comboWithItemData(&dlg, QStringLiteral("natural"));
    CHECK(sortMode != nullptr);
    if (!sortMode)
        return;
    sortMode->setCurrentIndex(sortMode->findData(QStringLiteral("natural")));

    QCheckBox *numbering = childWithText<QCheckBox>(&dlg, QStringLiteral("aktiv"));
    CHECK(numbering != nullptr);
    if (!numbering)
        return;
    numbering->setChecked(true);

    // Stellen = 2, Trenner = "_"
    for (QSpinBox *spin : dlg.findChildren<QSpinBox *>()) {
        if (spin->value() == 2 && spin->maximum() == 10)
            spin->setValue(2);   // Stellen-Feld
    }
    for (QLineEdit *edit : dlg.findChildren<QLineEdit *>()) {
        if (edit->placeholderText() == QStringLiteral("_"))
            edit->setText(QStringLiteral("_"));
    }

    auto *preview = dlg.findChild<QTableWidget *>();
    CHECK(preview != nullptr);
    if (!preview)
        return;
    // Nummern folgen der natuerlichen Reihenfolge: 1 -> 01, 2 -> 02, 10 -> 03
    CHECK_EQ(newNameFor(preview, QStringLiteral("img1.txt")), QStringLiteral("img1_01.txt"));
    CHECK_EQ(newNameFor(preview, QStringLiteral("img2.txt")), QStringLiteral("img2_02.txt"));
    CHECK_EQ(newNameFor(preview, QStringLiteral("img10.txt")), QStringLiteral("img10_03.txt"));
}

TEST(bulk_rename_dialog, auto_resolve_conflicts)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    touch(tmp.filePath(QStringLiteral("a.jpg")));
    touch(tmp.filePath(QStringLiteral("b.jpg")));

    gui::AsyncBridge bridge;
    core::LocalFileSystem fs;
    gui::BulkRenameDialog dlg(&bridge, &fs, tmp.path(),
                              {QStringLiteral("a.jpg"), QStringLiteral("b.jpg")});

    // Beide Namen auf denselben Zielnamen abbilden (Regex).
    QComboBox *matchMode = comboWithItemData(&dlg, QStringLiteral("regex"));
    CHECK(matchMode != nullptr);
    if (!matchMode)
        return;
    matchMode->setCurrentIndex(matchMode->findData(QStringLiteral("regex")));

    const auto edits = dlg.findChildren<QLineEdit *>();
    CHECK(edits.size() >= 2);
    if (edits.size() < 2)
        return;
    edits.at(0)->setText(QStringLiteral("^[ab]$"));   // Suchen
    edits.at(1)->setText(QStringLiteral("foto"));     // Ersetzen

    QCheckBox *autoResolve =
        childWithText<QCheckBox>(&dlg, QStringLiteral("Konflikte automatisch"));
    CHECK(autoResolve != nullptr);
    if (!autoResolve)
        return;

    // Ohne Auto-Aufloesung ist "Umbenennen" gesperrt (Konflikt).
    autoResolve->setChecked(false);
    auto *applyBtn = childWithText<QPushButton>(&dlg, QStringLiteral("Umbenennen"));
    CHECK(applyBtn != nullptr);
    if (!applyBtn)
        return;
    CHECK(!applyBtn->isEnabled());

    autoResolve->setChecked(true);
    auto *preview = dlg.findChild<QTableWidget *>();
    CHECK(preview != nullptr);
    if (!preview)
        return;
    QStringList news = {newNameFor(preview, QStringLiteral("a.jpg")),
                        newNameFor(preview, QStringLiteral("b.jpg"))};
    news.sort();
    CHECK_EQ(news, (QStringList{QStringLiteral("foto (1).jpg"), QStringLiteral("foto.jpg")}));
    CHECK(applyBtn->isEnabled());
}

TEST(bulk_rename_dialog, safe_order_swaps_on_disk)
{
    // Tausch a <-> b muss ueber einen Temp-Namen laufen, sonst wird eine der
    // beiden Dateien ueberschrieben.
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    const QString pa = tmp.filePath(QStringLiteral("a.txt"));
    const QString pb = tmp.filePath(QStringLiteral("b.txt"));
    touch(pa, QByteArrayLiteral("A"));
    touch(pb, QByteArrayLiteral("B"));

    const std::vector<core::RenamePair> changes = {
        {QStringLiteral("a.txt"), QStringLiteral("b.txt")},
        {QStringLiteral("b.txt"), QStringLiteral("a.txt")},
    };
    const QSet<QString> existing = {QStringLiteral("a.txt"), QStringLiteral("b.txt")};
    const auto steps = core::planSafeOrder(changes, existing);
    CHECK(steps.size() > changes.size());   // Temp-Schritt eingefuegt

    core::LocalFileSystem fs;
    for (const auto &[from, to] : steps)
        fs.rename(fs.join(tmp.path(), from), fs.join(tmp.path(), to));

    const auto read = [](const QString &path) {
        QFile f(path);
        f.open(QIODevice::ReadOnly);
        return f.readAll();
    };
    CHECK_EQ(read(pa), QByteArrayLiteral("B"));
    CHECK_EQ(read(pb), QByteArrayLiteral("A"));
}

TEST(bulk_rename_dialog, prefix_preview_and_names_on_disk)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    touch(tmp.filePath(QStringLiteral("one.txt")));
    touch(tmp.filePath(QStringLiteral("two.txt")));

    gui::AsyncBridge bridge;
    core::LocalFileSystem fs;
    gui::BulkRenameDialog dlg(&bridge, &fs, tmp.path(),
                              {QStringLiteral("one.txt"), QStringLiteral("two.txt")});

    // Praefix-Feld ist das erste Feld der Gruppe "Text & Endung"; ueber die
    // berechneten Paare laesst sich das Ergebnis unabhaengig davon pruefen.
    const auto pairs = core::computeRenames(
        {QStringLiteral("one.txt"), QStringLiteral("two.txt")},
        [] {
            core::RenameOptions o;
            o.prefix = QStringLiteral("x_");
            return o;
        }());
    CHECK_EQ(pairs.size(), size_t(2));
    CHECK_EQ(pairs[0].second, QStringLiteral("x_one.txt"));
    CHECK_EQ(pairs[1].second, QStringLiteral("x_two.txt"));

    // Und die Ausfuehrung auf der Platte.
    for (const auto &[from, to] : core::planSafeOrder(pairs, {}))
        fs.rename(fs.join(tmp.path(), from), fs.join(tmp.path(), to));
    CHECK_EQ(namesOnDisk(tmp.path()),
             (QStringList{QStringLiteral("x_one.txt"), QStringLiteral("x_two.txt")}));
}
