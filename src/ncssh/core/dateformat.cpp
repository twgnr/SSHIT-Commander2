// Benutzerfreundliches Datumsformat -> strftime.  (Port von core/dateformat.py)
#include "ncssh/core/dateformat.hpp"

#include <QHash>
#include <QLocale>
#include <QRegularExpression>

namespace ncssh::core {

// Token -> strftime. Reihenfolge im Regex: laengere Token zuerst (HH24 vor HH usw.).
static const QHash<QString, QString> &tokenMap()
{
    static const QHash<QString, QString> tokens = {
        {QStringLiteral("YYYY"), QStringLiteral("%Y")},
        {QStringLiteral("YY"), QStringLiteral("%y")},
        {QStringLiteral("MONTH"), QStringLiteral("%B")},
        {QStringLiteral("MON"), QStringLiteral("%b")},
        {QStringLiteral("MM"), QStringLiteral("%m")},
        {QStringLiteral("DD"), QStringLiteral("%d")},
        {QStringLiteral("HH24"), QStringLiteral("%H")},
        {QStringLiteral("HH12"), QStringLiteral("%I")},
        {QStringLiteral("HH"), QStringLiteral("%H")},
        {QStringLiteral("MI"), QStringLiteral("%M")},
        {QStringLiteral("SS"), QStringLiteral("%S")},
        {QStringLiteral("AM"), QStringLiteral("%p")},
        {QStringLiteral("PM"), QStringLiteral("%p")},
    };
    return tokens;
}

QString toStrftime(const QString &fmtIn)
{
    QString fmt = fmtIn.isEmpty() ? QString::fromLatin1(DEFAULT_DATE_FORMAT) : fmtIn;
    fmt.replace(QLatin1String("%"), QLatin1String("%%"));  // literale % schuetzen
    // Laengere Token zuerst, damit z.B. HH24 vor HH gewinnt.
    static const QRegularExpression re(
        QStringLiteral("MONTH|YYYY|HH24|HH12|MON|YY|MM|DD|HH|MI|SS|AM|PM"),
        QRegularExpression::CaseInsensitiveOption);
    QString out;
    out.reserve(fmt.size());
    int last = 0;
    auto it = re.globalMatch(fmt);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += fmt.mid(last, m.capturedStart() - last);
        out += tokenMap().value(m.captured(0).toUpper());
        last = m.capturedEnd();
    }
    out += fmt.mid(last);
    return out;
}

static QString pad2(int v)
{
    return QString::number(v).rightJustified(2, QLatin1Char('0'));
}

QString applyStrftime(const QDateTime &dt, const QString &strftimeFmt)
{
    if (!dt.isValid())
        return {};
    const QDate d = dt.date();
    const QTime t = dt.time();
    // Monatsnamen im C-Locale (englisch) — wie Pythons strftime ohne setlocale.
    const QLocale loc = QLocale::c();
    QString out;
    out.reserve(strftimeFmt.size());
    for (int i = 0; i < strftimeFmt.size(); ++i) {
        const QChar c = strftimeFmt.at(i);
        if (c != QLatin1Char('%')) {
            out += c;
            continue;
        }
        if (i + 1 >= strftimeFmt.size()) {
            out += c;  // einzelnes % am Ende
            break;
        }
        const QChar f = strftimeFmt.at(++i);
        switch (f.unicode()) {
        case 'Y': out += QString::number(d.year()).rightJustified(4, QLatin1Char('0')); break;
        case 'y': out += pad2(d.year() % 100); break;
        case 'B': out += loc.monthName(d.month(), QLocale::LongFormat); break;
        case 'b': out += loc.monthName(d.month(), QLocale::ShortFormat); break;
        case 'm': out += pad2(d.month()); break;
        case 'd': out += pad2(d.day()); break;
        case 'H': out += pad2(t.hour()); break;
        case 'I': out += pad2(t.hour() % 12 == 0 ? 12 : t.hour() % 12); break;
        case 'M': out += pad2(t.minute()); break;
        case 'S': out += pad2(t.second()); break;
        case 'p': out += (t.hour() < 12) ? QStringLiteral("AM") : QStringLiteral("PM"); break;
        case '%': out += QLatin1Char('%'); break;
        default:
            // Unbekannte Codes unveraendert ausgeben (kein Absturz, kein Fallback noetig).
            out += QLatin1Char('%');
            out += f;
            break;
        }
    }
    return out;
}

QString formatDt(const QDateTime &dt, const QString &fmt)
{
    if (!dt.isValid())
        return {};
    return applyStrftime(dt, toStrftime(fmt));
}

} // namespace ncssh::core
