// Tests fuer die GitHub-Repo-Alarm-Logik (ohne Netzwerk).
// (Port von tests/test_githubalarm.py)
#include "tests/harness.hpp"

#include "ncssh/core/githubalarm.hpp"

using namespace ncssh::core;

namespace {
// Bequemer Vergleich: parseRepoInput liefert optional<pair>.
bool parsesTo(const char *input, const char *owner, const char *repo)
{
    const auto parsed = parseRepoInput(QString::fromUtf8(input));
    return parsed && parsed->first == QString::fromUtf8(owner)
           && parsed->second == QString::fromUtf8(repo);
}
} // namespace

TEST(githubalarm, parse_repo_input_variants)
{
    CHECK(parsesTo("octocat/Hello-World", "octocat", "Hello-World"));
    CHECK(parsesTo("https://github.com/octocat/Hello-World", "octocat", "Hello-World"));
    CHECK(parsesTo("https://github.com/octocat/Hello-World.git", "octocat", "Hello-World"));
    CHECK(parsesTo("git@github.com:octocat/Hello-World.git", "octocat", "Hello-World"));
    CHECK(parsesTo("github.com/octocat/Hello-World/", "octocat", "Hello-World"));
}

TEST(githubalarm, parse_repo_input_invalid)
{
    CHECK(!parseRepoInput(QString()).has_value());
    CHECK(!parseRepoInput(QStringLiteral("nurEinName")).has_value());
    CHECK(!parseRepoInput(QStringLiteral("https://github.com/octocat")).has_value());
}

TEST(githubalarm, repospec_json_and_props)
{
    RepoSpec r;
    r.id = 1;
    r.owner = QStringLiteral("octocat");
    r.repo = QStringLiteral("Hello-World");
    r.name = QStringLiteral("Demo");
    r.lastPushed = QStringLiteral("2024-01-01T00:00:00Z");

    CHECK_EQ(r.fullName(), QStringLiteral("octocat/Hello-World"));
    CHECK_EQ(r.display(), QStringLiteral("Demo"));

    // JSON-Roundtrip
    const RepoSpec back = RepoSpec::fromJson(r.toJson());
    CHECK_EQ(back.id, r.id);
    CHECK_EQ(back.owner, r.owner);
    CHECK_EQ(back.repo, r.repo);
    CHECK_EQ(back.name, r.name);
    CHECK_EQ(back.enabled, r.enabled);
    CHECK_EQ(back.lastPushed, r.lastPushed);

    // Ohne eigenen Namen faellt display() auf owner/repo zurueck.
    RepoSpec plain;
    plain.id = 2;
    plain.owner = QStringLiteral("a");
    plain.repo = QStringLiteral("b");
    CHECK_EQ(plain.display(), QStringLiteral("a/b"));
}
