#include "ncssh/core/bulkrename.hpp"

#include <QRegularExpression>
#include <algorithm>
#include <functional>

namespace ncssh::core {

const QStringList &pipelineDoc()
{
    static const QStringList doc = {
        QStringLiteral("Suchen & Ersetzen"), QStringLiteral("Zuschneiden"),
        QStringLiteral("Einfügen"), QStringLiteral("Groß-/Kleinschreibung"),
        QStringLiteral("Leerzeichen"), QStringLiteral("Präfix & Suffix"),
        QStringLiteral("Nummerierung"), QStringLiteral("Dateiendung"),
    };
    return doc;
}

// Zerlegt in Text-/Zahl-Abschnitte (wie re.split(r"(\d+)")).
static QStringList numSplit(const QString &name)
{
    QStringList parts;
    static const QRegularExpression re(QStringLiteral("(\\d+)"));
    int last = 0;
    auto it = re.globalMatch(name);
    while (it.hasNext()) {
        const auto m = it.next();
        parts << name.mid(last, m.capturedStart() - last);  // Text (ggf. leer)
        parts << m.captured(1);                             // Zahl
        last = m.capturedEnd();
    }
    parts << name.mid(last);
    return parts;
}

int naturalCompare(const QString &a, const QString &b)
{
    const QStringList pa = numSplit(a);
    const QStringList pb = numSplit(b);
    const int n = qMin(pa.size(), pb.size());
    for (int i = 0; i < n; ++i) {
        const QString &x = pa[i];
        const QString &y = pb[i];
        const bool xNum = (i % 2 == 1);  // ungerade Positionen sind Zahlen
        if (xNum) {
            // Fuehrende Nullen ignorieren, numerisch vergleichen.
            const qlonglong nx = x.toLongLong();
            const qlonglong ny = y.toLongLong();
            if (nx != ny)
                return nx < ny ? -1 : 1;
        } else {
            const QString lx = x.toLower();
            const QString ly = y.toLower();
            if (lx != ly)
                return lx < ly ? -1 : 1;
        }
    }
    if (pa.size() != pb.size())
        return pa.size() < pb.size() ? -1 : 1;
    return 0;
}

std::pair<QString, QString> splitName(const QString &name)
{
    // os.path.splitext: fuehrende Punkte gehoeren zum Basisnamen (".bashrc").
    int dot = name.lastIndexOf(QLatin1Char('.'));
    // Punkt am Anfang (oder nur fuehrende Punkte) zaehlt nicht als Endung.
    int firstNonDot = 0;
    while (firstNonDot < name.size() && name[firstNonDot] == QLatin1Char('.'))
        ++firstNonDot;
    if (dot <= firstNonDot - 1 || dot < 0)
        return {name, QString()};
    if (dot < firstNonDot)
        return {name, QString()};
    return {name.left(dot), name.mid(dot)};
}

std::vector<int> sortIndices(const std::vector<QString> &names, const QString &mode)
{
    std::vector<int> idx(names.size());
    for (int i = 0; i < int(names.size()); ++i)
        idx[i] = i;

    if (mode == QLatin1String("name") || mode == QLatin1String("natural")) {
        std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) {
            return naturalCompare(names[a], names[b]) < 0;
        });
    } else if (mode == QLatin1String("name_desc")) {
        std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) {
            return naturalCompare(names[a], names[b]) > 0;
        });
    } else if (mode == QLatin1String("ext")) {
        std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) {
            const QString ea = splitName(names[a]).second.toLower();
            const QString eb = splitName(names[b]).second.toLower();
            if (ea != eb)
                return ea < eb;
            return naturalCompare(names[a], names[b]) < 0;
        });
    }
    return idx;
}

std::vector<RenamePair> autoResolveCollisions(const std::vector<RenamePair> &pairs)
{
    QSet<QString> fixed;
    for (const auto &p : pairs) {
        if (p.first == p.second && !p.second.isEmpty())
            fixed.insert(p.second);
    }
    QSet<QString> seen = fixed;
    std::vector<RenamePair> out;
    for (const auto &p : pairs) {
        if (p.first == p.second || p.second.isEmpty()) {
            out.push_back(p);
            continue;
        }
        QString cand = p.second;
        if (seen.contains(cand)) {
            const auto [base, ext] = splitName(p.second);
            int i = 1;
            cand = QStringLiteral("%1 (%2)%3").arg(base).arg(i).arg(ext);
            while (seen.contains(cand)) {
                ++i;
                cand = QStringLiteral("%1 (%2)%3").arg(base).arg(i).arg(ext);
            }
        }
        seen.insert(cand);
        out.emplace_back(p.first, cand);
    }
    return out;
}

std::vector<RenamePair> planSafeOrder(const std::vector<RenamePair> &pairs,
                                      const QSet<QString> &existing)
{
    std::vector<RenamePair> remaining;
    for (const auto &p : pairs) {
        if (p.first != p.second)
            remaining.push_back(p);
    }
    if (remaining.empty())
        return {};

    QSet<QString> allNames = existing;
    for (const auto &p : remaining) {
        allNames.insert(p.first);
        allNames.insert(p.second);
    }
    const auto makeTemp = [&]() -> QString {
        int i = 0;
        while (true) {
            const QString t = QStringLiteral(".sshit-rename-tmp-%1").arg(i);
            if (!allNames.contains(t)) {
                allNames.insert(t);
                return t;
            }
            ++i;
        }
    };

    std::vector<RenamePair> ops;
    QSet<QString> srcSet;
    for (const auto &p : remaining)
        srcSet.insert(p.first);

    while (!remaining.empty()) {
        bool progressed = false;
        for (int k = 0; k < int(remaining.size()); ++k) {
            const auto [s, d] = remaining[k];
            if (!srcSet.contains(d)) {
                ops.emplace_back(s, d);
                remaining.erase(remaining.begin() + k);
                srcSet.remove(s);
                progressed = true;
                break;
            }
        }
        if (!progressed) {
            // nur noch Zyklen uebrig -> einen ueber einen Temp-Namen aufbrechen
            const auto [s, d] = remaining.front();
            remaining.erase(remaining.begin());
            const QString tmp = makeTemp();
            ops.emplace_back(s, tmp);
            srcSet.remove(s);
            remaining.emplace_back(tmp, d);
            srcSet.insert(tmp);
        }
    }
    return ops;
}

QString wildcardToRegex(const QString &pattern)
{
    QString out;
    for (const QChar ch : pattern) {
        if (ch == QLatin1Char('*'))
            out += QStringLiteral(".*");
        else if (ch == QLatin1Char('?'))
            out += QLatin1Char('.');
        else
            out += QRegularExpression::escape(QString(ch));
    }
    return out;
}

std::optional<QString> validateRegex(const QString &pattern)
{
    const QRegularExpression re(pattern);
    if (!re.isValid())
        return re.errorString();
    return std::nullopt;
}

bool nameMatches(const QString &name, const QString &glob,
                 const std::vector<QString> &extensions, bool ignoreCase)
{
    if (!extensions.empty()) {
        QString ext = splitName(name).second;
        while (ext.startsWith(QLatin1Char('.')))
            ext.remove(0, 1);
        QSet<QString> wanted;
        for (QString e : extensions) {
            e = e.trimmed();
            while (e.startsWith(QLatin1Char('.')))
                e.remove(0, 1);
            if (!e.isEmpty())
                wanted.insert(e.toLower());
        }
        if (!wanted.isEmpty() && !wanted.contains(ext.toLower()))
            return false;
    }
    if (!glob.isEmpty()) {
        auto opts = ignoreCase ? QRegularExpression::CaseInsensitiveOption
                               : QRegularExpression::NoPatternOption;
        const QRegularExpression re(
            QRegularExpression::anchoredPattern(wildcardToRegex(glob)), opts);
        if (!re.match(name).hasMatch())
            return false;
    }
    return true;
}

// --- Kern-Transformation ---------------------------------------------------

using Replacer = std::function<QString(const QString &)>;

static Replacer makeReplacer(const QString &search, const QString &replace,
                             const QString &mode, bool ignoreCase, bool replaceAll)
{
    if (search.isEmpty())
        return [](const QString &s) { return s; };
    auto opts = ignoreCase ? QRegularExpression::CaseInsensitiveOption
                           : QRegularExpression::NoPatternOption;
    QString pattern;
    QString repl;
    if (mode == QLatin1String("regex")) {
        pattern = search;               // Profi: Gruppen erlaubt
        repl = replace;
    } else if (mode == QLatin1String("wildcard")) {
        pattern = wildcardToRegex(search);
        repl = replace;
        repl.replace(QLatin1String("\\"), QLatin1String("\\\\"));  // woertlich
    } else {  // "text"
        pattern = QRegularExpression::escape(search);
        repl = replace;
        repl.replace(QLatin1String("\\"), QLatin1String("\\\\"));  // woertlich
    }
    const QRegularExpression rx(pattern, opts);
    if (!rx.isValid())
        return [](const QString &s) { return s; };  // ungueltig -> keine Aenderung
    // QRegularExpression nutzt \1-Backreferences im Ersetzungsstring nicht; Qt
    // erwartet \1. Python nutzt ebenfalls \1 -> passt.
    const bool all = replaceAll;
    return [rx, repl, all](const QString &s) -> QString {
        if (all)
            return QString(s).replace(rx, repl);
        // nur erstes Vorkommen ersetzen
        const auto m = rx.match(s);
        if (!m.hasMatch())
            return s;
        QString result = s;
        // Ersetzung des ersten Treffers unter Beruecksichtigung von \1-Gruppen:
        QString expanded;
        for (int i = 0; i < repl.size(); ++i) {
            if (repl[i] == QLatin1Char('\\') && i + 1 < repl.size()
                && repl[i + 1].isDigit()) {
                expanded += m.captured(repl[i + 1].digitValue());
                ++i;
            } else {
                expanded += repl[i];
            }
        }
        result.replace(m.capturedStart(), m.capturedLength(), expanded);
        return result;
    };
}

static QString applyCase(const QString &text, const QString &mode)
{
    if (mode == QLatin1String("lower"))
        return text.toLower();
    if (mode == QLatin1String("upper"))
        return text.toUpper();
    if (mode == QLatin1String("title")) {
        QString out;
        bool startWord = true;
        for (const QChar c : text) {
            if (c.isLetterOrNumber()) {
                out += startWord ? c.toUpper() : c.toLower();
                startWord = false;
            } else {
                out += c;
                startWord = true;
            }
        }
        return out;
    }
    if (mode == QLatin1String("sentence")) {
        if (text.isEmpty())
            return text;
        return text.left(1).toUpper() + text.mid(1).toLower();
    }
    return text;
}

static QString applySpaces(const QString &text, const QString &mode)
{
    if (mode == QLatin1String("underscore"))
        return QString(text).replace(QLatin1Char(' '), QLatin1Char('_'));
    if (mode == QLatin1String("dash"))
        return QString(text).replace(QLatin1Char(' '), QLatin1Char('-'));
    if (mode == QLatin1String("remove"))
        return QString(text).remove(QLatin1Char(' '));
    return text;
}

static QString transformCore(QString base, const Replacer &replacer,
                             const RenameOptions &o)
{
    base = replacer(base);
    if (!o.removeText.isEmpty()) {
        auto opts = o.ignoreCase ? QRegularExpression::CaseInsensitiveOption
                                 : QRegularExpression::NoPatternOption;
        base.remove(QRegularExpression(QRegularExpression::escape(o.removeText), opts));
    }
    if (o.trimStart > 0)
        base = base.mid(o.trimStart);
    if (o.trimEnd > 0)
        base = o.trimEnd < base.size() ? base.left(base.size() - o.trimEnd) : QString();
    if (!o.insertText.isEmpty()) {
        int pos = o.insertPos;
        if (pos < 0)
            pos = qMax(0, base.size() + pos + 1);
        pos = qMin(pos, base.size());
        base = base.left(pos) + o.insertText + base.mid(pos);
    }
    base = applyCase(base, o.caseMode);
    base = applySpaces(base, o.spaceMode);
    return base;
}

std::vector<RenamePair> computeRenames(const std::vector<QString> &names,
                                       const RenameOptions &o)
{
    const Replacer replacer =
        makeReplacer(o.search, o.replace, o.matchMode, o.ignoreCase, o.replaceAll);

    std::vector<RenamePair> out;
    int counter = o.start;
    const bool hasGroups = !o.groupKeys.empty();
    QString prevKey;
    bool firstEntry = true;

    for (int i = 0; i < int(names.size()); ++i) {
        const QString &name = names[i];
        if (hasGroups) {
            const QString key = (i < int(o.groupKeys.size())) ? o.groupKeys[i] : QString();
            if (firstEntry || key != prevKey) {
                if (!firstEntry)
                    counter = o.start;
                prevKey = key;
            }
        }
        firstEntry = false;

        const auto [base, ext] = splitName(name);
        QString extNoDot = ext.startsWith(QLatin1Char('.')) ? ext.mid(1) : ext;

        QString newBase;
        QString newExtNoDot;
        if (o.scope == QLatin1String("full")) {
            const QString newFull = transformCore(name, replacer, o);
            const auto [nb, ne] = splitName(newFull);
            newBase = nb;
            newExtNoDot = ne.startsWith(QLatin1Char('.')) ? ne.mid(1) : ne;
        } else if (o.scope == QLatin1String("ext")) {
            newBase = base;
            newExtNoDot = transformCore(extNoDot, replacer, o);
        } else {  // "name"
            newBase = transformCore(base, replacer, o);
            newExtNoDot = extNoDot;
        }

        newBase = o.prefix + newBase + o.suffix;
        if (o.numbering) {
            const QString num =
                QString::number(counter).rightJustified(qMax(1, o.width), QLatin1Char('0'));
            counter += o.step;
            if (o.numPosition == QLatin1String("prefix")) {
                newBase = num + o.numSep + newBase;
            } else if (o.numPosition == QLatin1String("at")
                       || o.numPosition == QLatin1String("at_replace")) {
                const int pos = qMin(qMax(0, o.numPos), newBase.size());
                const QString left = newBase.left(pos);
                const QString right = o.numPosition == QLatin1String("at_replace")
                                          ? QString()
                                          : newBase.mid(pos);
                newBase = left + o.numSep + num + right;
            } else {  // "suffix"
                newBase = newBase + o.numSep + num;
            }
        }

        if (o.scope != QLatin1String("full")) {
            if (o.extMode == QLatin1String("lower"))
                newExtNoDot = newExtNoDot.toLower();
            else if (o.extMode == QLatin1String("upper"))
                newExtNoDot = newExtNoDot.toUpper();
            else if (o.extMode == QLatin1String("set")) {
                newExtNoDot = o.extValue;
                while (newExtNoDot.startsWith(QLatin1Char('.')))
                    newExtNoDot.remove(0, 1);
            }
        }

        const QString newName =
            newBase + (newExtNoDot.isEmpty() ? QString() : QLatin1Char('.') + newExtNoDot);
        out.emplace_back(name, newName);
    }
    return out;
}

QSet<QString> findCollisions(const std::vector<RenamePair> &pairs)
{
    QHash<QString, int> counts;
    for (const auto &p : pairs)
        counts[p.second]++;
    QSet<QString> out;
    for (auto it = counts.begin(); it != counts.end(); ++it) {
        if (it.value() > 1)
            out.insert(it.key());
    }
    return out;
}

} // namespace ncssh::core
