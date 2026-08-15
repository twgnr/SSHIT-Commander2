// Bekannte Host-Keys verwalten (TOFU-Store ansehen und Eintraege entfernen).
#pragma once

#include "ncssh/core/hostkeys.hpp"

#include <QDialog>

class QTableWidget;

namespace ncssh::gui {

class KnownHostsDialog : public QDialog {
    Q_OBJECT
public:
    explicit KnownHostsDialog(core::HostKeyStore *store, QWidget *parent = nullptr);

private:
    void reload();
    void removeSelected();

    core::HostKeyStore *m_store;
    QTableWidget *m_table = nullptr;
};

} // namespace ncssh::gui
