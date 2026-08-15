// GitHub-Repo-Alarm: ueberwacht Repositories auf neue Pushes.
#pragma once

#include "ncssh/core/githubalarm.hpp"
#include "ncssh/gui/bridge.hpp"

#include <QDialog>
#include <QObject>
#include <vector>

class QTableWidget;
class QListWidget;
class QLineEdit;
class QLabel;
class QTimer;

namespace ncssh::gui {

// Fragt die ueberwachten Repos periodisch ab und meldet Aenderungen.
class GithubAlarmManager : public QObject {
    Q_OBJECT
public:
    explicit GithubAlarmManager(AsyncBridge *bridge, QObject *parent = nullptr);

    void reload();
    void checkNow();

signals:
    void repoChanged(const QString &fullName, const QString &pushedAt);

private:
    AsyncBridge *m_bridge;
    QTimer *m_timer;
    std::vector<core::RepoSpec> m_repos;
    bool m_busy = false;
};

class GithubAlarmDialog : public QDialog {
    Q_OBJECT
public:
    GithubAlarmDialog(GithubAlarmManager *manager, QWidget *parent = nullptr);

private:
    void reload();
    void addRepo();
    void removeRepo();
    void toggleRepo();
    void saveToken();

    GithubAlarmManager *m_manager;
    std::vector<core::RepoSpec> m_repos;

    QLineEdit *m_input = nullptr;
    QLineEdit *m_token = nullptr;
    QTableWidget *m_table = nullptr;
    QListWidget *m_events = nullptr;
    QLabel *m_status = nullptr;
};

} // namespace ncssh::gui
