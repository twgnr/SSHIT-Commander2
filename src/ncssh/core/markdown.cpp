#include "ncssh/core/markdown.hpp"

#include <QRegularExpression>
#include <QStringList>

namespace ncssh::core {

namespace {

const char *const CODE_BG = "#1b1e26";
const char *const CODE_FG = "#d3d7cf";
const char *const INLINE_BG = "#2a2e38";
const char *const BORDER = "#2e3440";

QString preStyle()
{
    return QStringLiteral("background:%1;color:%2;padding:8px 10px;"
                          "border:1px solid %3;border-radius:6px;white-space:pre-wrap;")
        .arg(QLatin1String(CODE_BG), QLatin1String(CODE_FG), QLatin1String(BORDER));
}

QString codeStyle()
{
    return QStringLiteral("background:%1;border-radius:3px;padding:0 3px;")
        .arg(QLatin1String(INLINE_BG));
}

const QRegularExpression &inlineCodeRe()
{
    static const QRegularExpression re(QStringLiteral("`([^`]+)`"));
    return re;
}
const QRegularExpression &boldRe()
{
    static const QRegularExpression re(QStringLiteral("\\*\\*([^*]+)\\*\\*"));
    return re;
}
const QRegularExpression &italicRe()
{
    static const QRegularExpression re(QStringLiteral("(?<!\\*)\\*([^*]+)\\*(?!\\*)"));
    return re;
}
const QRegularExpression &bulletRe()
{
    static const QRegularExpression re(QStringLiteral("^\\s*[-*]\\s+(.*)$"));
    return re;
}
const QRegularExpression &orderedRe()
{
    static const QRegularExpression re(QStringLiteral("^\\s*\\d+\\.\\s+(.*)$"));
    return re;
}
const QRegularExpression &headingRe()
{
    static const QRegularExpression re(QStringLiteral("^(#{1,6})\\s+(.*)$"));
    return re;
}

// HTML-Escaping wie Pythons html.escape (inkl. Quotes).
QString htmlEscape(const QString &text)
{
    QString out = text;
    out.replace(QLatin1Char('&'), QLatin1String("&amp;"));
    out.replace(QLatin1Char('<'), QLatin1String("&lt;"));
    out.replace(QLatin1Char('>'), QLatin1String("&gt;"));
    out.replace(QLatin1Char('"'), QLatin1String("&quot;"));
    out.replace(QLatin1Char('\''), QLatin1String("&#x27;"));
    return out;
}

QString formatPlain(const QString &text)
{
    QString out = htmlEscape(text);
    out.replace(boldRe(), QStringLiteral("<b>\\1</b>"));
    out.replace(italicRe(), QStringLiteral("<i>\\1</i>"));
    return out;
}

// Inline-Formatierung auf einem bereits geblockten Textstueck.
// Code-Spans werden zuerst geschuetzt (kein Markdown darin), der Rest escaped
// und mit fett/kursiv versehen.
QString inlineFmt(const QString &text)
{
    QString out;
    int pos = 0;
    QRegularExpressionMatchIterator it = inlineCodeRe().globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += formatPlain(text.mid(pos, m.capturedStart() - pos));
        out += QStringLiteral("<code style='%1'>%2</code>")
                   .arg(codeStyle(), htmlEscape(m.captured(1)));
        pos = m.capturedEnd();
    }
    out += formatPlain(text.mid(pos));
    return out;
}

} // namespace

QString mdToHtml(const QString &text)
{
    const QStringList lines = text.split(QLatin1Char('\n'));
    QStringList htmlParts;
    int i = 0;
    const int n = lines.size();
    QStringList para;
    QString listKind;  // "ul" | "ol" | leer

    auto flushPara = [&] {
        if (!para.isEmpty()) {
            htmlParts.append(QStringLiteral("<p style='margin:4px 0;'>")
                             + para.join(QStringLiteral("<br>")) + QStringLiteral("</p>"));
            para.clear();
        }
    };
    auto closeList = [&] {
        if (!listKind.isEmpty()) {
            htmlParts.append(QStringLiteral("</%1>").arg(listKind));
            listKind.clear();
        }
    };

    while (i < n) {
        const QString line = lines.at(i);
        const QString stripped = line.trimmed();

        // Eingezaeunter Code-Block
        if (stripped.startsWith(QLatin1String("```"))) {
            flushPara();
            closeList();
            ++i;
            QStringList code;
            while (i < n && !lines.at(i).trimmed().startsWith(QLatin1String("```"))) {
                code.append(htmlEscape(lines.at(i)));
                ++i;
            }
            ++i;  // schliessendes ``` ueberspringen
            htmlParts.append(QStringLiteral("<pre style='%1'>").arg(preStyle())
                             + code.join(QLatin1Char('\n')) + QStringLiteral("</pre>"));
            continue;
        }

        // Ueberschrift
        const QRegularExpressionMatch h = headingRe().match(line);
        if (h.hasMatch()) {
            flushPara();
            closeList();
            // # -> h3 ... damit es nicht riesig wird
            const int level = qMin(int(h.captured(1).size()) + 2, 6);
            htmlParts.append(QStringLiteral("<h%1 style='margin:8px 0 4px;'>%2</h%1>")
                                 .arg(level)
                                 .arg(inlineFmt(h.captured(2))));
            ++i;
            continue;
        }

        // Aufzaehlung
        const QRegularExpressionMatch b = bulletRe().match(line);
        const QRegularExpressionMatch o = orderedRe().match(line);
        if (b.hasMatch() || o.hasMatch()) {
            flushPara();
            const QString want =
                b.hasMatch() ? QStringLiteral("ul") : QStringLiteral("ol");
            if (listKind != want) {
                closeList();
                htmlParts.append(QStringLiteral("<%1 style='margin:4px 0 4px 18px;'>").arg(want));
                listKind = want;
            }
            const QString item = b.hasMatch() ? b.captured(1) : o.captured(1);
            htmlParts.append(QStringLiteral("<li>%1</li>").arg(inlineFmt(item)));
            ++i;
            continue;
        }

        // Leerzeile -> Absatz/Liste abschliessen
        if (stripped.isEmpty()) {
            flushPara();
            closeList();
            ++i;
            continue;
        }

        // normale Textzeile
        closeList();
        para.append(inlineFmt(line));
        ++i;
    }

    flushPara();
    closeList();
    return htmlParts.join(QString());
}

} // namespace ncssh::core
