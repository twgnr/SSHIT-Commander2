#include "ncssh/gui/githubalarm_dialog.hpp"

#include "ncssh/core/gitstatus.hpp"
#include "ncssh/core/settings.hpp"

#include "ncssh/core/i18n.hpp"

#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
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
    reload();
}

int GithubAlarmManager::intervalSeconds()
{
    // Einstellungen -> Allgemein (github_alarm_interval, Sekunden; Standard
    // 900 = 15 min, min. 60).
    return qMax(60, core::getSettingInt(QStringLiteral("github_alarm_interval"), 900));
}

void GithubAlarmManager::reload()
{
    // Intervall jedes Mal neu lesen: eine Aenderung in den Einstellungen gilt
    // sofort (setInterval startet einen laufenden Timer neu).
    m_timer->setInterval(intervalSeconds() * 1000);
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
                // Frisch laden: Panes tragen zwischendurch gelernte lokale
                // Ordner ein — die alte Kopie wuerde sie sonst ueberschreiben.
                m_repos = core::loadRepos();
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
        [this](const QString &) { m_busy = false; }, this);
}

// ---------------------------------------------------------------------------

GithubAlarmDialog::GithubAlarmDialog(GithubAlarmManager *manager, QWidget *parent)
    : QDialog(parent), m_manager(manager)
{
    setWindowTitle(_t("GitHub Repo Alarm"));
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
    m_token->setPlaceholderText(
        _t("GitHub-Token (für private Repos / höheres Limit):"));
    if (!core::githubGetToken().isEmpty())
        m_token->setText(core::githubGetToken());
    auto *tokenBtn = new QPushButton(_t("Token speichern"), this);
    connect(tokenBtn, &QPushButton::clicked, this, &GithubAlarmDialog::saveToken);
    tokenRow->addWidget(m_token, 1);
    tokenRow->addWidget(tokenBtn);
    form->addRow(_t("GitHub-Token"), tokenRow);
    layout->addLayout(form);

    layout->addWidget(new QLabel(
        _t("Überwachte Repositories — Häkchen in „Aktiv“ schaltet die Überwachung an/aus:"),
        this));
    m_table = new QTableWidget(0, 4, this);
    m_table->setHorizontalHeaderLabels(
        {_t("Repository"), _t("Lokaler Ordner"), _t("Letzter Push"), _t("Aktiv")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // Spalte "Aktiv": Haekchen schaltet die Ueberwachung des Repos an/aus.
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (!item || item->column() != 3)
            return;
        const int id = item->data(Qt::UserRole).toInt();
        m_repos = core::loadRepos();   // zwischendurch gelernte Ordner erhalten
        for (RepoSpec &r : m_repos) {
            if (r.id == id)
                r.enabled = item->checkState() == Qt::Checked;
        }
        core::saveRepos(m_repos);
        if (m_manager)
            m_manager->reload();
    });
    layout->addWidget(m_table, 2);

    layout->addWidget(new QLabel(_t("Meldungen:"), this));
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
    // Lokaler Klon: Panes markieren bei Aenderungen darin alle Ordner darueber.
    auto *localBtn = new QPushButton(_t("Lokaler Ordner"), this);
    localBtn->setToolTip(_t("Lokalen Klon zuordnen — Panes markieren dann bei Änderungen "
                            "darin auch alle übergeordneten Ordner farbig"));
    auto *localMenu = new QMenu(localBtn);
    localMenu->addAction(_t("Ordner wählen …"), this, &GithubAlarmDialog::chooseLocalPath);
    localMenu->addAction(_t("Zuordnung entfernen"), this, &GithubAlarmDialog::clearLocalPath);
    localBtn->setMenu(localMenu);
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
    buttons->addWidget(localBtn);
    buttons->addStretch(1);
    buttons->addWidget(closeBtn);
    layout->addLayout(buttons);

    reload();
}

void GithubAlarmDialog::reload()
{
    m_repos = core::loadRepos();
    const QSignalBlocker blocker(m_table);   // Neuaufbau ist keine Nutzer-Aenderung
    m_table->setRowCount(0);
    int row = 0;
    for (const RepoSpec &r : m_repos) {
        m_table->insertRow(row);
        auto *nameItem = new QTableWidgetItem(r.display());
        nameItem->setData(Qt::UserRole, r.id);
        m_table->setItem(row, 0, nameItem);
        m_table->setItem(row, 1, new QTableWidgetItem(r.localPath));
        m_table->setItem(row, 2, new QTableWidgetItem(r.lastPushed));
        auto *active = new QTableWidgetItem();
        active->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        active->setCheckState(r.enabled ? Qt::Checked : Qt::Unchecked);
        active->setData(Qt::UserRole, r.id);
        m_table->setItem(row, 3, active);
        ++row;
    }
    m_status->setText(QStringLiteral("%1 Repository(s)").arg(m_repos.size()));
}

void GithubAlarmDialog::addRepo()
{
    const auto parsed = core::parseRepoInput(m_input->text());
    if (!parsed) {
        QMessageBox::warning(this, _t("Fehler"),
                             _t("Bitte ein gültiges Repo angeben (owner/repo)."));
        return;
    }
    m_repos = core::loadRepos();   // zwischendurch gelernte Ordner nicht verlieren
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

core::RepoSpec *GithubAlarmDialog::repoAtRow(int row)
{
    const QTableWidgetItem *item = row >= 0 ? m_table->item(row, 0) : nullptr;
    if (!item)
        return nullptr;
    const int id = item->data(Qt::UserRole).toInt();
    m_repos = core::loadRepos();
    for (RepoSpec &r : m_repos)
        if (r.id == id)
            return &r;
    return nullptr;
}

void GithubAlarmDialog::toggleRepo()
{
    RepoSpec *repo = repoAtRow(m_table->currentRow());
    if (!repo)
        return;
    repo->enabled = !repo->enabled;
    core::saveRepos(m_repos);
    m_manager->reload();
    reload();
}

void GithubAlarmDialog::removeRepo()
{
    RepoSpec *repo = repoAtRow(m_table->currentRow());
    if (!repo)
        return;
    m_repos.erase(m_repos.begin() + (repo - m_repos.data()));
    core::saveRepos(m_repos);
    m_manager->reload();
    reload();
}

void GithubAlarmDialog::chooseLocalPath()
{
    if (!repoAtRow(m_table->currentRow())) {
        m_status->setText(_t("Bitte zuerst ein Repository in der Liste wählen."));
        return;
    }
    const int row = m_table->currentRow();
    const QString start = repoAtRow(row)->localPath;
    const QString dir = QFileDialog::getExistingDirectory(
        this, _t("Lokalen Klon wählen"), start.isEmpty() ? QDir::homePath() : start);
    if (dir.isEmpty())
        return;
    // Auf die Wurzel des Klons abbilden (auch wenn ein Unterordner gewaehlt wurde).
    const core::GitRepoInfo info = core::gitRepoInfo(dir);
    if (info.root.isEmpty()) {
        QMessageBox::warning(this, _t("Kein Git-Repository"),
                             _t("„%1“ ist kein Git-Repository.").arg(QDir::toNativeSeparators(dir)));
        return;
    }
    RepoSpec *repo = repoAtRow(row);   // nach dem modalen Dialog frisch laden
    if (!repo)
        return;
    repo->localPath = info.root;
    core::saveRepos(m_repos);
    m_manager->reload();
    reload();
}

void GithubAlarmDialog::clearLocalPath()
{
    RepoSpec *repo = repoAtRow(m_table->currentRow());
    if (!repo || repo->localPath.isEmpty())
        return;
    repo->localPath.clear();
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
