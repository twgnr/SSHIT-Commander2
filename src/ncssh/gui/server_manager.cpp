#include "ncssh/gui/server_manager.hpp"

#include "ncssh/core/i18n.hpp"
#include "ncssh/core/importers.hpp"
#include "ncssh/core/secrets.hpp"
#include "ncssh/gui/key_dialog.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include "ncssh/gui/file_dialogs.hpp"
#include <QFormLayout>
#include <QTcpSocket>
#include <stdexcept>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;
using core::ServerProfile;

ServerManagerDialog::ServerManagerDialog(AsyncBridge *bridge, QWidget *parent)
    : QDialog(parent), m_bridge(bridge)
{
    setWindowTitle(_t("Server-Verwaltung"));
    resize(820, 560);
    m_store.load();

    auto *root = new QHBoxLayout(this);

    // Linke Spalte: Filter + Liste
    auto *left = new QVBoxLayout();
    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(_t("Filtern … (Name, Host, Benutzer)"));
    m_filter->setClearButtonEnabled(true);
    connect(m_filter, &QLineEdit::textChanged, this, [this] { reload(); });
    m_list = new QListWidget(this);
    connect(m_list, &QListWidget::currentTextChanged, this, [this](const QString &name) {
        if (auto p = m_store.get(name)) {
            // Gespeichertes Passwort/Passphrase aus dem Keyring ins Formular
            // holen — sonst wuerde ein erneutes Speichern mit leerem Feld das
            // gespeicherte Passwort loeschen.
            m_store.hydrate(*p);
            loadIntoForm(*p);
        }
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this, &ServerManagerDialog::onConnect);
    left->addWidget(new QLabel(_t("Verbindungen"), this));
    left->addWidget(m_filter);
    left->addWidget(m_list, 1);
    auto *newBtn = new QPushButton(_t("Neuer Server"), this);
    connect(newBtn, &QPushButton::clicked, this, [this] {
        m_list->clearSelection();
        loadIntoForm(core::ServerProfile{});
        m_name->setFocus();
    });
    left->addWidget(newBtn);
    auto *importBtn = new QPushButton(_t("Import (PuTTY/WinSCP/SSH)"), this);
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
    m_auth->addItem(_t("SSH-Key"), QStringLiteral("key"));
    m_auth->addItem(_t("Passwort"), QStringLiteral("password"));
    m_auth->addItem(_t("SSH-Agent"), QStringLiteral("agent"));
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
            m_auth->setCurrentIndex(m_auth->findData(QStringLiteral("key")));
        }
    });
    keyRow->addWidget(keyToolsBtn);
    m_password = new QLineEdit(this);
    m_password->setEchoMode(QLineEdit::Password);
    m_savePassword = new QCheckBox(
        _t("Passwort/Passphrase sicher im OS-Keyring speichern"), this);
    m_policy = new QComboBox(this);
    // Klartext statt der internen Kennungen — die Kennung steckt in den Daten.
    m_policy->addItem(_t("Beim ersten Mal vertrauen (accept-new)"),
                      QStringLiteral("accept-new"));
    m_policy->addItem(_t("Strikt (nur bekannte)"), QStringLiteral("strict"));
    m_policy->addItem(_t("Ignorieren (unsicher)"), QStringLiteral("ignore"));
    m_proxyJump = new QLineEdit(this);
    m_proxyJump->setPlaceholderText(_t("ProxyJump, z.B. user@jump:22  (optional)"));
    m_startPath = new QLineEdit(this);
    m_startPath->setText(QStringLiteral("."));

    // Tab-Farbe: Auswahlknopf zeigt die Farbe selbst.
    auto *colorRow = new QHBoxLayout();
    m_colorButton = new QPushButton(_t("Tab-Farbe wählen …"), this);
    connect(m_colorButton, &QPushButton::clicked, this, &ServerManagerDialog::pickTabColor);
    auto *clearColor = new QPushButton(QStringLiteral("✕"), this);
    clearColor->setFixedWidth(30);
    connect(clearColor, &QPushButton::clicked, this, [this] {
        m_tabColor.clear();
        updateColorButton();
    });
    colorRow->addWidget(m_colorButton, 1);
    colorRow->addWidget(clearColor);

    form->addRow(_t("Anzeigename *"), m_name);
    form->addRow(_t("Host *"), m_host);
    form->addRow(_t("Port"), m_port);
    form->addRow(_t("Benutzername"), m_user);
    form->addRow(_t("Authentifizierung"), m_auth);
    form->addRow(_t("Key-Pfad"), keyRow);
    form->addRow(_t("Passwort"), m_password);
    form->addRow(QString(), m_savePassword);
    form->addRow(_t("Host-Key-Prüfung"), m_policy);
    form->addRow(_t("ProxyJump"), m_proxyJump);
    form->addRow(_t("Startverzeichnis"), m_startPath);
    form->addRow(_t("Tab-Farbe"), colorRow);

    // --- Verbindungs-Feinsteuerung ---
    m_keepalive = new QSpinBox(this);
    m_keepalive->setRange(0, 3600);
    m_keepalive->setSuffix(QStringLiteral(" s"));
    m_keepalive->setToolTip(_t("Keepalive-Intervall (0 = aus)"));
    form->addRow(_t("Keepalive"), m_keepalive);
    m_timeout = new QSpinBox(this);
    m_timeout->setRange(5, 300);
    m_timeout->setSuffix(QStringLiteral(" s"));
    form->addRow(_t("Verbindungs-Timeout"), m_timeout);
    m_compression = new QCheckBox(_t("SSH-Kompression"), this);
    form->addRow(QString(), m_compression);
    m_agentFwd = new QCheckBox(_t("SSH-Agent weiterreichen (Forwarding)"), this);
    // libssh2 kann eingehende auth-agent@openssh.com-Kanaele NICHT annehmen.
    // Ein aktiviertes Forwarding erzeugte auf dem Server nur ein SSH_AUTH_SOCK,
    // das nie antwortet — daher bewusst deaktiviert statt scheinbar vorhanden.
    m_agentFwd->setEnabled(false);
    m_agentFwd->setToolTip(
        _t("Von der verwendeten SSH-Bibliothek (libssh2) nicht unterstützt."));
    form->addRow(QString(), m_agentFwd);
    m_ciphers = new QLineEdit(this);
    m_ciphers->setPlaceholderText(_t("Standard — z. B. aes256-ctr,aes128-ctr"));
    form->addRow(_t("Chiffren"), m_ciphers);
    m_kex = new QLineEdit(this);
    m_kex->setPlaceholderText(_t("Standard — z. B. ecdh-sha2-nistp256"));
    form->addRow(_t("Schlüsseltausch"), m_kex);

    m_lastConnected = new QLabel(this);
    m_lastConnected->setObjectName(QStringLiteral("Muted"));
    form->addRow(_t("Zuletzt"), m_lastConnected);

    m_reachability = new QLabel(this);
    m_reachability->setObjectName(QStringLiteral("Muted"));
    m_reachability->setWordWrap(true);
    form->addRow(_t("Erreichbarkeit"), m_reachability);

    auto *right = new QVBoxLayout();
    right->addLayout(form);
    right->addStretch(1);
    auto *btnRow = new QHBoxLayout();
    auto *saveBtn = new QPushButton(_t("Speichern"), this);
    auto *delBtn = new QPushButton(_t("Löschen"), this);
    auto *testBtn = new QPushButton(_t("Erreichbarkeit testen"), this);
    auto *connectBtn = new QPushButton(_t("Verbinden"), this);
    connectBtn->setDefault(true);
    connect(saveBtn, &QPushButton::clicked, this, &ServerManagerDialog::onSave);
    connect(delBtn, &QPushButton::clicked, this, &ServerManagerDialog::onDelete);
    connect(testBtn, &QPushButton::clicked, this, &ServerManagerDialog::testReachability);
    connect(connectBtn, &QPushButton::clicked, this, &ServerManagerDialog::onConnect);
    btnRow->addWidget(saveBtn);
    btnRow->addWidget(delBtn);
    btnRow->addWidget(testBtn);
    btnRow->addStretch(1);
    btnRow->addWidget(connectBtn);
    right->addLayout(btnRow);
    root->addLayout(right, 2);
    updateColorButton();

    reload();
}

void ServerManagerDialog::reload()
{
    const QString needle = m_filter ? m_filter->text().trimmed().toLower() : QString();
    m_list->clear();
    for (const auto &p : m_store.profiles()) {
        if (!needle.isEmpty()
            && !p.name.toLower().contains(needle)
            && !p.host.toLower().contains(needle)
            && !p.username.toLower().contains(needle))
            continue;
        m_list->addItem(p.name);
    }
}

void ServerManagerDialog::updateColorButton()
{
    if (m_tabColor.isEmpty()) {
        m_colorButton->setStyleSheet(QString());
        m_colorButton->setText(_t("Tab-Farbe wählen …"));
        return;
    }
    m_colorButton->setStyleSheet(
        QStringLiteral("QPushButton { background: %1; }").arg(m_tabColor));
    m_colorButton->setText(m_tabColor);
}

void ServerManagerDialog::pickTabColor()
{
    const QColor chosen = QColorDialog::getColor(
        m_tabColor.isEmpty() ? QColor(Qt::gray) : QColor(m_tabColor), this, _t("Tab-Farbe"));
    if (!chosen.isValid())
        return;
    m_tabColor = chosen.name();
    updateColorButton();
}

void ServerManagerDialog::testReachability()
{
    const QString host = m_host->text().trimmed();
    const int port = m_port->value();
    if (host.isEmpty()) {
        m_reachability->setText(_t("Es wurde keine Verbindung ausgewählt."));
        return;
    }
    m_reachability->setText(_t("Teste %1:%2 …").arg(host).arg(port));
    m_bridge->run<QString>(
        [host, port]() -> QString {
            // Reiner TCP-Verbindungstest, ohne SSH-Handshake.
            QTcpSocket socket;
            socket.connectToHost(host, quint16(port));
            if (!socket.waitForConnected(5000))
                throw std::runtime_error(socket.errorString().toStdString());
            socket.disconnectFromHost();
            return QString();
        },
        [this, host, port](const QString &) {
            m_reachability->setText(_t("✓ %1:%2 erreichbar.").arg(host).arg(port));
        },
        [this, host, port](const QString &err) {
            m_reachability->setText(
                _t("%1:%2 nicht erreichbar:\n%3").arg(host).arg(port).arg(err));
        });
}

void ServerManagerDialog::loadIntoForm(const ServerProfile &p)
{
    m_name->setText(p.name);
    m_host->setText(p.host);
    m_port->setValue(p.port ? p.port : 22);
    m_user->setText(p.username);
    m_auth->setCurrentIndex(std::max(0, m_auth->findData(p.authMethod)));
    m_keyPath->setText(p.keyPath);
    const int policyIndex = m_policy->findData(p.knownHostsPolicy);
    m_policy->setCurrentIndex(policyIndex >= 0 ? policyIndex : 0);
    m_proxyJump->setText(p.proxyJump);
    m_keepalive->setValue(p.keepaliveSeconds);
    m_timeout->setValue(p.connectTimeout);
    m_compression->setChecked(p.compression);
    m_agentFwd->setChecked(p.agentForwarding);
    m_ciphers->setText(p.ciphers);
    m_kex->setText(p.kexAlgorithms);
    m_startPath->setText(p.startPath);
    m_savePassword->setChecked(p.savePassword);
    m_tabColor = p.color;
    updateColorButton();
    m_lastConnected->setText(p.lastConnected.isEmpty() ? QStringLiteral("—") : p.lastConnected);
    m_reachability->clear();
    // Gespeichertes Passwort (maskiert) anzeigen, damit erkennbar ist, dass es
    // hinterlegt ist, und ein erneutes Speichern es nicht verwirft.
    m_password->setText(p.password);
}

ServerProfile ServerManagerDialog::formToProfile() const
{
    ServerProfile p;
    p.name = m_name->text().trimmed();
    p.host = m_host->text().trimmed();
    p.port = m_port->value();
    p.username = m_user->text().trimmed();
    p.authMethod = m_auth->currentData().toString();
    p.keyPath = m_keyPath->text().trimmed();
    p.password = m_password->text();
    p.savePassword = m_savePassword->isChecked();
    p.knownHostsPolicy = m_policy->currentData().toString();
    p.proxyJump = m_proxyJump->text().trimmed();
    p.keepaliveSeconds = m_keepalive->value();
    p.connectTimeout = m_timeout->value();
    p.compression = m_compression->isChecked();
    p.agentForwarding = m_agentFwd->isChecked();
    p.ciphers = m_ciphers->text().trimmed();
    p.kexAlgorithms = m_kex->text().trimmed();
    p.color = m_tabColor;
    p.startPath = m_startPath->text().trimmed().isEmpty() ? QStringLiteral(".")
                                                          : m_startPath->text().trimmed();
    // Bestehenden Zeitstempel erhalten — das Formular zeigt ihn nur an.
    if (const auto existing = m_store.get(p.name)) {
        p.lastConnected = existing->lastConnected;
        // Die Passphrase hat kein Formularfeld — gespeicherten Wert erhalten,
        // damit ein erneutes Speichern sie nicht aus dem Keyring wirft.
        if (p.savePassword)
            p.passphrase =
                core::getSecret(p.name, QStringLiteral("passphrase")).value_or(QString());
    }
    return p;
}

void ServerManagerDialog::onSave()
{
    ServerProfile p = formToProfile();
    if (p.name.isEmpty() || p.host.isEmpty()) {
        QMessageBox::warning(this, _t("Fehlende Angaben"), _t("Name und Host sind Pflicht."));
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
    if (QMessageBox::question(this, _t("Löschen"), _t("Profil '%1' löschen?").arg(name))
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
    dlg.setWindowTitle(_t("Verbindungen importieren"));
    dlg.resize(640, 480);
    auto *layout = new QVBoxLayout(&dlg);
    auto *info = new QLabel(
        _t("Gefundene Verbindungen auswählen, die importiert werden sollen:")
            + QStringLiteral("\n") + _t("Hinweis: Passwörter werden nicht übernommen."),
        &dlg);
    info->setWordWrap(true);
    info->setObjectName(QStringLiteral("Muted"));
    layout->addWidget(info);

    auto *list = new QListWidget(&dlg);
    // Vorhandene Profile kenntlich machen — sie werden beim Import ersetzt.
    QSet<QString> known;
    for (const auto &existing : m_store.profiles())
        known.insert(existing.host + QLatin1Char(':') + QString::number(existing.port));
    const auto fill = [&list, &found, &known] {
        list->clear();
        if (found.empty()) {
            auto *empty = new QListWidgetItem(_t("Keine Sitzungen automatisch gefunden"), list);
            empty->setFlags(Qt::NoItemFlags);
            return;
        }
        for (int i = 0; i < int(found.size()); ++i) {
            const QString key =
                found[i].host + QLatin1Char(':') + QString::number(found[i].port);
            QString label = QStringLiteral("%1  —  %2").arg(found[i].name, found[i].display());
            if (known.contains(key))
                label = _t("%1  (bereits vorhanden)").arg(label);
            auto *item = new QListWidgetItem(label, list);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            // Vorhandene nicht standardmaessig ueberschreiben.
            item->setCheckState(known.contains(key) ? Qt::Unchecked : Qt::Checked);
            item->setData(Qt::UserRole, i);
        }
    };
    fill();
    layout->addWidget(list, 1);

    auto *selRow = new QHBoxLayout();
    auto *allBtn = new QPushButton(_t("Alle"), &dlg);
    auto *noneBtn = new QPushButton(_t("Keine"), &dlg);
    auto *fileBtn = new QPushButton(_t("Aus Datei importieren …"), &dlg);
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
            &dlg, _t("Exportierte PuTTY-/WinSCP-Datei wählen"), QString(),
            _t("Export-Dateien (*.reg *.ini);;Alle Dateien (*.*)"));
        if (path.isEmpty())
            return;
        QString error;
        std::vector<core::ServerProfile> extra;
        try {
            extra = core::importFromFile(path);
        } catch (const std::exception &exc) {
            QMessageBox::warning(&dlg, _t("Import"),
                                 _t("Datei konnte nicht gelesen werden: %1")
                                     .arg(QString::fromUtf8(exc.what())));
            return;
        }
        if (extra.empty()) {
            QMessageBox::information(&dlg, _t("Import"),
                                     _t("Keine Sitzungen in der Datei gefunden."));
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
    box->button(QDialogButtonBox::Ok)->setText(_t("Ausgewählte importieren"));
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
                             _t("%1 Verbindung(en) importiert.").arg(added));
}

} // namespace ncssh::gui
