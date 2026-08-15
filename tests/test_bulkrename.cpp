// Tests fuer die reine Massen-Umbenenn-Logik (core/bulkrename).
#include "tests/harness.hpp"

#include "ncssh/core/bulkrename.hpp"

#include <QSet>
#include <algorithm>

using namespace ncssh::core;

namespace {
std::vector<QString> names(std::initializer_list<const char *> list)
{
    std::vector<QString> out;
    for (const char *s : list)
        out.push_back(QString::fromUtf8(s));
    return out;
}

QStringList newNames(const std::vector<RenamePair> &pairs)
{
    QStringList out;
    for (const auto &p : pairs)
        out << p.second;
    return out;
}

std::vector<int> asVector(const std::vector<int> &v) { return v; }
} // namespace

// --- Sortierung ------------------------------------------------------------

TEST(bulkrename, natural_key_orders_numbers_numerically)
{
    std::vector<QString> list = names({"file10", "file2", "file1"});
    std::sort(list.begin(), list.end(), [](const QString &a, const QString &b) {
        return naturalCompare(a, b) < 0;
    });
    CHECK_EQ(QStringList(list.begin(), list.end()),
             (QStringList{QStringLiteral("file1"), QStringLiteral("file2"),
                          QStringLiteral("file10")}));
}

TEST(bulkrename, sort_indices_name_and_ext)
{
    const auto list = names({"b.txt", "a.png", "c.txt"});
    CHECK_EQ(sortIndices(list, QStringLiteral("name")), (std::vector<int>{1, 0, 2}));
    CHECK_EQ(sortIndices(list, QStringLiteral("name_desc")), (std::vector<int>{2, 0, 1}));
    // nach Endung (png vor txt), innerhalb stabil/natuerlich
    CHECK_EQ(sortIndices(list, QStringLiteral("ext")), (std::vector<int>{1, 0, 2}));
    CHECK_EQ(sortIndices(list, QStringLiteral("none")), (std::vector<int>{0, 1, 2}));
}

TEST(bulkrename, sort_indices_natural)
{
    const auto list = names({"img12", "img2", "img1"});
    CHECK_EQ(sortIndices(list, QStringLiteral("natural")), (std::vector<int>{2, 1, 0}));
}

// --- Konflikte aufloesen ---------------------------------------------------

TEST(bulkrename, auto_resolve_appends_counter_before_extension)
{
    const std::vector<RenamePair> pairs = {
        {QStringLiteral("1.jpg"), QStringLiteral("foto.jpg")},
        {QStringLiteral("2.jpg"), QStringLiteral("foto.jpg")},
        {QStringLiteral("3.jpg"), QStringLiteral("foto.jpg")},
    };
    CHECK_EQ(newNames(autoResolveCollisions(pairs)),
             (QStringList{QStringLiteral("foto.jpg"), QStringLiteral("foto (1).jpg"),
                          QStringLiteral("foto (2).jpg")}));
}

TEST(bulkrename, auto_resolve_respects_unchanged_names)
{
    // "foto.jpg" bleibt unveraendert -> der umbenannte darf ihn nicht ueberschreiben
    const std::vector<RenamePair> pairs = {
        {QStringLiteral("foto.jpg"), QStringLiteral("foto.jpg")},
        {QStringLiteral("x.jpg"), QStringLiteral("foto.jpg")},
    };
    const auto out = autoResolveCollisions(pairs);
    CHECK_EQ(out.size(), size_t(2));
    CHECK_EQ(out[0].second, QStringLiteral("foto.jpg"));
    CHECK_EQ(out[1].second, QStringLiteral("foto (1).jpg"));
}

TEST(bulkrename, auto_resolve_no_extension)
{
    const std::vector<RenamePair> pairs = {
        {QStringLiteral("a"), QStringLiteral("doc")},
        {QStringLiteral("b"), QStringLiteral("doc")},
    };
    CHECK_EQ(newNames(autoResolveCollisions(pairs)),
             (QStringList{QStringLiteral("doc"), QStringLiteral("doc (1)")}));
}

// --- Sichere Reihenfolge ---------------------------------------------------

namespace {
// Simuliert die Umbenennungen und prueft, dass kein Schritt eine noch belegte
// Datei ueberschreibt.
QSet<QString> applySteps(const QStringList &startFiles,
                         const std::vector<RenamePair> &steps)
{
    QSet<QString> present(startFiles.begin(), startFiles.end());
    for (const auto &[src, dst] : steps) {
        if (!present.contains(src))
            ncssh::tests::reportFailure(__FILE__, __LINE__,
                                        "Quelle fehlt: " + src.toStdString());
        if (present.contains(dst))
            ncssh::tests::reportFailure(
                __FILE__, __LINE__,
                "Ziel ist noch belegt -> Datenverlust: " + dst.toStdString());
        present.remove(src);
        present.insert(dst);
    }
    return present;
}
} // namespace

TEST(bulkrename, plan_safe_order_independent)
{
    const std::vector<RenamePair> pairs = {
        {QStringLiteral("a.txt"), QStringLiteral("x.txt")},
        {QStringLiteral("b.txt"), QStringLiteral("y.txt")},
    };
    const auto result = applySteps({QStringLiteral("a.txt"), QStringLiteral("b.txt")},
                                   planSafeOrder(pairs));
    CHECK_EQ(result, (QSet<QString>{QStringLiteral("x.txt"), QStringLiteral("y.txt")}));
}

TEST(bulkrename, plan_safe_order_chain)
{
    // a->b, b->c : b->c muss VOR a->b laufen, sonst ueberschreibt a->b das alte b
    const std::vector<RenamePair> pairs = {
        {QStringLiteral("a"), QStringLiteral("b")},
        {QStringLiteral("b"), QStringLiteral("c")},
    };
    const auto result = applySteps({QStringLiteral("a"), QStringLiteral("b")},
                                   planSafeOrder(pairs));
    CHECK_EQ(result, (QSet<QString>{QStringLiteral("b"), QStringLiteral("c")}));
}

TEST(bulkrename, plan_safe_order_swap_uses_temp)
{
    // Tausch a<->b ist ohne Zwischennamen unmoeglich
    const std::vector<RenamePair> pairs = {
        {QStringLiteral("a"), QStringLiteral("b")},
        {QStringLiteral("b"), QStringLiteral("a")},
    };
    const auto steps = planSafeOrder(pairs);
    CHECK_EQ(steps.size(), size_t(3));   // ein Temp-Schritt zusaetzlich
    const auto result = applySteps({QStringLiteral("a"), QStringLiteral("b")}, steps);
    CHECK_EQ(result, (QSet<QString>{QStringLiteral("a"), QStringLiteral("b")}));
}

TEST(bulkrename, plan_safe_order_ignores_noops)
{
    const std::vector<RenamePair> pairs = {{QStringLiteral("a"), QStringLiteral("a")}};
    CHECK(planSafeOrder(pairs).empty());
}

// --- Nummerierung mit Gruppen-Reset ----------------------------------------

TEST(bulkrename, compute_renames_group_keys_reset_counter)
{
    RenameOptions o;
    o.numbering = true;
    o.start = 1;
    o.width = 2;
    o.numSep = QStringLiteral("_");
    o.groupKeys = names({"dir1", "dir1", "dir2"});
    CHECK_EQ(newNames(computeRenames(names({"a.txt", "b.txt", "c.txt"}), o)),
             (QStringList{QStringLiteral("a_01.txt"), QStringLiteral("b_02.txt"),
                          QStringLiteral("c_01.txt")}));
}

TEST(bulkrename, compute_renames_numbering_without_groups_is_continuous)
{
    RenameOptions o;
    o.numbering = true;
    o.start = 1;
    o.width = 1;
    CHECK_EQ(newNames(computeRenames(names({"a", "b", "c"}), o)),
             (QStringList{QStringLiteral("a1"), QStringLiteral("b2"),
                          QStringLiteral("c3")}));
}

TEST(bulkrename, find_collisions_detects_duplicates)
{
    const std::vector<RenamePair> pairs = {
        {QStringLiteral("a"), QStringLiteral("x")},
        {QStringLiteral("b"), QStringLiteral("x")},
        {QStringLiteral("c"), QStringLiteral("y")},
    };
    CHECK_EQ(findCollisions(pairs), (QSet<QString>{QStringLiteral("x")}));
}
