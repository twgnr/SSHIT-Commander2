#include "ncssh/gui/githubalarm_dialog.hpp"

#include "ncssh/core/i18n.hpp"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace ncssh::gui {

using core::_t;
using core::RepoSpec;

// ---------------------------------------------------------------------------

GithubAlarmManager::GithubAlarmManager(AsyncBridge *bridge, QObject *parent)
    : QObject(parent), m_bridge(bridge), m_timer(new QTimer(this))
{
    connect(m_timer, &QTimer::timeout, this, &GithubAlarmManager::checkNow);
    m_timer->setInterval(5 * 60 * 1000);  // alle 5 Minuten
    reload();
}

void GithubAlarmManager::reload()
{
    m_repos = core::loadRepos();
    const bool anyEnabled = std::any_of(m_repos.begin(), m_repos.end(),
                                        [](const RepoSpec &r) { return r.enabled; });
    if (anyEnabled)
        m_timer->start();
    else
        m_timer->stop();
}

void GithubAlarmManager::checkNow()
{
    if (m_busy || m_repos.empty())
        return;
    m_busy = true;
    const std::vector<RepoSpec> repos = m_repos;
    const QString token = core::githubGetToken();

    m_bridge->run<std::vector<RepoSpec>>(
        [repos, token]() -> std::vector<RepoSpec> {
            std::vector<RepoSpec> changed;
            for (RepoSpec r : repos) {
                if (!r.enabled)
                    continue;
                const auto [pushedAt, error] = core::fetchPushedAt(r.owner, r.repo, token);
                if (pushedAt.isEmpty() || pushedAt == r.lastPushed)
                    continue;
                r.lastPushed = pushedAt;
                changed.push_back(r);
            }
            return changed;
        },
        [this](const std::vector<RepoSpec> &changed) {
            if (!changed.empty()) {
                // Neuen Stand merken, damit nur einmal gemeldet wird.
                for (const RepoSpec &c : changed) {
                    for (RepoSpec &r : m_repos) {
                        if (r.id == c.id)
                            r.lastPushed = c.lastPushed;
                    }
                    emit repoChanged(c.fullName(), c.lastPushed);
                }
                core::saveRepos(m_repos);
            }
            m_busy = false;
        },
        [this](const QString &) { m_busy = false; });
}

// ---------------------------------------------------------------------------

GithubAlarmDialog::GithubAlarmDialog(GithubAlarmManager *manager, QWidget *parent)
    : QDialog(parent), m_manager(manager)
{
    setWindowTitle(_t("GitHub-Alarm"));
    resize(820, 560);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout();

    auto *addRow = new QHBoxLayout();
    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(QStringLiteral("owner/repo oder https://github.com/owner/repo"));
    connect(m_input, &QLineEdit::returnPressed, this, &GithubAlarmDialog::addRepo);
    auto *addBtn = new QPushButton(_t("Hinzufügen"), this);
    connect(addBtn, &QPushButton::clicked, this, &GithubAlarmDialog::addRepo);
    addRow->addWidget(m_input, 1);
    addRow->addWidget(addBtn);
    form->addRow(_t("Repository"), addRow);

    auto *tokenRow = new QHBoxLayout();
    m_token = new QLineEdit(this);
    m_token->setEchoMode(QLineEdit::Password);
    m_token->setPlaceholderText(_t("optional — erhöht das API-Limit"));
    if (!core::githubGetToken().isEmpty())
        m_token->setText(core::githubGetToken());
    auto *tokenBtn = new QPushButton(_t("Token speichern"), this);
    connect(tokenBtn, &QPushButton::clicked, this, &GithubAlarmDialog::saveToken);
    tokenRow->addWidget(m_token, 1);
    tokenRow->addWidget(tokenBtn);
    form->addRow(_t("GitHub-Token"), tokenRow);
    layout->addLayout(form);

    m_table = new QTableWidget(0, 3, this);
    m_table->setHorizontalHeaderLabels({_t("Repository"), _t("Letzter Push"), _t("Aktiv")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_table, 2);

    layout->addWidget(new QLabel(_t("Ereignisse"), this));
    m_events = new QListWidget(this);
    layout->addWidget(m_events, 1);
    connect(manager, &GithubAlarmManager::repoChanged, this,
            [this](const QString &fullName, const QString &pushedAt) {
                m_events->insertItem(0, QStringLiteral("%1 — neue Daten (%2)")
                                            .arg(fullName, pushedAt));
                reload();
            });

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("Muted"));
    layout->addWidget(m_status);

    auto *buttons = new QHBoxLayout();
    auto *checkBtn = new QPushButton(_t("Jetzt prüfen"), this);
    auto *toggleBtn = new QPushButton(_t("Aktiv/Inaktiv"), this);
    auto *removeBtn = new QPushButton(_t("Entfernen"), this);
    auto *closeBtn = new QPushButton(_t("Schließen"), this);
    closeBtn->setDefault(true);
    connect(checkBtn, &QPushButton::clicked, this, [this] {
        m_status->setText(_t("Prüfe …"));
        m_manager->checkNow();
    });
    connect(toggleBtn, &QPushButton::clicked, this, &GithubAlarmDialog::toggleRepo);
    connect(removeBtn, &QPushButton::clicked, this, &GithubAlarmDialog::removeRepo);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(checkBtn);
    buttons->addWidget(toggleBtn);
    buttons->addWidget(removeBtn);
    buttons->addStretch(1);
    buttons->addWidget(closeBtn);
    layout->addLayout(buttons);

    reload();
}

void GithubAlarmDialog::reload()
{
    m_repos = core::loadRepos();
    m_table->setRowCount(0);
    int row = 0;
    for (const RepoSpec &r : m_repos) {
        m_table->insertRow(row);
        m_table->setItem(row, 0, new QTableWidgetItem(r.display()));
        m_table->setItem(row, 1, new QTableWidgetItem(r.lastPushed));
        m_table->setItem(row, 2, new QTableWidgetItem(r.enabled ? QStringLiteral("✓")
                                                                : QString()));
        ++row;
    }
    m_status->setText(QStringLiteral("%1 Repository(s)").arg(m_repos.size()));
}

void GithubAlarmDialog::addRepo()
{
    const auto parsed = core::parseRepoInput(m_input->text());
    if (!parsed) {
        QMessageBox::warning(this, _t("Fehler"),
                             _t("Eingabe nicht erkannt. Erwartet: owner/repo oder GitHub-URL."));
        return;
    }
    RepoSpec spec;
    int maxId = 0;
    for (const RepoSpec &r : m_repos)
        maxId = qMax(maxId, r.id);
    spec.id = maxId + 1;
    spec.owner = parsed->first;
    spec.repo = parsed->second;
    m_repos.push_back(spec);
    core::saveRepos(m_repos);
    m_manager->reload();
    m_input->clear();
    reload();
}

void GithubAlarmDialog::toggleRepo()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= int(m_repos.size()))
        return;
    m_repos[row].enabled = !m_repos[row].enabled;
    core::saveRepos(m_repos);
    m_manager->reload();
    reload();
}

void GithubAlarmDialog::removeRepo()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= int(m_repos.size()))
        return;
    m_repos.erase(m_repos.begin() + row);
    core::saveRepos(m_repos);
    m_manager->reload();
    reload();
}

void GithubAlarmDialog::saveToken()
{
    core::githubSetToken(m_token->text().trimmed());
    m_status->setText(_t("Token im Schlüsselbund gespeichert."));
}

} // namespace ncssh::gui
