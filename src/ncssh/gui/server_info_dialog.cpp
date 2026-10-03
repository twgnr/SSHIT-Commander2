#include "ncssh/gui/server_info_dialog.hpp"

#include "ncssh/core/i18n.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;

namespace {

QPlainTextEdit *monoView(QWidget *parent)
{
    auto *view = new QPlainTextEdit(parent);
    view->setReadOnly(true);
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    view->setFont(mono);
    return view;
}

QTableWidgetItem *item(const QString &text)
{
    auto *it = new QTableWidgetItem(text);
    it->setFlags(it->flags() & ~Qt::ItemIsEditable);
    return it;
}

QString orDash(const QString &text)
{
    return text.isEmpty() ? QStringLiteral("—") : text;
}

} // namespace

ServerInfoDialog::ServerInfoDialog(AsyncBridge *bridge, net::SSHSessionPtr session,
                                   std::function<void(const QString &)> openFile,
                                   QWidget *parent)
    : QDialog(parent), m_bridge(bridge), m_session(std::move(session)),
      m_openFile(std::move(openFile))
{
    setObjectName(QStringLiteral("ServerInfoDialog"));
    setWindowTitle(m_session ? _t("Server-Info — %1").arg(m_session->label())
                             : _t("Server-Info"));
    resize(900, 620);
    auto *layout = new QVBoxLayout(this);

    // Uebersicht
    auto *form = new QFormLayout();
    const auto addRow = [&](const QString &label) {
        auto *value = new QLabel(QStringLiteral("…"), this);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        value->setWordWrap(true);
        form->addRow(label, value);
        return value;
    };
    m_hostname = addRow(_t("Hostname:"));
    m_os = addRow(_t("System:"));
    m_kernel = addRow(_t("Kernel:"));
    m_uptime = addRow(_t("Laufzeit / Last:"));
    m_identity = addRow(_t("Angemeldet als:"));
    layout->addLayout(form);

    auto *tabs = new QTabWidget(this);
    layout->addWidget(tabs, 1);

    // Konfigurationsdateien
    auto *filesPage = new QWidget(tabs);
    auto *filesLayout = new QVBoxLayout(filesPage);
    m_fileFilter = new QLineEdit(filesPage);
    m_fileFilter->setPlaceholderText(_t("Filtern (Pfad oder Kategorie) …"));
    m_fileFilter->setClearButtonEnabled(true);
    connect(m_fileFilter, &QLineEdit::textChanged, this, &ServerInfoDialog::applyFileFilter);
    filesLayout->addWidget(m_fileFilter);
    m_files = new QTableWidget(0, 3, filesPage);
    m_files->setObjectName(QStringLiteral("ServerInfoFiles"));
    m_files->setHorizontalHeaderLabels({_t("Kategorie"), _t("Pfad"), _t("Zugriff")});
    m_files->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_files->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_files->setSelectionMode(QAbstractItemView::SingleSelection);
    m_files->verticalHeader()->setVisible(false);
    m_files->setSortingEnabled(true);
    connect(m_files, &QTableWidget::cellDoubleClicked, this,
            [this](int, int) { openSelectedFile(); });
    filesLayout->addWidget(m_files, 1);
    auto *fileButtons = new QHBoxLayout();
    auto *hint = new QLabel(_t("Doppelklick öffnet die Datei im Editor."), filesPage);
    hint->setObjectName(QStringLiteral("Muted"));
    m_openBtn = new QPushButton(_t("Öffnen / Bearbeiten"), filesPage);
    connect(m_openBtn, &QPushButton::clicked, this, &ServerInfoDialog::openSelectedFile);
    auto *copyBtn = new QPushButton(_t("Pfad kopieren"), filesPage);
    connect(copyBtn, &QPushButton::clicked, this, [this] {
        const int row = m_files->currentRow();
        if (row >= 0 && m_files->item(row, 1))
            QApplication::clipboard()->setText(m_files->item(row, 1)->text());
    });
    fileButtons->addWidget(hint, 1);
    fileButtons->addWidget(copyBtn);
    fileButtons->addWidget(m_openBtn);
    filesLayout->addLayout(fileButtons);
    tabs->addTab(filesPage, _t("Konfigurationsdateien"));

    // Benutzer
    auto *usersPage = new QWidget(tabs);
    auto *usersLayout = new QVBoxLayout(usersPage);
    m_systemUsers = new QCheckBox(_t("Systemkonten anzeigen"), usersPage);
    connect(m_systemUsers, &QCheckBox::toggled, this, &ServerInfoDialog::fillUsers);
    usersLayout->addWidget(m_systemUsers);
    m_users = new QTableWidget(0, 6, usersPage);
    m_users->setObjectName(QStringLiteral("ServerInfoUsers"));
    m_users->setHorizontalHeaderLabels(
        {_t("Benutzer"), _t("UID"), _t("Name"), _t("Home"), _t("Shell"), _t("Admin")});
    m_users->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_users->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_users->verticalHeader()->setVisible(false);
    m_users->setSortingEnabled(true);
    usersLayout->addWidget(m_users, 1);
    tabs->addTab(usersPage, _t("Benutzer"));

    m_ports = monoView(tabs);
    tabs->addTab(m_ports, _t("Ports"));
    m_services = monoView(tabs);
    tabs->addTab(m_services, _t("Dienste"));
    m_storage = monoView(tabs);
    tabs->addTab(m_storage, _t("Speicher"));

    auto *buttons = new QHBoxLayout();
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("Muted"));
    m_reloadBtn = new QPushButton(_t("Aktualisieren"), this);
    connect(m_reloadBtn, &QPushButton::clicked, this, &ServerInfoDialog::reload);
    auto *closeBtn = new QPushButton(_t("Schließen"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(m_status, 1);
    buttons->addWidget(m_reloadBtn);
    buttons->addWidget(closeBtn);
    layout->addLayout(buttons);

    if (m_session)
        reload();
}

void ServerInfoDialog::reload()
{
    if (!m_session)
        return;
    m_reloadBtn->setEnabled(false);
    m_status->setText(_t("Lese Serverdaten …"));
    net::SSHSessionPtr session = m_session;
    const QByteArray script = core::serverInfoScript().toUtf8();
    m_bridge->run<QString>(
        [session, script] {
            // "sh -s": das Skript kommt ueber stdin — unabhaengig von der
            // Login-Shell des Benutzers (fish, zsh …) und ohne Quoting.
            const net::ExecResult result = session->exec(QStringLiteral("sh -s"), script);
            return QString::fromUtf8(result.out);
        },
        [this](const QString &output) {
            m_reloadBtn->setEnabled(true);
            const core::ServerInfo info = core::parseServerInfo(output);
            if (info.hostname.isEmpty() && info.users.empty()) {
                m_status->setText(_t("Keine Daten — Server-Info gibt es nur für Linux/Unix-Server."));
                return;
            }
            showInfo(info);
        },
        [this](const QString &err) {
            m_reloadBtn->setEnabled(true);
            m_status->setText(_t("Fehler: %1").arg(err));
        },
        this);
}

void ServerInfoDialog::showInfo(const core::ServerInfo &info)
{
    m_info = info;
    m_hostname->setText(orDash(info.hostname));
    m_os->setText(orDash(info.os));
    m_kernel->setText(orDash(info.kernel));
    m_uptime->setText(orDash(info.uptime));
    m_identity->setText(orDash(info.identity));

    m_files->setSortingEnabled(false);
    m_files->setRowCount(0);
    for (const core::ServerConfigFile &f : info.files) {
        const int row = m_files->rowCount();
        m_files->insertRow(row);
        m_files->setItem(row, 0, item(f.category));
        m_files->setItem(row, 1, item(f.path));
        const QString access = f.writable   ? _t("lesen & schreiben")
                               : f.readable ? _t("nur lesen")
                                            : _t("kein Zugriff (sudo nötig)");
        m_files->setItem(row, 2, item(access));
    }
    m_files->setSortingEnabled(true);
    m_files->sortByColumn(0, Qt::AscendingOrder);
    m_files->resizeColumnToContents(0);
    applyFileFilter();

    fillUsers();
    m_ports->setPlainText(info.ports.isEmpty() ? _t("(keine Angaben — ss/netstat fehlt?)")
                                               : info.ports);
    m_services->setPlainText(info.services.isEmpty() ? _t("(keine Angaben — kein systemd?)")
                                                     : info.services);
    m_storage->setPlainText(info.disk + QStringLiteral("\n\n") + info.memory);
    m_status->setText(_t("%1 Konfigurationsdateien, %2 Benutzer")
                          .arg(info.files.size())
                          .arg(info.users.size()));
}

void ServerInfoDialog::fillUsers()
{
    // Standard: echte Konten (root, UID >= 1000, Admins) — die vielen
    // Dienstkonten (www-data, daemon …) nur auf Wunsch.
    const bool all = m_systemUsers->isChecked();
    m_users->setSortingEnabled(false);
    m_users->setRowCount(0);
    for (const core::ServerUser &u : m_info.users) {
        const bool regular = u.uid == 0 || u.admin || (u.uid >= 1000 && u.uid != 65534);
        if (!all && !regular)
            continue;
        const int row = m_users->rowCount();
        m_users->insertRow(row);
        m_users->setItem(row, 0, item(u.name));
        auto *uid = new QTableWidgetItem;
        uid->setData(Qt::DisplayRole, u.uid);   // numerisch sortieren
        uid->setFlags(uid->flags() & ~Qt::ItemIsEditable);
        m_users->setItem(row, 1, uid);
        m_users->setItem(row, 2, item(u.gecos));
        m_users->setItem(row, 3, item(u.home));
        m_users->setItem(row, 4, item(u.login ? u.shell : u.shell + _t(" (keine Anmeldung)")));
        m_users->setItem(row, 5, item(u.admin ? QStringLiteral("✓") : QString()));
    }
    m_users->setSortingEnabled(true);
    m_users->resizeColumnsToContents();
}

void ServerInfoDialog::applyFileFilter()
{
    const QString needle = m_fileFilter->text().trimmed();
    for (int row = 0; row < m_files->rowCount(); ++row) {
        const bool match = needle.isEmpty()
                           || m_files->item(row, 0)->text().contains(needle, Qt::CaseInsensitive)
                           || m_files->item(row, 1)->text().contains(needle, Qt::CaseInsensitive);
        m_files->setRowHidden(row, !match);
    }
}

void ServerInfoDialog::openSelectedFile()
{
    const int row = m_files->currentRow();
    if (row < 0 || !m_files->item(row, 1) || !m_openFile)
        return;
    m_openFile(m_files->item(row, 1)->text());
}

} // namespace ncssh::gui
