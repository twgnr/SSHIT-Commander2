// GitHub-Repo-Alarm: ueberwacht Repositories auf neue Daten (Bordmittel).
//
// Pro Repo wird der Zeitstempel des letzten Pushs (pushed_at der GitHub-API)
// abgefragt; aendert er sich, gibt es "neue Daten". parseRepoInput und die
// Datenmodelle sind rein/testbar.
#pragma once

#include <QJsonObject>
#include <QString>
#include <optional>
#include <utility>
#include <vector>

namespace ncssh::core {

struct RepoSpec {
    int id = 0;
    QString owner;
    QString repo;
    QString name;
    bool enabled = true;
    QString lastPushed;  // gemerkter Stand (pushed_at), fuer Aenderungserkennung

    QString fullName() const { return owner + QLatin1Char('/') + repo; }
    QString display() const { return name.isEmpty() ? fullName() : name; }

    QJsonObject toJson() const;
    static RepoSpec fromJson(const QJsonObject &d);
};

// "owner/repo" aus diversen Eingaben: "owner/repo", GitHub-URL,
// "git@github.com:owner/repo.git". nullopt, wenn nicht erkennbar.
std::optional<std::pair<QString, QString>> parseRepoInput(const QString &text);

// Holt pushed_at des Repos -> (zeitstempel, fehler). Leerer Zeitstempel bei
// Fehler (Fehlertext gesetzt). Blockierend — im Worker aufrufen.
std::pair<QString, QString> fetchPushedAt(const QString &owner, const QString &repo,
                                          const QString &token = {}, int timeoutMs = 8000);

// GitHub-Token aus dem OS-Keyring (nie im Klartext in settings.json).
QString githubGetToken();
void githubSetToken(const QString &value);

std::vector<RepoSpec> loadRepos();
void saveRepos(const std::vector<RepoSpec> &repos);

} // namespace ncssh::core
