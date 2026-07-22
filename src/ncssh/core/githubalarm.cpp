#include "ncssh/core/githubalarm.hpp"

#include "ncssh/core/secrets.hpp"
#include "ncssh/core/settings.hpp"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace ncssh::core {

static const QString kApi = QStringLiteral("https://api.github.com");

QJsonObject RepoSpec::toJson() const
{
    return QJsonObject{
        {QStringLiteral("id"), id},
        {QStringLiteral("owner"), owner},
        {QStringLiteral("repo"), repo},
        {QStringLiteral("name"), name},
        {QStringLiteral("enabled"), enabled},
        {QStringLiteral("last_pushed"), lastPushed},
    };
}

RepoSpec RepoSpec::fromJson(const QJsonObject &d)
{
    RepoSpec r;
    r.id = d.value(QStringLiteral("id")).toInt(0);
    r.owner = d.value(QStringLiteral("owner")).toString();
    r.repo = d.value(QStringLiteral("repo")).toString();
    r.name = d.value(QStringLiteral("name")).toString();
    r.enabled = d.value(QStringLiteral("enabled")).toBool(true);
    r.lastPushed = d.value(QStringLiteral("last_pushed")).toString();
    return r;
}

std::optional<std::pair<QString, QString>> parseRepoInput(const QString &text)
{
    QString t = text.trimmed();
    static const QStringList prefixes = {
        QStringLiteral("https://github.com/"), QStringLiteral("http://github.com/"),
        QStringLiteral("git@github.com:"), QStringLiteral("ssh://git@github.com/"),
        QStringLiteral("github.com/"),
    };
    for (const QString &pre : prefixes) {
        if (t.toLower().startsWith(pre)) {
            t = t.mid(pre.length());
            break;
        }
    }
    if (t.endsWith(QLatin1String(".git")))
        t.chop(4);
    while (t.startsWith(QLatin1Char('/'))) t.remove(0, 1);
    while (t.endsWith(QLatin1Char('/'))) t.chop(1);
    const QStringList parts = t.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.size() >= 2)
        return std::make_pair(parts[0], parts[1]);
    return std::nullopt;
}

std::pair<QString, QString> fetchPushedAt(const QString &owner, const QString &repo,
                                          const QString &token, int timeoutMs)
{
    QNetworkAccessManager manager;
    manager.setTransferTimeout(timeoutMs);
    QNetworkRequest req(QUrl(QStringLiteral("%1/repos/%2/%3").arg(kApi, owner, repo)));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("User-Agent", "SSHIT-Commander");
    if (!token.isEmpty())
        req.setRawHeader("Authorization", "Bearer " + token.toUtf8());

    QNetworkReply *reply = manager.get(req);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    reply->deleteLater();

    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 400)
        return {{}, QStringLiteral("HTTP %1").arg(status)};
    if (reply->error() != QNetworkReply::NoError)
        return {{}, reply->errorString()};
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isObject())
        return {{}, QStringLiteral("Ungültige Antwort")};
    return {doc.object().value(QStringLiteral("pushed_at")).toString(), {}};
}

// --- Token (verschluesselt im OS-Keyring) -----------------------------------

QString githubGetToken()
{
    const auto tok = getSecret(QStringLiteral("github"), QStringLiteral("token"));
    if (tok && !tok->isEmpty())
        return *tok;
    // Migration: frueher evtl. im Klartext gespeicherten Token uebernehmen + loeschen.
    const QString legacy = getSettingString(QStringLiteral("github_token"));
    if (!legacy.isEmpty()) {
        githubSetToken(legacy);
        setSetting(QStringLiteral("github_token"), QString());
        return legacy;
    }
    return {};
}

void githubSetToken(const QString &value)
{
    setSecret(QStringLiteral("github"), QStringLiteral("token"), value);
}

// --- Persistenz -------------------------------------------------------------

std::vector<RepoSpec> loadRepos()
{
    std::vector<RepoSpec> out;
    const QVariantList list = getSetting(QStringLiteral("github_alarms")).toList();
    for (const QVariant &v : list)
        out.push_back(RepoSpec::fromJson(QJsonObject::fromVariantMap(v.toMap())));
    return out;
}

void saveRepos(const std::vector<RepoSpec> &repos)
{
    QJsonArray arr;
    for (const auto &r : repos)
        arr.append(r.toJson());
    setSetting(QStringLiteral("github_alarms"), arr);
}

} // namespace ncssh::core
