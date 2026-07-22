#include "ncssh/core/diff.hpp"

#include <QMap>
#include <algorithm>

namespace ncssh::core {

static bool sameTime(const FileEntry &left, const FileEntry &right)
{
    if (left.modified.isValid() && right.modified.isValid())
        return std::abs(left.modified.secsTo(right.modified)) < 2;
    return true;
}

static QString newer(const FileEntry &left, const FileEntry &right)
{
    if (left.modified.isValid() && right.modified.isValid())
        return left.modified >= right.modified ? QStringLiteral("newer_left")
                                               : QStringLiteral("newer_right");
    return QStringLiteral("newer_left");
}

static QStringList sortedNames(const QMap<QString, FileEntry> &lmap,
                               const QMap<QString, FileEntry> &rmap)
{
    QStringList names = lmap.keys();
    for (const QString &n : rmap.keys()) {
        if (!lmap.contains(n))
            names.append(n);
    }
    std::sort(names.begin(), names.end(), [](const QString &a, const QString &b) {
        return a.toLower() < b.toLower();
    });
    return names;
}

std::vector<DiffEntry> compare(const std::vector<FileEntry> &left,
                               const std::vector<FileEntry> &right)
{
    QMap<QString, FileEntry> lmap, rmap;
    for (const auto &e : left)
        if (e.type != EntryType::Parent) lmap.insert(e.name, e);
    for (const auto &e : right)
        if (e.type != EntryType::Parent) rmap.insert(e.name, e);

    std::vector<DiffEntry> result;
    for (const QString &name : sortedNames(lmap, rmap)) {
        const bool hasL = lmap.contains(name);
        const bool hasR = rmap.contains(name);
        QString status;
        if (hasL && !hasR)
            status = QStringLiteral("left_only");
        else if (hasR && !hasL)
            status = QStringLiteral("right_only");
        else if (lmap[name].isDir() || rmap[name].isDir())
            status = QStringLiteral("dir");
        else if (lmap[name].size == rmap[name].size && sameTime(lmap[name], rmap[name]))
            status = QStringLiteral("same");
        else
            status = newer(lmap[name], rmap[name]);
        DiffEntry d;
        d.name = name;
        d.status = status;
        if (hasL) d.left = lmap[name];
        if (hasR) d.right = rmap[name];
        result.push_back(std::move(d));
    }
    return result;
}

static void walk(FileSystemProvider *provider, const QString &root, const QString &rel,
                 QMap<QString, FileEntry> &acc, int limit)
{
    const QString full = rel.isEmpty() ? root : provider->join(root, rel);
    std::vector<FileEntry> entries;
    try {
        entries = provider->listDir(full);
    } catch (...) {
        return;
    }
    for (const auto &e : entries) {
        if (e.type == EntryType::Parent)
            continue;
        const QString child = rel.isEmpty() ? e.name : rel + QLatin1Char('/') + e.name;
        if (e.isDir()) {
            if (acc.size() < limit)
                walk(provider, root, child, acc, limit);
        } else {
            acc.insert(child, e);
        }
        if (acc.size() >= limit)
            return;
    }
}

std::vector<DiffEntry> compareRecursive(FileSystemProvider *lprov, const QString &lpath,
                                        FileSystemProvider *rprov, const QString &rpath,
                                        int limit)
{
    QMap<QString, FileEntry> lmap, rmap;
    walk(lprov, lpath, {}, lmap, limit);
    walk(rprov, rpath, {}, rmap, limit);

    std::vector<DiffEntry> result;
    for (const QString &name : sortedNames(lmap, rmap)) {
        const bool hasL = lmap.contains(name);
        const bool hasR = rmap.contains(name);
        QString status;
        if (hasL && !hasR)
            status = QStringLiteral("left_only");
        else if (hasR && !hasL)
            status = QStringLiteral("right_only");
        else if (lmap[name].size == rmap[name].size && sameTime(lmap[name], rmap[name]))
            status = QStringLiteral("same");
        else
            status = newer(lmap[name], rmap[name]);
        DiffEntry d;
        d.name = name;
        d.status = status;
        if (hasL) d.left = lmap[name];
        if (hasR) d.right = rmap[name];
        result.push_back(std::move(d));
    }
    return result;
}

} // namespace ncssh::core
