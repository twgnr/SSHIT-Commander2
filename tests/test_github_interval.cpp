// GitHub-Repo-Alarm: das Pruef-Intervall kommt aus den Einstellungen
// (Allgemein -> "GitHub-Alarm: Repos prüfen alle") und gilt nach reload().
#include "tests/harness.hpp"

#include "ncssh/core/settings.hpp"
#include "ncssh/gui/githubalarm_dialog.hpp"

#include <QTemporaryDir>

using namespace ncssh;

TEST(github_interval, interval_comes_from_settings)
{
    const QByteArray oldAppData = qgetenv("APPDATA");
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    qputenv("APPDATA", tmp.path().toUtf8());
    {
        CHECK_EQ(gui::GithubAlarmManager::intervalSeconds(), 900);   // Standard 15 min
        core::setSetting(QStringLiteral("github_alarm_interval"), 5 * 60);
        CHECK_EQ(gui::GithubAlarmManager::intervalSeconds(), 300);
        // Zu kurze Werte werden auf 1 Minute angehoben (GitHub-Ratenlimit).
        core::setSetting(QStringLiteral("github_alarm_interval"), 10);
        CHECK_EQ(gui::GithubAlarmManager::intervalSeconds(), 60);
    }
    qputenv("APPDATA", oldAppData);
}
