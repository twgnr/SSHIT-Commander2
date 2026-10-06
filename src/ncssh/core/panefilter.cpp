#include "ncssh/core/panefilter.hpp"

#include "ncssh/core/i18n.hpp"
#include "ncssh/core/natsort.hpp"

#include <algorithm>

namespace ncssh::core {

namespace {

QStringList splitList(const QString &text, const QString &separators = QStringLiteral(";"))
{
    QStringList out;
    QString current;
    for (const QChar ch : text) {
        if (separators.contains(ch)) {
            if (!current.trimmed().isEmpty())
                out << current.trimmed();
            current.clear();
        } else {
            current += ch;
        }
    }
    if (!current.trimmed().isEmpty())
        out << current.trimmed();
    return out;
}

QString sizeText(qint64 bytes)
{
    static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = double(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    return unit == 0 ? QStringLiteral("%1 B").arg(bytes)
                     : QStringLiteral("%1 %2").arg(value, 0, 'f', value < 10 ? 1 : 0)
                           .arg(QLatin1String(units[unit]));
}

QString unitLabel(const QString &unit)
{
    if (unit == QLatin1String("minutes")) return _t("Minuten");
    if (unit == QLatin1String("hours"))   return _t("Stunden");
    if (unit == QLatin1String("weeks"))   return _t("Wochen");
    if (unit == QLatin1String("months"))  return _t("Monate");
    if (unit == QLatin1String("years"))   return _t("Jahre");
    return _t("Tage");
}

QDateTime subtract(const QDateTime &now, int amount, const QString &unit)
{
    if (unit == QLatin1String("minutes")) return now.addSecs(-qint64(amount) * 60);
    if (unit == QLatin1String("hours"))   return now.addSecs(-qint64(amount) * 3600);
    if (unit == QLatin1String("weeks"))   return now.addDays(-qint64(amount) * 7);
    if (unit == QLatin1String("months"))  return now.addMonths(-amount);
    if (unit == QLatin1String("years"))   return now.addYears(-amount);
    return now.addDays(-amount);
}

} // namespace

bool PaneFilter::isActive() const
{
    return kind != Kind::All || !names.trimmed().isEmpty() || !startsWith.trimmed().isEmpty()
           || !endsWith.trimmed().isEmpty() || !regex.isEmpty()
           || !extensions.trimmed().isEmpty() || dateMode != DateMode::Off || minSize >= 0
           || maxSize >= 0;
}

QString PaneFilter::validate() const
{
    if (!regex.isEmpty()) {
        const QRegularExpression re(regex);
        if (!re.isValid())
            return _t("Ungültiger regulärer Ausdruck: %1").arg(re.errorString());
    }
    if (minSize >= 0 && maxSize >= 0 && minSize > maxSize)
        return _t("Die Mindestgröße ist größer als die Höchstgröße.");
    if (dateMode == DateMode::Between && from.isValid() && to.isValid() && from > to)
        return _t("Der Zeitraum beginnt nach seinem Ende.");
    if ((dateMode == DateMode::OlderThan || dateMode == DateMode::NewerThan) && amount < 0)
        return _t("Die Zeitspanne darf nicht negativ sein.");
    return {};
}

QString PaneFilter::summary() const
{
    QStringList parts;
    if (kind == Kind::FilesOnly)
        parts << _t("nur Dateien");
    else if (kind == Kind::DirsOnly)
        parts << _t("nur Ordner");
    if (!names.trimmed().isEmpty())
        parts << _t("Name: %1").arg(names.trimmed());
    if (!startsWith.trimmed().isEmpty())
        parts << _t("beginnt mit: %1").arg(startsWith.trimmed());
    if (!endsWith.trimmed().isEmpty())
        parts << _t("endet mit: %1").arg(endsWith.trimmed());
    if (!regex.isEmpty())
        parts << _t("Regex: %1").arg(regex);
    if (!extensions.trimmed().isEmpty())
        parts << _t("Endungen: %1").arg(extensions.trimmed());
    const QString field = dateField == QLatin1String("created") ? _t("erstellt") : _t("geändert");
    if (dateMode == DateMode::Between)
        parts << _t("%1 zwischen %2 und %3")
                     .arg(field, from.toString(QStringLiteral("dd.MM.yyyy HH:mm")),
                          to.toString(QStringLiteral("dd.MM.yyyy HH:mm")));
    else if (dateMode == DateMode::OlderThan)
        parts << _t("%1 vor mehr als %2 %3").arg(field).arg(amount).arg(unitLabel(unit));
    else if (dateMode == DateMode::NewerThan)
        parts << _t("%1 in den letzten %2 %3").arg(field).arg(amount).arg(unitLabel(unit));
    if (minSize >= 0)
        parts << _t("ab %1").arg(sizeText(minSize));
    if (maxSize >= 0)
        parts << _t("bis %1").arg(sizeText(maxSize));
    return parts.join(QStringLiteral(" · "));
}

// ---------------------------------------------------------------------------

CompiledPaneFilter::CompiledPaneFilter(const PaneFilter &filter, const QDateTime &now)
    : m_filter(filter), m_active(filter.isActive())
{
    const auto cs = filter.caseSensitive ? QRegularExpression::NoPatternOption
                                         : QRegularExpression::CaseInsensitiveOption;
    for (const QString &pattern : splitList(filter.names)) {
        const bool wildcard = pattern.contains(QLatin1Char('*'))
                              || pattern.contains(QLatin1Char('?'));
        m_namePatterns.emplace_back(
            QRegularExpression::wildcardToRegularExpression(
                wildcard ? pattern : QStringLiteral("*%1*").arg(pattern),
                QRegularExpression::NonPathWildcardConversion),
            cs);
    }
    m_starts = splitList(filter.startsWith);
    m_ends = splitList(filter.endsWith);
    if (!filter.regex.isEmpty())
        m_regex = QRegularExpression(filter.regex, cs);
    for (QString ext : splitList(filter.extensions, QStringLiteral(";, "))) {
        while (ext.startsWith(QLatin1Char('.')) || ext.startsWith(QLatin1Char('*')))
            ext.remove(0, 1);
        if (!ext.isEmpty())
            m_extensions << ext.toLower();
    }
    switch (filter.dateMode) {
    case PaneFilter::DateMode::Between:
        m_from = filter.from;
        m_to = filter.to;
        break;
    case PaneFilter::DateMode::OlderThan:
        m_to = subtract(now, filter.amount, filter.unit);
        break;
    case PaneFilter::DateMode::NewerThan:
        m_from = subtract(now, filter.amount, filter.unit);
        break;
    case PaneFilter::DateMode::Off:
        break;
    }
}

bool CompiledPaneFilter::matchesName(const QString &name) const
{
    const Qt::CaseSensitivity cs = m_filter.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    if (!m_namePatterns.empty()
        && std::none_of(m_namePatterns.begin(), m_namePatterns.end(),
                        [&](const QRegularExpression &re) { return re.match(name).hasMatch(); }))
        return false;
    if (!m_starts.isEmpty()
        && std::none_of(m_starts.begin(), m_starts.end(),
                        [&](const QString &s) { return name.startsWith(s, cs); }))
        return false;
    if (!m_ends.isEmpty()
        && std::none_of(m_ends.begin(), m_ends.end(),
                        [&](const QString &s) { return name.endsWith(s, cs); }))
        return false;
    if (m_regex.isValid() && !m_regex.pattern().isEmpty() && !m_regex.match(name).hasMatch())
        return false;
    return true;
}

bool CompiledPaneFilter::matchesDate(const FileEntry &entry) const
{
    if (m_filter.dateMode == PaneFilter::DateMode::Off)
        return true;
    const QDateTime &value =
        m_filter.dateField == QLatin1String("created") ? entry.created : entry.modified;
    if (!value.isValid())
        return false;   // unbekanntes Datum erfuellt keinen Zeitraum
    if (m_filter.dateMode == PaneFilter::DateMode::OlderThan)
        return value < m_to;
    if (m_from.isValid() && value < m_from)
        return false;
    if (m_to.isValid() && value > m_to)
        return false;
    return true;
}

bool CompiledPaneFilter::matches(const FileEntry &entry) const
{
    if (entry.type == EntryType::Parent || entry.name == QLatin1String(".."))
        return true;
    if (!m_active)
        return true;
    const bool dir = entry.isDir();
    if (m_filter.kind == PaneFilter::Kind::FilesOnly && dir)
        return false;
    if (m_filter.kind == PaneFilter::Kind::DirsOnly && !dir)
        return false;
    if (dir) {
        // Groesse und Endung gelten nur fuer Dateien.
        if (!m_filter.applyToDirs)
            return true;
        return matchesName(entry.name) && matchesDate(entry);
    }
    if (!matchesName(entry.name) || !matchesDate(entry))
        return false;
    if (m_filter.minSize >= 0 && entry.size < m_filter.minSize)
        return false;
    if (m_filter.maxSize >= 0 && entry.size > m_filter.maxSize)
        return false;
    if (!m_extensions.isEmpty()) {
        const int dot = entry.name.lastIndexOf(QLatin1Char('.'));
        const QString ext = dot > 0 ? entry.name.mid(dot + 1).toLower() : QString();
        if (!m_extensions.contains(ext))
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------

void sortEntries(std::vector<FileEntry> &entries, const QList<SortKey> &keysIn,
                 bool dirsFirst, bool natural)
{
    QList<SortKey> keys = keysIn;
    if (keys.isEmpty())
        keys << SortKey{};
    auto extOf = [](const FileEntry &e) {
        const int dot = e.name.lastIndexOf(QLatin1Char('.'));
        return dot > 0 ? e.name.mid(dot + 1).toLower() : QString();
    };
    auto cmpText = [](const QString &a, const QString &b) {
        return a < b ? -1 : (b < a ? 1 : 0);
    };
    // Dreiwertiger Vergleich je Spalte: <0, 0 (gleich), >0.
    auto compare = [&](const QString &column, const FileEntry &a, const FileEntry &b) -> int {
        auto three = [](const auto &x, const auto &y) { return x < y ? -1 : (y < x ? 1 : 0); };
        if (column == QLatin1String("size"))     return three(a.size, b.size);
        if (column == QLatin1String("modified")) return three(a.modified, b.modified);
        if (column == QLatin1String("created"))  return three(a.created, b.created);
        if (column == QLatin1String("accessed")) return three(a.accessed, b.accessed);
        if (column == QLatin1String("type"))     return three(int(a.type), int(b.type));
        if (column == QLatin1String("ext"))      return cmpText(extOf(a), extOf(b));
        if (column == QLatin1String("perm"))     return three(a.permissions, b.permissions);
        if (column == QLatin1String("owner"))
            return cmpText(a.owner.toLower(), b.owner.toLower());
        if (natural) {
            if (naturalLess(a.name, b.name)) return -1;
            if (naturalLess(b.name, a.name)) return 1;
            return 0;
        }
        return cmpText(a.name.toLower(), b.name.toLower());
    };

    std::stable_sort(entries.begin(), entries.end(), [&](const FileEntry &a, const FileEntry &b) {
        if (a.type == EntryType::Parent) return b.type != EntryType::Parent;
        if (b.type == EntryType::Parent) return false;
        if (dirsFirst && a.isDir() != b.isDir()) return a.isDir();
        for (const SortKey &key : keys) {
            const int c = compare(key.column, a, b);
            if (c != 0)
                return key.ascending ? c < 0 : c > 0;
        }
        return false;
    });
}

} // namespace ncssh::core
