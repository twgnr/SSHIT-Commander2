#include "ncssh/gui/icons.hpp"

#include "ncssh/core/settings.hpp"
#include "ncssh/gui/style.hpp"

#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QPointF>
#include <QRectF>
#include <functional>

namespace ncssh::gui {

namespace {

QPen strokePen(const QColor &color, qreal width)
{
    return QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
}

// Senkrechter Pfeil vom Schaftende tailY zur Spitze tipY.
void vArrow(QPainter &p, qreal x, qreal tailY, qreal tipY, qreal head, qreal w,
            const QColor &color)
{
    p.setPen(strokePen(color, w));
    p.drawLine(QPointF(x, tailY), QPointF(x, tipY));
    const qreal d = (tipY > tailY) ? 1.0 : -1.0;   // zeigt nach unten?
    const qreal back = tipY - d * head;            // Spitzenschenkel laufen zurueck
    p.drawLine(QPointF(x, tipY), QPointF(x - head, back));
    p.drawLine(QPointF(x, tipY), QPointF(x + head, back));
}

using DrawFn = std::function<void(QPainter &, qreal, const QColor &)>;

// --- Einzelne Glyphen -------------------------------------------------------

void drawTransfers(QPainter &p, qreal s, const QColor &c)
{
    // Zwei gegenlaeufige Pfeile (hoch/runter).
    const qreal w = s * 0.09;
    vArrow(p, s * 0.34, s * 0.80, s * 0.20, s * 0.13, w, c);
    vArrow(p, s * 0.66, s * 0.20, s * 0.80, s * 0.13, w, c);
}

void drawCopy(QPainter &p, qreal s, const QColor &c)
{
    // Zwei ueberlappende Seiten.
    p.setPen(strokePen(c, s * 0.08));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(s * 0.16, s * 0.16, s * 0.48, s * 0.58), s * 0.06, s * 0.06);
    p.drawRoundedRect(QRectF(s * 0.36, s * 0.30, s * 0.48, s * 0.58), s * 0.06, s * 0.06);
}

void drawEdit(QPainter &p, qreal s, const QColor &c)
{
    // Stift mit Spitze unten links.
    p.setPen(strokePen(c, s * 0.09));
    p.drawLine(QPointF(s * 0.28, s * 0.74), QPointF(s * 0.70, s * 0.30));
    p.drawLine(QPointF(s * 0.70, s * 0.30), QPointF(s * 0.82, s * 0.42));
    p.drawLine(QPointF(s * 0.82, s * 0.42), QPointF(s * 0.40, s * 0.86));
    p.drawLine(QPointF(s * 0.40, s * 0.86), QPointF(s * 0.22, s * 0.90));
    p.drawLine(QPointF(s * 0.22, s * 0.90), QPointF(s * 0.28, s * 0.74));
}

void drawConnect(QPainter &p, qreal s, const QColor &c)
{
    // Zwei verbundene Stecker.
    p.setPen(strokePen(c, s * 0.09));
    p.drawLine(QPointF(s * 0.20, s * 0.50), QPointF(s * 0.42, s * 0.50));
    p.drawLine(QPointF(s * 0.58, s * 0.50), QPointF(s * 0.80, s * 0.50));
    p.drawRoundedRect(QRectF(s * 0.30, s * 0.34, s * 0.16, s * 0.32), s * 0.04, s * 0.04);
    p.drawRoundedRect(QRectF(s * 0.54, s * 0.34, s * 0.16, s * 0.32), s * 0.04, s * 0.04);
}

void drawPalette(QPainter &p, qreal s, const QColor &c)
{
    // Liste mit Zeilen (Befehlskatalog).
    p.setPen(strokePen(c, s * 0.08));
    p.drawRoundedRect(QRectF(s * 0.18, s * 0.20, s * 0.64, s * 0.60), s * 0.06, s * 0.06);
    for (int i = 0; i < 3; ++i) {
        const qreal y = s * (0.34 + i * 0.16);
        p.drawLine(QPointF(s * 0.28, y), QPointF(s * 0.72, y));
    }
}

void drawHistory(QPainter &p, qreal s, const QColor &c)
{
    // Uhr mit Zeigern.
    p.setPen(strokePen(c, s * 0.08));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QRectF(s * 0.18, s * 0.18, s * 0.64, s * 0.64));
    p.drawLine(QPointF(s * 0.50, s * 0.50), QPointF(s * 0.50, s * 0.30));
    p.drawLine(QPointF(s * 0.50, s * 0.50), QPointF(s * 0.66, s * 0.58));
}

void drawTunnels(QPainter &p, qreal s, const QColor &c)
{
    // Roehre mit durchlaufendem Pfeil.
    p.setPen(strokePen(c, s * 0.08));
    p.drawEllipse(QRectF(s * 0.14, s * 0.28, s * 0.20, s * 0.44));
    p.drawLine(QPointF(s * 0.24, s * 0.28), QPointF(s * 0.76, s * 0.28));
    p.drawLine(QPointF(s * 0.24, s * 0.72), QPointF(s * 0.76, s * 0.72));
    p.drawLine(QPointF(s * 0.40, s * 0.50), QPointF(s * 0.86, s * 0.50));
    p.drawLine(QPointF(s * 0.86, s * 0.50), QPointF(s * 0.74, s * 0.40));
    p.drawLine(QPointF(s * 0.86, s * 0.50), QPointF(s * 0.74, s * 0.60));
}

void drawView(QPainter &p, qreal s, const QColor &c)
{
    // Auge.
    p.setPen(strokePen(c, s * 0.08));
    p.setBrush(Qt::NoBrush);
    QPainterPath path;
    path.moveTo(s * 0.14, s * 0.50);
    path.quadTo(s * 0.50, s * 0.16, s * 0.86, s * 0.50);
    path.quadTo(s * 0.50, s * 0.84, s * 0.14, s * 0.50);
    p.drawPath(path);
    p.drawEllipse(QRectF(s * 0.41, s * 0.41, s * 0.18, s * 0.18));
}

void drawRename(QPainter &p, qreal s, const QColor &c)
{
    // Textcursor zwischen Klammern.
    p.setPen(strokePen(c, s * 0.08));
    p.drawLine(QPointF(s * 0.30, s * 0.24), QPointF(s * 0.22, s * 0.24));
    p.drawLine(QPointF(s * 0.22, s * 0.24), QPointF(s * 0.22, s * 0.76));
    p.drawLine(QPointF(s * 0.22, s * 0.76), QPointF(s * 0.30, s * 0.76));
    p.drawLine(QPointF(s * 0.70, s * 0.24), QPointF(s * 0.78, s * 0.24));
    p.drawLine(QPointF(s * 0.78, s * 0.24), QPointF(s * 0.78, s * 0.76));
    p.drawLine(QPointF(s * 0.78, s * 0.76), QPointF(s * 0.70, s * 0.76));
    p.drawLine(QPointF(s * 0.50, s * 0.28), QPointF(s * 0.50, s * 0.72));
}

void drawMkdir(QPainter &p, qreal s, const QColor &c)
{
    // Ordner mit Plus.
    p.setPen(strokePen(c, s * 0.08));
    QPainterPath folder;
    folder.moveTo(s * 0.16, s * 0.74);
    folder.lineTo(s * 0.16, s * 0.30);
    folder.lineTo(s * 0.42, s * 0.30);
    folder.lineTo(s * 0.50, s * 0.40);
    folder.lineTo(s * 0.84, s * 0.40);
    folder.lineTo(s * 0.84, s * 0.74);
    folder.closeSubpath();
    p.drawPath(folder);
    p.drawLine(QPointF(s * 0.50, s * 0.50), QPointF(s * 0.50, s * 0.66));
    p.drawLine(QPointF(s * 0.42, s * 0.58), QPointF(s * 0.58, s * 0.58));
}

void drawDelete(QPainter &p, qreal s, const QColor &c)
{
    // Papierkorb.
    p.setPen(strokePen(c, s * 0.08));
    p.drawLine(QPointF(s * 0.20, s * 0.30), QPointF(s * 0.80, s * 0.30));
    p.drawLine(QPointF(s * 0.40, s * 0.30), QPointF(s * 0.42, s * 0.20));
    p.drawLine(QPointF(s * 0.42, s * 0.20), QPointF(s * 0.58, s * 0.20));
    p.drawLine(QPointF(s * 0.58, s * 0.20), QPointF(s * 0.60, s * 0.30));
    p.drawLine(QPointF(s * 0.26, s * 0.30), QPointF(s * 0.32, s * 0.84));
    p.drawLine(QPointF(s * 0.74, s * 0.30), QPointF(s * 0.68, s * 0.84));
    p.drawLine(QPointF(s * 0.32, s * 0.84), QPointF(s * 0.68, s * 0.84));
}

void drawReload(QPainter &p, qreal s, const QColor &c)
{
    // Kreisbogen mit Pfeilspitze.
    p.setPen(strokePen(c, s * 0.09));
    p.setBrush(Qt::NoBrush);
    const QRectF box(s * 0.20, s * 0.20, s * 0.60, s * 0.60);
    p.drawArc(box, 60 * 16, 260 * 16);
    p.drawLine(QPointF(s * 0.70, s * 0.24), QPointF(s * 0.72, s * 0.40));
    p.drawLine(QPointF(s * 0.72, s * 0.40), QPointF(s * 0.56, s * 0.36));
}

void drawSearch(QPainter &p, qreal s, const QColor &c)
{
    // Lupe.
    p.setPen(strokePen(c, s * 0.09));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QRectF(s * 0.20, s * 0.20, s * 0.42, s * 0.42));
    p.drawLine(QPointF(s * 0.58, s * 0.58), QPointF(s * 0.82, s * 0.82));
}

void drawSettings(QPainter &p, qreal s, const QColor &c)
{
    // Zahnrad (vereinfacht: Kreis + Zaehne).
    p.setPen(strokePen(c, s * 0.08));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QRectF(s * 0.34, s * 0.34, s * 0.32, s * 0.32));
    for (int i = 0; i < 8; ++i) {
        const qreal angle = i * M_PI / 4.0;
        const qreal cx = s * 0.5, cy = s * 0.5;
        p.drawLine(QPointF(cx + std::cos(angle) * s * 0.20,
                           cy + std::sin(angle) * s * 0.20),
                   QPointF(cx + std::cos(angle) * s * 0.34,
                           cy + std::sin(angle) * s * 0.34));
    }
}

void drawHelp(QPainter &p, qreal s, const QColor &c)
{
    // Kreis mit Fragezeichen.
    p.setPen(strokePen(c, s * 0.08));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QRectF(s * 0.18, s * 0.18, s * 0.64, s * 0.64));
    QFont f = p.font();
    f.setPixelSize(int(s * 0.46));
    f.setBold(true);
    p.setFont(f);
    p.drawText(QRectF(0, 0, s, s), Qt::AlignCenter, QStringLiteral("?"));
}

void drawClipboard(QPainter &p, qreal s, const QColor &c)
{
    // Klemmbrett.
    p.setPen(strokePen(c, s * 0.08));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(s * 0.24, s * 0.22, s * 0.52, s * 0.62), s * 0.06, s * 0.06);
    p.drawRoundedRect(QRectF(s * 0.38, s * 0.14, s * 0.24, s * 0.14), s * 0.04, s * 0.04);
    for (int i = 0; i < 2; ++i) {
        const qreal y = s * (0.46 + i * 0.16);
        p.drawLine(QPointF(s * 0.34, y), QPointF(s * 0.66, y));
    }
}

void drawMacro(QPainter &p, qreal s, const QColor &c)
{
    // 2x2-Tastenraster.
    p.setPen(strokePen(c, s * 0.07));
    p.setBrush(Qt::NoBrush);
    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < 2; ++col) {
            p.drawRoundedRect(QRectF(s * (0.18 + col * 0.34), s * (0.18 + row * 0.34),
                                     s * 0.28, s * 0.28),
                              s * 0.05, s * 0.05);
        }
    }
}

void drawTab(QPainter &p, qreal s, const QColor &c)
{
    // Plus im Rechteck (neuer Tab).
    p.setPen(strokePen(c, s * 0.08));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(s * 0.18, s * 0.24, s * 0.64, s * 0.54), s * 0.06, s * 0.06);
    p.drawLine(QPointF(s * 0.50, s * 0.36), QPointF(s * 0.50, s * 0.66));
    p.drawLine(QPointF(s * 0.35, s * 0.51), QPointF(s * 0.65, s * 0.51));
}

const QHash<QString, DrawFn> &registry()
{
    static const QHash<QString, DrawFn> map = {
        {QStringLiteral("transfers"), drawTransfers},
        {QStringLiteral("copy"), drawCopy},
        {QStringLiteral("edit"), drawEdit},
        {QStringLiteral("connect"), drawConnect},
        {QStringLiteral("palette"), drawPalette},
        {QStringLiteral("history"), drawHistory},
        {QStringLiteral("tunnels"), drawTunnels},
        {QStringLiteral("view"), drawView},
        {QStringLiteral("rename"), drawRename},
        {QStringLiteral("mkdir"), drawMkdir},
        {QStringLiteral("delete"), drawDelete},
        {QStringLiteral("reload"), drawReload},
        {QStringLiteral("search"), drawSearch},
        {QStringLiteral("settings"), drawSettings},
        {QStringLiteral("help"), drawHelp},
        {QStringLiteral("clipboard"), drawClipboard},
        {QStringLiteral("macro"), drawMacro},
        {QStringLiteral("tab"), drawTab},
    };
    return map;
}

// Zeichenflaeche mit HiDPI-Faktor fuer scharfe Kanten.
QPixmap render(const DrawFn &draw, int size, const QColor &color)
{
    constexpr int ratio = 2;
    QPixmap px(size * ratio, size * ratio);
    px.setDevicePixelRatio(ratio);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing, true);
    draw(p, qreal(size), color);
    p.end();
    return px;
}

} // namespace

QIcon icon(const QString &kind, const QColor &color, int size)
{
    const auto it = registry().find(kind);
    if (it == registry().end())
        return {};
    QIcon result(render(it.value(), size, color));
    // Gedaempftes Pixmap fuer Disabled — Qts Automatik ist auf dunklen Themes
    // kaum sichtbar.
    result.addPixmap(render(it.value(), size, QColor(128, 128, 128, 90)), QIcon::Disabled);
    return result;
}

QIcon themedIcon(const QString &kind, int size)
{
    const ThemeColors colors = themeColors(
        core::getSettingString(QStringLiteral("theme"), defaultTheme()));
    return icon(kind, QColor(colors.value(QStringLiteral("text"))), size);
}

bool hasIcon(const QString &kind)
{
    return registry().contains(kind);
}

} // namespace ncssh::gui
