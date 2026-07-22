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

    // Beide Dateien im Worker lesen, dann rendern.
    bridge->run<QPair<QString, QString>>(
        [provA, pathA, provB, pathB] {
            return QPair<QString, QString>(provA->readText(pathA, 2'000'000),
                                           provB->readText(pathB, 2'000'000));
        },
        [this](const QPair<QString, QString> &texts) { render(texts.first, texts.second); },
        [this](const QString &err) { m_status->setText(err); });
}

void FileDiffDialog::render(const QString &textA, const QString &textB)
{
    const auto rows = core::unified(textA, textB, m_nameA, m_nameB);
    if (rows.empty()) {
        m_status->setText(_t("Die Dateien sind identisch."));
        return;
    }
    QTextCursor cur = m_view->textCursor();
    int added = 0, removed = 0;
    for (const auto &[line, kind] : rows) {
        QTextCharFormat fmt;
        if (kind == QLatin1String("hdr")) {
            fmt.setForeground(QColor(QStringLiteral("#8b90a0")));
            fmt.setFontWeight(QFont::Bold);
        } else if (kind == QLatin1String("hunk")) {
            fmt.setForeground(QColor(QStringLiteral("#4f8cff")));
        } else if (kind == QLatin1String("add")) {
            fmt.setForeground(QColor(QStringLiteral("#3fb950")));
            ++added;
        } else if (kind == QLatin1String("del")) {
            fmt.setForeground(QColor(QStringLiteral("#ef4444")));
            ++removed;
        }
        cur.insertText(line + QLatin1Char('\n'), fmt);
    }
    m_view->moveCursor(QTextCursor::Start);
    m_status->setText(QStringLiteral("+%1 / -%2 Zeilen").arg(added).arg(removed));
}

} // namespace ncssh::gui
