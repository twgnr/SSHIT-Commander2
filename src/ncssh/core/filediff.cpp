#include "ncssh/core/filediff.hpp"

#include <QHash>
#include <QStringList>
#include <algorithm>

namespace ncssh::core {

namespace {

// Ein Diff-Schritt: gleich (equal), entfernt (del), hinzugefuegt (add).
// Nur der Index der Zeile (in a bzw. b) — keine QString-Kopie je Schritt,
// sonst kostet ein 2-Mio.-Zeilen-Vergleich unnoetig viel Speicher.
struct Op {
    char kind;   // '=', '-', '+'
    int line;    // '=' und '-': Index in a, '+': Index in b
};

// Grenzen fuer den Myers-Lauf. Frueher lief hier eine volle LCS-Tabelle
// ((n+1)*(m+1) ints): zwei 40k-Zeilen-Dateien brauchten > 6 GB -> bad_alloc
// bzw. minutenlanges Einfrieren. Myers braucht O((n+m)*D) Zeit und hier
// O(D^2/2) Speicher (D = Zahl der Aenderungen); beides wird gedeckelt.
constexpr int kMaxEditDistance = 3000;          // ~4,5 Mio. ints = ~18 MB Spur
constexpr qint64 kMaxWork = 150'000'000;        // Vergleichsschritte gesamt

// Myers-O(ND)-Diff ueber Zeilen-IDs a[0..n) / b[0..m). Haengt die Schritte an
// ops an (Indizes relativ zu aOff/bOff). false = Grenze ueberschritten, ops
// bleibt dann unveraendert.
bool myersDiff(const std::vector<int> &a, int aOff, int n,
               const std::vector<int> &b, int bOff, int m, std::vector<Op> &ops)
{
    if (n == 0 && m == 0)
        return true;
    const int limit = std::min(n + m, kMaxEditDistance);
    const int off = limit + 1;
    std::vector<int> v(size_t(2 * limit + 3), 0);
    // trace: nach Runde d die x-Werte fuer k = -d, -d+2, ..., d (d+1 Werte).
    std::vector<int> trace;
    qint64 work = 0;
    int found = -1;
    for (int d = 0; d <= limit && found < 0; ++d) {
        for (int k = -d; k <= d; k += 2) {
            int x;
            if (k == -d || (k != d && v[off + k - 1] < v[off + k + 1]))
                x = v[off + k + 1];          // Schritt nach unten (Einfuegung)
            else
                x = v[off + k - 1] + 1;      // Schritt nach rechts (Loeschung)
            int y = x - k;
            while (x < n && y < m && a[aOff + x] == b[bOff + y]) {
                ++x;
                ++y;
                ++work;
            }
            v[off + k] = x;
            ++work;
            if (x >= n && y >= m)
                found = d;
            // Je Diagonale pruefen: eine einzelne Runde kann bei vielen langen
            // Schlangen selbst schon sehr teuer sein.
            if (work > kMaxWork)
                return false;
        }
        for (int k = -d; k <= d; k += 2)
            trace.push_back(v[off + k]);
    }
    if (found < 0)
        return false;

    // Rueckverfolgung von (n, m) zum Ursprung; Schritte rueckwaerts sammeln.
    const auto at = [&trace](int d, int k) {
        // Runde d beginnt bei d*(d+1)/2; k -> Position (k + d) / 2.
        return trace[size_t(qint64(d) * (d + 1) / 2 + (k + d) / 2)];
    };
    std::vector<Op> rev;
    int x = n, y = m;
    for (int d = found; d > 0; --d) {
        const int k = x - y;
        int prevK;
        if (k == -d || (k != d && at(d - 1, k - 1) < at(d - 1, k + 1)))
            prevK = k + 1;
        else
            prevK = k - 1;
        const int prevX = at(d - 1, prevK);
        const int prevY = prevX - prevK;
        const int startX = (prevK == k + 1) ? prevX : prevX + 1;
        while (x > startX) {          // Diagonale (gleiche Zeilen)
            --x;
            --y;
            rev.push_back({'=', aOff + x});
        }
        if (prevK == k + 1)
            rev.push_back({'+', bOff + prevY});
        else
            rev.push_back({'-', aOff + prevX});
        x = prevX;
        y = prevY;
    }
    while (x > 0) {                   // Runde 0: reine Diagonale ab (0, 0)
        --x;
        --y;
        rev.push_back({'=', aOff + x});
    }
    ops.insert(ops.end(), rev.rbegin(), rev.rend());
    return true;
}

// Zeilen-Diff. Gemeinsamer Anfang/Ende wird vorab abgeschnitten (haeufigster
// Fall: wenige Aenderungen in grossen Dateien), dazwischen Myers. Wird eine
// Grenze erreicht, wird der Mittelteil grob als "alles entfernt, alles neu"
// ausgegeben — korrekt, nur nicht minimal; approximate meldet das.
std::vector<Op> diffLines(const QStringList &a, const QStringList &b, bool &approximate)
{
    approximate = false;
    const int n = int(a.size());
    const int m = int(b.size());
    // Zeilen auf ganzzahlige IDs abbilden: Myers vergleicht dann ints statt Strings.
    QHash<QString, int> ids;
    ids.reserve(n + m);
    const auto idOf = [&ids](const QString &s) {
        const auto it = ids.constFind(s);
        if (it != ids.cend())
            return *it;
        const int id = int(ids.size());
        ids.insert(s, id);
        return id;
    };
    std::vector<int> ia(static_cast<size_t>(n)), ib(static_cast<size_t>(m));
    for (int i = 0; i < n; ++i)
        ia[size_t(i)] = idOf(a[i]);
    for (int j = 0; j < m; ++j)
        ib[size_t(j)] = idOf(b[j]);

    int pre = 0;
    while (pre < n && pre < m && ia[size_t(pre)] == ib[size_t(pre)])
        ++pre;
    int suf = 0;
    while (suf < n - pre && suf < m - pre
           && ia[size_t(n - 1 - suf)] == ib[size_t(m - 1 - suf)])
        ++suf;

    std::vector<Op> ops;
    ops.reserve(size_t(n) + size_t(m) - size_t(pre) - size_t(suf));
    for (int i = 0; i < pre; ++i)
        ops.push_back({'=', i});
    const int midA = n - pre - suf;
    const int midB = m - pre - suf;
    if (!myersDiff(ia, pre, midA, ib, pre, midB, ops)) {
        approximate = true;
        for (int i = 0; i < midA; ++i)
            ops.push_back({'-', pre + i});
        for (int j = 0; j < midB; ++j)
            ops.push_back({'+', pre + j});
    }
    for (int i = n - suf; i < n; ++i)
        ops.push_back({'=', i});
    return ops;
}

} // namespace

std::vector<DiffRow> unified(const QString &a, const QString &b,
                             const QString &nameA, const QString &nameB, int context,
                             bool *approximate)
{
    const QStringList aLines = a.split(QLatin1Char('\n'));
    const QStringList bLines = b.split(QLatin1Char('\n'));
    bool approx = false;
    const std::vector<Op> ops = diffLines(aLines, bLines, approx);
    if (approximate)
        *approximate = approx;

    std::vector<DiffRow> rows;
    // Keine Unterschiede -> leere Ausgabe (wie difflib).
    const bool anyChange = std::any_of(ops.begin(), ops.end(),
                                       [](const Op &o) { return o.kind != '='; });
    if (!anyChange)
        return rows;

    rows.emplace_back(QStringLiteral("--- %1").arg(nameA), QStringLiteral("hdr"));
    rows.emplace_back(QStringLiteral("+++ %1").arg(nameB), QStringLiteral("hdr"));

    // Hunks bilden: Aenderungen mit je context Kontextzeilen zusammenfassen.
    const int total = static_cast<int>(ops.size());
    int idx = 0;
    int lineA = 1, lineB = 1;  // 1-basierte Zeilennummern im jeweiligen Text

    // Zeilennummern je Op vorberechnen.
    std::vector<int> startA(total), startB(total);
    {
        int ca = 1, cb = 1;
        for (int k = 0; k < total; ++k) {
            startA[k] = ca;
            startB[k] = cb;
            if (ops[k].kind == '=') { ++ca; ++cb; }
            else if (ops[k].kind == '-') { ++ca; }
            else { ++cb; }
        }
    }

    while (idx < total) {
        // naechste Aenderung suchen
        while (idx < total && ops[idx].kind == '=')
            ++idx;
        if (idx >= total)
            break;
        int hunkStart = std::max(0, idx - context);
        int hunkEnd = idx;
        // Hunk erweitern, solange Aenderungen innerhalb von 2*context folgen.
        while (hunkEnd < total) {
            if (ops[hunkEnd].kind != '=') {
                hunkEnd = hunkEnd + 1;
                continue;
            }
            // pruefen, ob innerhalb der naechsten context-Zeilen noch etwas kommt
            int look = hunkEnd;
            int equalRun = 0;
            while (look < total && ops[look].kind == '=' && equalRun < context * 2) {
                ++look;
                ++equalRun;
            }
            if (look < total && equalRun < context * 2 && ops[look].kind != '=') {
                hunkEnd = look;
                continue;
            }
            break;
        }
        const int tailEnd = std::min(total, hunkEnd + context);

        int countA = 0, countB = 0;
        for (int k = hunkStart; k < tailEnd; ++k) {
            if (ops[k].kind == '=') { ++countA; ++countB; }
            else if (ops[k].kind == '-') ++countA;
            else ++countB;
        }
        lineA = startA[hunkStart];
        lineB = startB[hunkStart];
        rows.emplace_back(QStringLiteral("@@ -%1,%2 +%3,%4 @@")
                              .arg(lineA).arg(countA).arg(lineB).arg(countB),
                          QStringLiteral("hunk"));
        for (int k = hunkStart; k < tailEnd; ++k) {
            const Op &op = ops[k];
            if (op.kind == '=')
                rows.emplace_back(QLatin1Char(' ') + aLines.at(op.line), QStringLiteral("ctx"));
            else if (op.kind == '-')
                rows.emplace_back(QLatin1Char('-') + aLines.at(op.line), QStringLiteral("del"));
            else
                rows.emplace_back(QLatin1Char('+') + bLines.at(op.line), QStringLiteral("add"));
        }
        idx = tailEnd;
    }
    return rows;
}

} // namespace ncssh::core
