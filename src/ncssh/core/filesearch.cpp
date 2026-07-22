#include "ncssh/core/filesearch.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <utility>
#include <vector>

namespace ncssh::core {

// Dateien groesser als dies werden bei der Inhaltssuche nur bis zu dieser Grenze
// gelesen (Schutz vor Speicherueberlauf bei riesigen Logdateien o. Ae.).
static constexpr qint64 kMaxContentBytes = 50'000'000;

namespace {

// Vorkompilierte Muster fuer einen Suchlauf.
struct Compiled {
    std::optional<QRegularExpression> rx;        // Inhalts-/Namen-Regex
    std::optional<QRegularExpression> wildcard;  // Name-Modus, Glob
    std::vector<QRegularExpression> includeRx;   // Datei-Glob (Include)
    std::vector<QRegularExpression> excludeRx;   // Datei-Glob (Exclude)
    QSet<QString> excludeDir;                    // Ordnernamen (lower)
};

QString joinPath(const QString &dir, const QString &name)
{
    const QChar sep = QDir::separator();
    if (dir.endsWith(sep) || dir.endsWith(QLatin1Char('/')))
        return dir + name;
    return dir + sep + name;
}

bool looksBinary(const QByteArray &chunk)
{
    return chunk.contains('\0');
}

// Wie Python str.splitlines(): trennt an allen Zeilenumbruch-Zeichen,
// behandelt "\r\n" als eine Grenze und verwirft eine leere Schlusszeile.
QStringList splitLines(const QString &text)
{
    const auto isBreak = [](ushort u) -> bool {
        switch (u) {
        case 0x0A: case 0x0B: case 0x0C: case 0x0D:
        case 0x1C: case 0x1D: case 0x1E: case 0x85:
        case 0x2028: case 0x2029:
            return true;
        default:
            return false;
        }
    };
    QStringList out;
    const int n = text.size();
    int start = 0;
    int i = 0;
    while (i < n) {
        const ushort u = text.at(i).unicode();
        if (isBreak(u)) {
            out.append(text.mid(start, i - start));
            if (u == 0x0D && i + 1 < n && text.at(i + 1).unicode() == 0x0A)
                i += 2;
            else
                i += 1;
            start = i;
        } else {
            ++i;
        }
    }
    if (start < n)
        out.append(text.mid(start, n - start));
    return out;
}

bool nameHit(const QString &name, const SearchOptions &opts, const Compiled &c)
{
    if (opts.pattern.isEmpty())
        return true;
    if (opts.regex)
        return c.rx ? c.rx->match(name).hasMatch() : true;
    const QString n = opts.ignoreCase ? name.toLower() : name;
    return c.wildcard && c.wildcard->match(n).hasMatch();
}

bool globAny(const QString &nameLower, const std::vector<QRegularExpression> &patterns)
{
    for (const QRegularExpression &rx : patterns)
        if (rx.match(nameLower).hasMatch())
            return true;
    return false;
}

bool passesFileFilters(const QFileInfo &fi, const SearchOptions &opts,
                       const Compiled &c, qint64 now)
{
    const qint64 size = fi.size();
    if (opts.minSize.has_value() && size < *opts.minSize)
        return false;
    if (opts.maxSize.has_value() && size > *opts.maxSize)
        return false;
    if (opts.newerThanDays.has_value()) {
        const qint64 mtime = fi.lastModified().toSecsSinceEpoch();
        if (static_cast<double>(mtime) < static_cast<double>(now) - *opts.newerThanDays * 86400.0)
            return false;
    }
    const QString baseLower = fi.fileName().toLower();
    if (!c.includeRx.empty() && !globAny(baseLower, c.includeRx))
        return false;
    if (!c.excludeRx.empty() && globAny(baseLower, c.excludeRx))
        return false;
    return true;
}

// Per-Verzeichnis-Besucher; Rueckgabe false stoppt den gesamten Walk.
using WalkVisitor = std::function<bool(const QString &dirpath, const QStringList &dirnames,
                                       const QStringList &filenames, int depth)>;

void walkDir(const QString &dirpath, int depth, const SearchOptions &opts,
             const QSet<QString> &excl, const CancelTokenPtr &cancel,
             const WalkVisitor &visit, bool &stop)
{
    if (stop)
        return;
    if (cancel && cancel->isCancelled()) {
        stop = true;
        return;
    }
    QDir dir(dirpath);
    const auto infos = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot
                                             | QDir::Hidden | QDir::System);
    QStringList dirnames;
    QStringList filenames;
    QSet<QString> symlinkDirs;
    for (const QFileInfo &fi : infos) {
        if (fi.isDir()) {
            dirnames << fi.fileName();
            if (fi.isSymLink())
                symlinkDirs.insert(fi.fileName());
        } else {
            filenames << fi.fileName();
        }
    }
    // Ordner-Ausschluss.
    if (!excl.isEmpty()) {
        QStringList kept;
        for (const QString &d : std::as_const(dirnames))
            if (!excl.contains(d.toLower()))
                kept << d;
        dirnames = kept;
    }
    // Tiefenbegrenzung: bei erreichter Tiefe nicht weiter absteigen.
    if (opts.maxDepth.has_value() && depth >= *opts.maxDepth)
        dirnames.clear();

    if (!visit(dirpath, dirnames, filenames, depth)) {
        stop = true;
        return;
    }
    for (const QString &d : std::as_const(dirnames)) {
        if (symlinkDirs.contains(d))  // Symlinks nicht folgen (os.walk followlinks=False)
            continue;
        walkDir(joinPath(dirpath, d), depth + 1, opts, excl, cancel, visit, stop);
        if (stop)
            return;
    }
}

void walk(const QString &root, const SearchOptions &opts, const QSet<QString> &excl,
          const CancelTokenPtr &cancel, const WalkVisitor &visit)
{
    const QString start = QDir::toNativeSeparators(
        QDir::cleanPath(QFileInfo(root).absoluteFilePath()));
    bool stop = false;
    walkDir(start, 0, opts, excl, cancel, visit, stop);
}

void iterName(const QString &root, const SearchOptions &opts, const Compiled &c,
              qint64 now, const HitCallback &onHit, const CancelTokenPtr &cancel)
{
    int count = 0;
    walk(root, opts, c.excludeDir, cancel,
         [&](const QString &dirpath, const QStringList &dirnames,
             const QStringList &filenames, int) -> bool {
             std::vector<std::pair<QString, bool>> groups;
             if (opts.kind == QLatin1String("all") || opts.kind == QLatin1String("dir"))
                 for (const QString &d : dirnames)
                     groups.emplace_back(d, true);
             if (opts.kind == QLatin1String("all") || opts.kind == QLatin1String("file"))
                 for (const QString &f : filenames)
                     groups.emplace_back(f, false);
             for (const auto &[name, isDir] : groups) {
                 if (!nameHit(name, opts, c))
                     continue;
                 const QString full = joinPath(dirpath, name);
                 if (!isDir) {
                     const QFileInfo fi(full);
                     if (!fi.exists())
                         continue;
                     if (!passesFileFilters(fi, opts, c, now))
                         continue;
                 }
                 onHit(full);
                 ++count;
                 if (count >= opts.limit)
                     return false;
             }
             return true;
         });
}

void iterContent(const QString &root, const SearchOptions &opts, const Compiled &c,
                 qint64 now, const HitCallback &onHit, const CancelTokenPtr &cancel)
{
    if (!c.rx)
        return;
    const QRegularExpression &rx = *c.rx;
    int count = 0;
    walk(root, opts, c.excludeDir, cancel,
         [&](const QString &dirpath, const QStringList &, const QStringList &filenames,
             int) -> bool {
             for (const QString &name : filenames) {
                 if (cancel && cancel->isCancelled())
                     return false;
                 const QString full = joinPath(dirpath, name);
                 const QFileInfo fi(full);
                 if (!fi.exists())
                     continue;
                 if (!passesFileFilters(fi, opts, c, now))
                     continue;
                 QFile fh(full);
                 if (!fh.open(QIODevice::ReadOnly))
                     continue;
                 QByteArray head = fh.read(8192);
                 if (!opts.includeBinary && looksBinary(head)) {
                     fh.close();
                     continue;
                 }
                 // OOM-Schutz: bei riesigen Dateien nur die ersten kMaxContentBytes
                 // durchsuchen (statt alles in den RAM zu laden).
                 QByteArray data = head;
                 const qint64 more = kMaxContentBytes - static_cast<qint64>(head.size());
                 if (more > 0)
                     data += fh.read(more);
                 fh.close();

                 const QString text = QString::fromUtf8(data);
                 const QStringList lines = splitLines(text);
                 bool emittedFile = false;
                 const int ctx = opts.context;
                 int lastEmitted = -1;
                 for (int i = 0; i < lines.size(); ++i) {
                     const bool hit = (rx.match(lines.at(i)).hasMatch()) != opts.invert;
                     if (!hit)
                         continue;
                     if (opts.namesOnly) {
                         onHit(full);
                         ++count;
                         emittedFile = true;
                         break;
                     }
                     const int lo = std::max(0, i - ctx);
                     const int hi = std::min(static_cast<int>(lines.size()), i + ctx + 1);
                     for (int j = lo; j < hi; ++j) {
                         if (j <= lastEmitted)
                             continue;
                         onHit(full + QLatin1Char(':') + QString::number(j + 1)
                               + QLatin1Char(':') + lines.at(j));
                         lastEmitted = j;
                         ++count;
                         if (count >= opts.limit)
                             return false;
                     }
                 }
                 if (emittedFile && count >= opts.limit)
                     return false;
             }
             if (count >= opts.limit)
                 return false;
             return true;
         });
}

} // namespace

std::optional<QRegularExpression> buildRegex(const SearchOptions &opts)
{
    if (opts.pattern.isEmpty())
        return std::nullopt;
    QString pat = opts.regex ? opts.pattern : QRegularExpression::escape(opts.pattern);
    if (opts.wholeWord)
        pat = QStringLiteral("\\b") + pat + QStringLiteral("\\b");
    const QRegularExpression::PatternOptions po =
        opts.ignoreCase ? QRegularExpression::CaseInsensitiveOption
                        : QRegularExpression::NoPatternOption;
    QRegularExpression rx(pat, po);
    if (!rx.isValid())  // ungueltig -> fester Text
        rx = QRegularExpression(QRegularExpression::escape(opts.pattern), po);
    return rx;
}

void iterSearch(const QString &root, const SearchOptions &opts, const HitCallback &onHit,
                const CancelTokenPtr &cancel)
{
    Compiled c;
    c.rx = buildRegex(opts);
    if (!opts.regex && !opts.pattern.isEmpty()) {
        const QString glob = opts.ignoreCase ? opts.pattern.toLower() : opts.pattern;
        c.wildcard = QRegularExpression(QRegularExpression::wildcardToRegularExpression(glob));
    }
    for (const QString &p : opts.include)
        c.includeRx.emplace_back(QRegularExpression::wildcardToRegularExpression(p.toLower()));
    for (const QString &p : opts.exclude)
        c.excludeRx.emplace_back(QRegularExpression::wildcardToRegularExpression(p.toLower()));
    for (const QString &d : opts.excludeDir)
        c.excludeDir.insert(d.toLower());

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (opts.mode == QLatin1String("name"))
        iterName(root, opts, c, now, onHit, cancel);
    else
        iterContent(root, opts, c, now, onHit, cancel);
}

} // namespace ncssh::core
