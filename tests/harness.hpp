// Minimales Test-Harness: TEST()-Makro zur Selbstregistrierung plus
// CHECK/CHECK_EQ-Makros. Bewusst klein — keine externe Test-Bibliothek noetig.
#pragma once

#include <QString>
#include <QStringList>
#include <functional>
#include <iterator>
#include <string>
#include <type_traits>
#include <vector>

namespace ncssh::tests {

struct TestCase {
    std::string suite;
    std::string name;
    std::function<void()> fn;
};

// Registriert einen Test (vom TEST()-Makro benutzt).
int registerTest(const char *suite, const char *name, std::function<void()> fn);

// Wird von den CHECK-Makros bei einem Fehlschlag gerufen.
void reportFailure(const char *file, int line, const std::string &message);

// Fuehrt alle registrierten Tests aus; gibt die Anzahl der Fehlschlaege zurueck.
int runAll();

// Hilfen fuer aussagekraeftige Meldungen.
std::string toText(const QString &value);
std::string toText(const QStringList &value);
std::string toText(const std::string &value);
std::string toText(bool value);

// Alle Zahlentypen (int, size_t, qsizetype, double ...) ueber ein Template —
// getrennte Ueberladungen waeren zwischen size_t/long long mehrdeutig.
template <typename T, std::enable_if_t<std::is_arithmetic_v<T>, int> = 0>
std::string toText(T value)
{
    if constexpr (std::is_floating_point_v<T>)
        return std::to_string(value);
    else
        return std::to_string(static_cast<long long>(value));
}

// Fallback fuer Container (QSet, QVector, std::vector ...): Elemente aufzaehlen,
// damit CHECK_EQ auch dort eine lesbare Meldung erzeugt.
template <typename T>
auto toText(const T &value) -> decltype(std::begin(value), std::string())
{
    std::string out = "{";
    bool first = true;
    for (const auto &item : value) {
        if (!first)
            out += ", ";
        first = false;
        out += toText(item);
    }
    return out + "}";
}

} // namespace ncssh::tests

#define NCSSH_CONCAT_(a, b) a##b
#define NCSSH_CONCAT(a, b) NCSSH_CONCAT_(a, b)

// TEST(Suite, Name) { ... }
#define TEST(suite, name)                                                          \
    static void NCSSH_CONCAT(suite##_##name##_fn, __LINE__)();                     \
    static const int NCSSH_CONCAT(suite##_##name##_reg, __LINE__) =                \
        ::ncssh::tests::registerTest(#suite, #name,                                \
                                     NCSSH_CONCAT(suite##_##name##_fn, __LINE__)); \
    static void NCSSH_CONCAT(suite##_##name##_fn, __LINE__)()

#define CHECK(expr)                                                                \
    do {                                                                           \
        if (!(expr))                                                               \
            ::ncssh::tests::reportFailure(__FILE__, __LINE__,                      \
                                          "CHECK fehlgeschlagen: " #expr);         \
    } while (0)

#define CHECK_EQ(actual, expected)                                                 \
    do {                                                                           \
        const auto &a_ = (actual);                                                 \
        const auto &e_ = (expected);                                               \
        if (!(a_ == e_))                                                           \
            ::ncssh::tests::reportFailure(                                         \
                __FILE__, __LINE__,                                                \
                std::string("CHECK_EQ fehlgeschlagen: " #actual "\n      ist:      ") \
                    + ::ncssh::tests::toText(a_) + "\n      erwartet: "            \
                    + ::ncssh::tests::toText(e_));                                 \
    } while (0)

#define CHECK_THROWS(expr)                                                         \
    do {                                                                           \
        bool threw_ = false;                                                       \
        try {                                                                      \
            (void)(expr);                                                          \
        } catch (...) {                                                            \
            threw_ = true;                                                         \
        }                                                                          \
        if (!threw_)                                                               \
            ::ncssh::tests::reportFailure(__FILE__, __LINE__,                      \
                                          "erwartete Ausnahme: " #expr);           \
    } while (0)
