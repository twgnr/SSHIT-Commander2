#include "ncssh/gui/filediff_dialog.hpp"

#include "ncssh/core/filediff.hpp"
#include "ncssh/core/i18n.hpp"

#include <QFont>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;

FileDiffDialog::FileDiffDialog(AsyncBridge *bridge,
                               core::FileSystemProvider *provA, const QString &pathA,
                               core::FileSystemProvider *provB, const QString &pathB,
                               QWidget *parent)
    : QDialog(parent)
{
    m_nameA = provA->basename(pathA);
    m_nameB = provB->basename(pathB);
    setWindowTitle(_t("Datei-Vergleich") + QStringLiteral(" — %1 ↔ %2").arg(m_nameA, m_nameB));
    resize(900, 640);

    auto *layout = new QVBoxLayout(this);
    m_view = new QPlainTextEdit(this);
    m_view->setReadOnly(true);
    m_view->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(10);
    m_view->setFont(mono);
    layout->addWidget(m_view, 1);

    m_status = new QLabel(_t("Lade …"), this);
    m_status->setObjectName(QStringLiteral("Muted"));
    layout->addWidget(m_status);

    auto *closeBtn = new QPushButton(_t("Schließen"), this);
    closeBtn->setDefault(true);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    layout->addWidget(closeBtn);

    // Lesen UND Vergleichen im Worker: der Diff grosser Dateien kostet
    // Sekunden — im GUI-Thread fror das Fenster ein, und ein bad_alloc im
    // Slot beendete die App. Fehler (auch bad_alloc) kommen hier als onError an.
    const QString nameA = m_nameA;
    const QString nameB = m_nameB;
    bridge->run<DiffResult>(
        [provA, pathA, provB, pathB, nameA, nameB] {
            const QString textA = provA->readText(pathA, 2'000'000);
            const QString textB = provB->readText(pathB, 2'000'000);
            DiffResult res;
            res.rows = core::unified(textA, textB, nameA, nameB, 3, &res.approximate);
            return res;
        },
        [this](const DiffResult &res) { render(res); },
        [this](const QString &err) { m_status->setText(err); }, this);
}

void FileDiffDialog::render(const DiffResult &res)
{
    const auto &rows = res.rows;
    if (rows.empty()) {
        m_status->setText(_t("Die Dateien sind identisch."));
        return;
    }
    // Anzeige deckeln: Hunderttausende farbige Zeilen einzeln einzufuegen
    // blockiert den GUI-Thread genauso wie frueher der Diff selbst.
    constexpr size_t kMaxShownRows = 100'000;
    QTextCursor cur = m_view->textCursor();
    cur.beginEditBlock();
    int added = 0, removed = 0;
    size_t shown = 0;
    for (const auto &[line, kind] : rows) {
        if (kind == QLatin1String("add"))
            ++added;
        else if (kind == QLatin1String("del"))
            ++removed;
        if (shown >= kMaxShownRows)
            continue;   // weiter zaehlen, aber nicht mehr anzeigen
        ++shown;
        QTextCharFormat fmt;
        if (kind == QLatin1String("hdr")) {
            fmt.setForeground(QColor(QStringLiteral("#8b90a0")));
            fmt.setFontWeight(QFont::Bold);
        } else if (kind == QLatin1String("hunk")) {
            fmt.setForeground(QColor(QStringLiteral("#4f8cff")));
        } else if (kind == QLatin1String("add")) {
            fmt.setForeground(QColor(QStringLiteral("#3fb950")));
        } else if (kind == QLatin1String("del")) {
            fmt.setForeground(QColor(QStringLiteral("#ef4444")));
        }
        cur.insertText(line + QLatin1Char('\n'), fmt);
    }
    if (shown < rows.size())
        cur.insertText(QStringLiteral("… (%1 / %2)\n").arg(qulonglong(shown)).arg(qulonglong(rows.size())));
    cur.endEditBlock();
    m_view->moveCursor(QTextCursor::Start);
    QString status = QStringLiteral("+%1 / -%2 Zeilen").arg(added).arg(removed);
    if (res.approximate)
        status += QStringLiteral(" — ") + _t("Dateien zu groß für Detailvergleich");
    m_status->setText(status);
}

} // namespace ncssh::gui
