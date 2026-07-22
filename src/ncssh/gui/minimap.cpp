#include "ncssh/gui/minimap.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTextBlock>

namespace ncssh::gui {

Minimap::Minimap(QPlainTextEdit *editor, QWidget *parent)
    : QWidget(parent), m_editor(editor)
{
    setFixedWidth(kWidth);
    setCursor(Qt::PointingHandCursor);
    // Bei Aenderungen und beim Scrollen neu zeichnen.
    connect(editor->document(), &QTextDocument::contentsChanged, this,
            QOverload<>::of(&QWidget::update));
    connect(editor->verticalScrollBar(), &QScrollBar::valueChanged, this,
            QOverload<>::of(&QWidget::update));
}

void Minimap::setMatches(const std::vector<int> &lines)
{
    m_matches = lines;
    update();
}

void Minimap::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(QStringLiteral("#12141a")));

    const int total = qMax(1, m_editor->blockCount());
    // Massstab so waehlen, dass das ganze Dokument in die Hoehe passt.
    const double scale = qMin<double>(kLineHeight, double(height()) / total);

    // Zeilen als kurze Balken andeuten (Laenge = Zeilenlaenge).
    p.setPen(Qt::NoPen);
    QTextBlock block = m_editor->document()->firstBlock();
    int line = 0;
    while (block.isValid()) {
        const QString text = block.text();
        const int trimmedLength = text.trimmed().length();
        if (trimmedLength > 0) {
            const int indent = text.length() - QString(text).remove(QRegularExpression(
                                                   QStringLiteral("^\\s+"))).length();
            const int x = qMin(kWidth - 4, 2 + indent / 2);
            const int w = qMin(kWidth - x - 2, qMax(1, trimmedLength / 2));
            const int y = int(line * scale);
            p.fillRect(x, y, w, qMax(1, int(scale) - 1),
                       QColor(QStringLiteral("#8b90a0")));
        }
        block = block.next();
        ++line;
    }

    // Suchtreffer farbig hervorheben.
    for (int matchLine : m_matches) {
        const int y = int(matchLine * scale);
        p.fillRect(0, y, kWidth, qMax(2, int(scale)),
                   QColor(255, 212, 0, 170));
    }

    // Sichtbaren Bereich als Rahmen zeigen. firstVisibleBlock() ist protected —
    // die Scrollbar von QPlainTextEdit zaehlt ohnehin in Bloecken.
    const int firstVisible = m_editor->verticalScrollBar()->value();
    const int visibleLines = qMax(1, m_editor->viewport()->height()
                                         / qMax(1, m_editor->fontMetrics().height()));
    const int viewY = int(firstVisible * scale);
    const int viewH = qMax(4, int(visibleLines * scale));
    p.fillRect(0, viewY, kWidth, viewH, QColor(79, 140, 255, 40));
    p.setPen(QColor(QStringLiteral("#4f8cff")));
    p.setBrush(Qt::NoBrush);
    p.drawRect(0, viewY, kWidth - 1, viewH);
}

void Minimap::jumpTo(int y)
{
    const int total = qMax(1, m_editor->blockCount());
    const double scale = qMin<double>(kLineHeight, double(height()) / total);
    const int line = qBound(0, int(y / qMax(0.01, scale)), total - 1);
    QTextCursor cursor(m_editor->document()->findBlockByNumber(line));
    m_editor->setTextCursor(cursor);
    m_editor->centerCursor();
}

void Minimap::mousePressEvent(QMouseEvent *event)
{
    jumpTo(event->position().toPoint().y());
}

void Minimap::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton)
        jumpTo(event->position().toPoint().y());
}

} // namespace ncssh::gui
