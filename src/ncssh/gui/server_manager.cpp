#include "ncssh/gui/server_manager.hpp"

#include "ncssh/core/i18n.hpp"
#include "ncssh/core/importers.hpp"
#include "ncssh/core/secrets.hpp"
#include "ncssh/gui/key_dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include "ncssh/gui/file_dialogs.hpp"
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;
using core::ServerProfile;

ServerManagerDialog::ServerManagerDialog(AsyncBridge *bridge, QWidget *parent)
    : QDialog(parent), m_bridge(bridge)
{
    setWindowTitle(_t("Server-Profile"));
    resize(720, 480);
    m_store.load();

    auto *root = new QHBoxLayout(this);

    // Linke Spalte: Liste
    auto *left = new QVBoxLayout();
    m_list = new QListWidget(this);
    connect(m_list, &QListWidget::currentTextChanged, this, [this](const QString &name) {
        if (const auto p = m_store.get(name))
            loadIntoForm(*p);
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this, &ServerManagerDialog::onConnect);
    left->addWidget(new QLabel(_t("Profile"), this));
    left->addWidget(m_list, 1);
    auto *importBtn = new QPushButton(_t("Aus PuTTY/WinSCP importieren"), this);
    connect(importBtn, &QPushButton::clicked, this, &ServerManagerDialog::onImport);
    left->addWidget(importBtn);
    root->addLayout(left, 1);

    // Rechte Spalte: Formular
    auto *form = new QFormLayout();
    m_name = new QLineEdit(this);
    m_host = new QLineEdit(this);
    m_port = new QSpinBox(this);
    m_port->setRange(1, 65535);
    m_port->setValue(22);
    m_user = new QLineEdit(this);
    m_auth = new QComboBox(this);
    m_auth->addItems({QStringLiteral("key"), QStringLiteral("password"), QStringLiteral("agent")});
    m_keyPath = new QLineEdit(this);
    auto *keyRow = new QHBoxLayout();
    keyRow->addWidget(m_keyPath, 1);
    auto *browse = new QPushButton(QStringLiteral("…"), this);
    browse->setFixedWidth(34);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString f = getOpenFileName(this, _t("SSH-Schlüssel wählen"));
        if (!f.isEmpty())
            m_keyPath->setText(f);
    });
    keyRow->addWidget(browse);
    // Schluessel erzeugen/konvertieren; danach den Pfad ins Profil uebernehmen.
    auto *keyToolsBtn = new QPushButton(QStringLiteral("🔑"), this);
    keyToolsBtn->setFixedWidth(34);
    keyToolsBtn->setToolTip(_t("SSH-Schlüssel erzeugen oder konvertieren"));
    connect(keyToolsBtn, &QPushButton::clicked, this, [this] {
        KeyDialog dlg(m_bridge, this);
        dlg.exec();
        if (!dlg.savedKeyPath().isEmpty()) {
            m_keyPath->setText(dlg.savedKeyPath());
            m_auth->setCurrentText(QStringLiteral("key"));
        }
    });
    keyRow->addWidget(keyToolsBtn);
    m_password = new QLineEdit(this);
    m_password->setEchoMode(QLineEdit::Password);
    m_policy = new QComboBox(this);
    m_policy->addItems({QStringLiteral("accept-new"), QStringLiteral("strict"), QStringLiteral("ignore")});
    m_startPath = new QLineEdit(this);
    m_startPath->setText(QStringLiteral("."));

    form->addRow(_t("Name"), m_name);
    form->addRow(_t("Host"), m_host);
    form->addRow(_t("Port"), m_port);
    form->addRow(_t("Benutzer"), m_user);
    form->addRow(_t("Auth-Methode"), m_auth);
    form->addRow(_t("Schlüssel"), keyRow);
    form->addRow(_t("Passwort"), m_password);
    form->addRow(_t("Host-Key-Richtlinie"), m_policy);
    form->addRow(_t("Startverzeichnis"), m_startPath);

    auto *right = new QVBoxLayout();
    right->addLayout(form);
    right->addStretch(1);
    auto *btnRow = new QHBoxLayout();
    auto *saveBtn = new QPushButton(_t("Speichern"), this);
    auto *delBtn = new QPushButton(_t("Löschen"), this);
    auto *connectBtn = new QPushButton(_t("Verbinden"), this);
    connectBtn->setDefault(true);
    connect(saveBtn, &QPushButton::clicked, this, &ServerManagerDialog::onSave);
    connect(delBtn, &QPushButton::clicked, this, &ServerManagerDialog::onDelete);
    connect(connectBtn, &QPushButton::clicked, this, &ServerManagerDialog::onConnect);
    btnRow->addWidget(saveBtn);
    btnRow->addWidget(delBtn);
    btnRow->addStretch(1);
    btnRow->addWidget(connectBtn);
    right->addLayout(btnRow);
    root->addLayout(right, 2);

    reload();
}

void ServerManagerDialog::reload()
{
    m_list->clear();
    for (const auto &p : m_store.profiles())
        m_list->addItem(p.name);
}

void ServerManagerDialog::loadIntoForm(const ServerProfile &p)
{
    m_name->setText(p.name);
    m_host->setText(p.host);
    m_port->setValue(p.port);
    m_user->setText(p.username);
    m_auth->setCurrentText(p.authMethod);
    m_keyPath->setText(p.keyPath);
    m_policy->setCurrentText(p.knownHostsPolicy);
    m_startPath->setText(p.startPath);
    m_password->clear();
}

ServerProfile ServerManagerDialog::formToProfile() const
{
    ServerProfile p;
    p.name = m_name->text().trimmed();
    p.host = m_host->text().trimmed();
    p.port = m_port->value();
    p.username = m_user->text().trimmed();
    p.authMethod = m_auth->currentText();
    p.keyPath = m_keyPath->text().trimmed();
    p.password = m_password->text();
    p.knownHostsPolicy = m_policy->currentText();
    p.startPath = m_startPath->text().trimmed().isEmpty() ? QStringLiteral(".")
                                                          : m_startPath->text().trimmed();
    return p;
}

void ServerManagerDialog::onSave()
{
    ServerProfile p = formToProfile();
    if (p.name.isEmpty() || p.host.isEmpty()) {
        QMessageBox::warning(this, _t("Fehler"), _t("Name und Host sind erforderlich."));
        return;
    }
    m_store.upsert(p);
    m_store.save();
    reload();
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->text() == p.name) {
            m_list->setCurrentRow(i);
            break;
        }
    }
}

void ServerManagerDialog::onDelete()
{
    if (!m_list->currentItem())
        return;
    const QString name = m_list->currentItem()->text();
    if (QMessageBox::question(this, _t("Löschen"),
                              QStringLiteral("Profil \"%1\" löschen?").arg(name))
        != QMessageBox::Yes)
        return;
    m_store.remove(name);
    m_store.save();
    reload();
}

void ServerManagerDialog::onConnect()
{
    ServerProfile p = formToProfile();
    if (p.host.isEmpty()) {
        QMessageBox::warning(this, _t("Fehler"), _t("Kein Host angegeben."));
        return;
    }
    // Gespeichertes Passwort/Passphrase aus dem Keyring nachladen (falls leer).
    if (p.authMethod == QLatin1String("password") && p.password.isEmpty()) {
        const auto pw = core::getSecret(p.name, QStringLiteral("password"));
        if (pw)
            p.password = *pw;
    }
    m_chosen = p;
    accept();
}

void ServerManagerDialog::onImport()
{
    // Gefundene Sitzungen zur Auswahl anbieten; zusaetzlich koennen zuvor
    // exportierte PuTTY-/WinSCP-Dateien geladen werden.
    std::vector<core::ServerProfile> found = core::importAll();

    QDialog dlg(this);
    dlg.setWindowTitle(_t("Sitzungen importieren"));
    dlg.resize(640, 480);
    auto *layout = new QVBoxLayout(&dlg);
    auto *info = new QLabel(
        _t("Gefundene Sitzungen aus PuTTY, WinSCP und ~/.ssh/config. "
           "Auswählen, was übernommen werden soll."), &dlg);
    info->setWordWrap(true);
    info->setObjectName(QStringLiteral("Muted"));
    layout->addWidget(info);

    auto *list = new QListWidget(&dlg);
    const auto fill = [&list, &found] {
        list->clear();
        for (int i = 0; i < int(found.size()); ++i) {
            auto *item = new QListWidgetItem(
                QStringLiteral("%1  —  %2").arg(found[i].name, found[i].display()), list);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Checked);
            item->setData(Qt::UserRole, i);
        }
    };
    fill();
    layout->addWidget(list, 1);

    auto *selRow = new QHBoxLayout();
    auto *allBtn = new QPushButton(_t("Alle"), &dlg);
    auto *noneBtn = new QPushButton(_t("Keine"), &dlg);
    auto *fileBtn = new QPushButton(_t("Aus Datei laden …"), &dlg);
    connect(allBtn, &QPushButton::clicked, &dlg, [list] {
        for (int i = 0; i < list->count(); ++i)
            list->item(i)->setCheckState(Qt::Checked);
    });
    connect(noneBtn, &QPushButton::clicked, &dlg, [list] {
        for (int i = 0; i < list->count(); ++i)
            list->item(i)->setCheckState(Qt::Unchecked);
    });
    connect(fileBtn, &QPushButton::clicked, &dlg, [this, &dlg, &found, &fill] {
        const QString path = getOpenFileName(
            &dlg, _t("Exportierte Sitzungen laden"), QString(),
            _t("PuTTY/WinSCP") + QStringLiteral(" (*.reg *.ini)"));
        if (path.isEmpty())
            return;
        const auto extra = core::importFromFile(path);
        if (extra.empty()) {
            QMessageBox::information(&dlg, _t("Import"),
                                     _t("In der Datei wurden keine Sitzungen gefunden."));
            return;
        }
        found.insert(found.end(), extra.begin(), extra.end());
        fill();
    });
    selRow->addWidget(allBtn);
    selRow->addWidget(noneBtn);
    selRow->addStretch(1);
    selRow->addWidget(fileBtn);
    layout->addLayout(selRow);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    box->button(QDialogButtonBox::Ok)->setText(_t("Importieren"));
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(box);

    if (dlg.exec() != QDialog::Accepted)
        return;

    int added = 0;
    for (int i = 0; i < list->count(); ++i) {
        if (list->item(i)->checkState() != Qt::Checked)
            continue;
        const int idx = list->item(i)->data(Qt::UserRole).toInt();
        if (idx >= 0 && idx < int(found.size())) {
            m_store.upsert(found[idx]);
            ++added;
        }
    }
    m_store.save();
    reload();
    QMessageBox::information(this, _t("Import"),
                             QStringLiteral("%1 Profil(e) importiert.").arg(added));
}

} // namespace ncssh::gui
