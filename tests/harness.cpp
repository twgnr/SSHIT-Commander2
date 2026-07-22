#include "tests/harness.hpp"

#include <cstdio>
#include <map>

namespace ncssh::tests {

namespace {
std::vector<TestCase> &registry()
{
    static std::vector<TestCase> cases;
    return cases;
}

// Fehlschlaege des gerade laufenden Tests.
std::vector<std::string> g_currentFailures;
} // namespace

int registerTest(const char *suite, const char *name, std::function<void()> fn)
{
    registry().push_back({suite, name, std::move(fn)});
    return 0;
}

void reportFailure(const char *file, int line, const std::string &message)
{
    g_currentFailures.push_back(std::string(file) + ":" + std::to_string(line) + "\n      "
                                + message);
}

std::string toText(const QString &value) { return "\"" + value.toStdString() + "\""; }
std::string toText(const QStringList &value)
{
    return "[" + value.join(QStringLiteral(", ")).toStdString() + "]";
}
std::string toText(const std::string &value) { return "\"" + value + "\""; }
std::string toText(bool value) { return value ? "true" : "false"; }

int runAll()
{
    int passed = 0;
    std::map<std::string, int> failuresBySuite;
    std::vector<std::string> failedNames;

    std::string currentSuite;
    for (const TestCase &test : registry()) {
        if (test.suite != currentSuite) {
            currentSuite = test.suite;
            std::printf("\n[%s]\n", currentSuite.c_str());
        }
        g_currentFailures.clear();
        try {
            test.fn();
        } catch (const std::exception &exc) {
            g_currentFailures.push_back(std::string("unerwartete Ausnahme: ") + exc.what());
        } catch (...) {
            g_currentFailures.push_back("unerwartete unbekannte Ausnahme");
        }
        if (g_currentFailures.empty()) {
            std::printf("  ok    %s\n", test.name.c_str());
            ++passed;
        } else {
            std::printf("  FEHL  %s\n", test.name.c_str());
            for (const std::string &f : g_currentFailures)
                std::printf("      %s\n", f.c_str());
            failuresBySuite[test.suite]++;
            failedNames.push_back(test.suite + "." + test.name);
        }
        // Umgeleitete Ausgabe waere sonst blockweise gepuffert — bei einem
        // haengenden Test saehe man gar nichts.
        std::fflush(stdout);
    }

    const int failed = static_cast<int>(failedNames.size());
    std::printf("\n────────────────────────────────────────\n");
    std::printf("%d Tests: %d bestanden, %d fehlgeschlagen\n",
                passed + failed, passed, failed);
    if (failed) {
        std::printf("\nFehlgeschlagen:\n");
        for (const std::string &name : failedNames)
            std::printf("  - %s\n", name.c_str());
    }
    return failed;
}

} // namespace ncssh::tests
