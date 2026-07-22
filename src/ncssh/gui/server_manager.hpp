// Server-Profile verwalten und eine Verbindung auswaehlen.
// (Port von gui/server_manager.py, funktional zusammengefasst)
#pragma once

#include "ncssh/core/models.hpp"
#include "ncssh/core/profiles.hpp"

#include <QDialog>
#include <optional>

class QListWidget;
class QLineEdit;
class QComboBox;
class QSpinBox;
class QCheckBox;

namespace ncssh::gui {

class ServerManagerDialog : public QDialog {
    Q_OBJECT
public:
    explicit ServerManagerDialog(QWidget *parent = nullptr);

    // Das zum Verbinden gewaehlte Profil (nach Accepted).
    std::optional<core::ServerProfile> chosen() const { return m_chosen; }

private:
    void reload();
    void loadIntoForm(const core::ServerProfile &p);
    core::ServerProfile formToProfile() const;
    void onSave();
    void onDelete();
    void onConnect();
    void onImport();

    core::ProfileStore m_store;
    std::optional<core::ServerProfile> m_chosen;

    QListWidget *m_list = nullptr;
    QLineEdit *m_name = nullptr;
    QLineEdit *m_host = nullptr;
    QSpinBox *m_port = nullptr;
    QLineEdit *m_user = nullptr;
    QComboBox *m_auth = nullptr;
    QLineEdit *m_keyPath = nullptr;
    QLineEdit *m_password = nullptr;
    QComboBox *m_policy = nullptr;
    QLineEdit *m_startPath = nullptr;
};

} // namespace ncssh::gui
