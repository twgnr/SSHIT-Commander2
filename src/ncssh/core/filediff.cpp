#include "ncssh/core/filediff.hpp"

#include <QStringList>
#include <algorithm>

namespace ncssh::core {

namespace {

// Ein Diff-Schritt: gleich (equal), entfernt (del), hinzugefuegt (add).
struct Op {
    char kind;   // '=', '-', '+'
    QString text;
};

// LCS-Tabelle (Standard-DP) — ausreichend fuer Text-Dateien der ueblichen
// Groesse; entspricht in der Ausgabe difflib.unified_diff.
std::vector<Op> diffLines(const QStringList &a, const QStringList &b)
{
    const int n = a.size();
    const int m = b.size();
    // dp[i][j] = Laenge der LCS von a[i:] und b[j:]
    std::vector<std::vector<int>> dp(n + 1, std::vector<int>(m + 1, 0));
    for (int i = n - 1; i >= 0; --i) {
        for (int j = m - 1; j >= 0; --j) {
            dp[i][j] = (a[i] == b[j]) ? dp[i + 1][j + 1] + 1
                                      : std::max(dp[i + 1][j], dp[i][j + 1]);
        }
    }
    std::vector<Op> ops;
    int i = 0, j = 0;
    while (i < n && j < m) {
        if (a[i] == b[j]) {
            ops.push_back({'=', a[i]});
            ++i;
            ++j;
        } else if (dp[i + 1][j] >= dp[i][j + 1]) {
            ops.push_back({'-', a[i]});
            ++i;
        } else {
            ops.push_back({'+', b[j]});
            ++j;
        }
    }
    while (i < n) ops.push_back({'-', a[i++]});
    while (j < m) ops.push_back({'+', b[j++]});
    return ops;
}

} // namespace

std::vector<DiffRow> unified(const QString &a, const QString &b,
                             const QString &nameA, const QString &nameB, int context)
{
    const QStringList aLines = a.split(QLatin1Char('\n'));
    const QStringList bLines = b.split(QLatin1Char('\n'));
    const std::vector<Op> ops = diffLines(aLines, bLines);

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
                rows.emplace_back(QLatin1Char(' ') + op.text, QStringLiteral("ctx"));
            else if (op.kind == '-')
                rows.emplace_back(QLatin1Char('-') + op.text, QStringLiteral("del"));
            else
                rows.emplace_back(QLatin1Char('+') + op.text, QStringLiteral("add"));
        }
        idx = tailEnd;
    }
    return rows;
}

} // namespace ncssh::core
