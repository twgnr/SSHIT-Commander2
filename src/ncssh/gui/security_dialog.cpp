#include "ncssh/gui/security_dialog.hpp"

#include "ncssh/core/i18n.hpp"
#include "ncssh/core/secaudit.hpp"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;

namespace {
// Ein Befund fuer die Anzeige: Gruppe / Titel / Schwere / Detail (+ optional URL).
struct Finding {
    QString group;
    QString title;
    QString severity;
    QString detail;
    QString url;
};

QColor severityColor(const QString &severity)
{
    const QString s = severity.toLower();
    if (s.contains(QLatin1String("critical")) || s.contains(QLatin1String("hoch"))
        || s.contains(QLatin1String("high")))
        return QColor(QStringLiteral("#ef4444"));
    if (s.contains(QLatin1String("medium")) || s.contains(QLatin1String("mittel")))
        return QColor(QStringLiteral("#d19a66"));
    if (s.contains(QLatin1String("low")) || s.contains(QLatin1String("niedrig")))
        return QColor(QStringLiteral("#4f8cff"));
    return QColor(QStringLiteral("#8b90a0"));
}
} // namespace

SecurityDialog::SecurityDialog(AsyncBridge *bridge, net::SSHSessionPtr session, QWidget *parent)
    : QDialog(parent), m_bridge(bridge), m_session(std::move(session))
{
    setWindowTitle(_t("Sicherheits-Audit (CVE)"));
    resize(1000, 660);

    auto *layout = new QVBoxLayout(this);
    auto *info = new QLabel(
        _t("Liest OS, Pakete und Konfiguration des verbundenen Servers aus und gleicht "
           "Kernkomponenten mit der OSV.dev-Datenbank ab. Es werden keine Änderungen "
           "vorgenommen."), this);
    info->setObjectName(QStringLiteral("Muted"));
    info->setWordWrap(true);
    layout->addWidget(info);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({_t("Befund"), _t("Schwere"), _t("CVE / Info")});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_tree->setAlternatingRowColors(true);
    // Doppelklick auf einen CVE-Eintrag oeffnet die Advisory-Seite.
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [](QTreeWidgetItem *item, int) {
        const QString url = item->data(0, Qt::UserRole).toString();
        if (!url.isEmpty())
            QDesktopServices::openUrl(QUrl(url));
    });
    // Auswahl zeigt den Detailtext gross darunter — in der Spalte wird er
    // sonst abgeschnitten.
    connect(m_tree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *item, QTreeWidgetItem *) {
                if (!item || item->childCount() > 0) {
                    m_details->setPlainText(_t("Eintrag auswählen für Details …"));
                    return;
                }
                const QString detail = item->text(2);
                m_details->setPlainText(detail.isEmpty()
                                            ? _t("Keine Detailbeschreibung verfügbar.")
                                            : detail);
            });
    layout->addWidget(m_tree, 1);

    m_details = new QPlainTextEdit(this);
    m_details->setReadOnly(true);
    m_details->setMaximumHeight(90);
    m_details->setPlainText(_t("Eintrag auswählen für Details …"));
    layout->addWidget(m_details);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 0);
    m_progress->setVisible(false);
    layout->addWidget(m_progress);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("Muted"));
    layout->addWidget(m_status);

    auto *buttons = new QHBoxLayout();
    m_runBtn = new QPushButton(_t("Audit starten"), this);
    m_runBtn->setDefault(true);
    auto *closeBtn = new QPushButton(_t("Schließen"), this);
    connect(m_runBtn, &QPushButton::clicked, this, &SecurityDialog::runAudit);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(m_runBtn);
    buttons->addStretch(1);
    buttons->addWidget(closeBtn);
    layout->addLayout(buttons);
}

void SecurityDialog::runAudit()
{
    m_tree->clear();
    m_progress->setVisible(true);
    m_runBtn->setEnabled(false);
    m_status->setText(_t("Scanne …"));

    net::SSHSessionPtr session = m_session;
    m_bridge->run<std::vector<Finding>>(
        [session]() -> std::vector<Finding> {
            std::vector<Finding> findings;
            const auto run = [&session](const QString &cmd) {
                try {
                    return QString::fromUtf8(session->exec(cmd).out);
                } catch (...) {
                    return QString();
                }
            };

            // --- System ---
            const auto osRelease = core::parseOsRelease(run(QStringLiteral("cat /etc/os-release")));
            const QString osId = osRelease.value(QStringLiteral("ID"));
            const QString versionId = osRelease.value(QStringLiteral("VERSION_ID"));
            const QString kernel = run(QStringLiteral("uname -r")).trimmed();
            findings.push_back({_t("System"), _t("Betriebssystem"), QString(),
                                QStringLiteral("%1 %2").arg(
                                    osRelease.value(QStringLiteral("PRETTY_NAME"), osId),
                                    versionId), {}});
            findings.push_back({_t("System"), _t("Kernel"), QString(), kernel, {}});

            // --- Offene Sicherheitsupdates ---
            const auto upgrades = core::parseAptUpgrade(
                run(QStringLiteral("apt-get -s upgrade 2>/dev/null")));
            int securityCount = 0;
            for (const auto &up : upgrades) {
                if (!up.security)
                    continue;
                ++securityCount;
                findings.push_back({_t("Updates"), up.package, QStringLiteral("security"),
                                    _t("Sicherheitsupdate → %1").arg(up.newVersion), {}});
            }
            const auto dnfCves = core::parseDnfCves(
                run(QStringLiteral("dnf updateinfo list cves 2>/dev/null")));
            for (const auto &c : dnfCves) {
                findings.push_back({_t("Updates"), c.cve, c.severity, c.package,
                                    QStringLiteral("https://osv.dev/vulnerability/%1").arg(c.cve)});
            }
            if (securityCount == 0 && dnfCves.empty())
                findings.push_back({_t("Updates"), _t("Keine offenen Sicherheitsupdates gefunden."),
                                    QString(), QString(), {}});

            // --- sshd-Konfiguration ---
            const auto sshdCfg = core::parseSshdConfig(
                run(QStringLiteral("cat /etc/ssh/sshd_config 2>/dev/null")));
            for (const auto &f : core::auditSshd(sshdCfg)) {
                findings.push_back({QStringLiteral("sshd_config"),
                                    QStringLiteral("%1 %2").arg(f.directive, f.value),
                                    f.severity, f.detail, {}});
            }

            // --- Automatische Sicherheitsupdates ---
            const QString unattended =
                run(QStringLiteral("cat /etc/apt/apt.conf.d/20auto-upgrades 2>/dev/null"));
            if (!unattended.trimmed().isEmpty() && !core::unattendedEnabled(unattended)) {
                findings.push_back(
                    {_t("Automatische Updates"), _t("unattended-upgrades"),
                     QStringLiteral("medium"),
                     _t("Automatische Sicherheitsupdates (unattended-upgrades) sind nicht "
                        "aktiviert"), {}});
            }

            // --- Firewall ---
            const auto ufw = core::parseUfwStatus(run(QStringLiteral("ufw status 2>/dev/null")));
            if (ufw) {
                const bool active = (*ufw == QLatin1String("active"));
                findings.push_back({_t("Firewall"), QStringLiteral("ufw"),
                                    active ? QString() : QStringLiteral("medium"),
                                    active ? *ufw : _t("Die Firewall (ufw) ist inaktiv."), {}});
            }
            const QString firewalld =
                run(QStringLiteral("systemctl is-active firewalld 2>/dev/null")).trimmed();
            if (!firewalld.isEmpty() && firewalld != QLatin1String("active")
                && firewalld != QLatin1String("unknown")) {
                findings.push_back({_t("Firewall"), QStringLiteral("firewalld"),
                                    QStringLiteral("medium"),
                                    _t("Die Firewall (firewalld) ist inaktiv."), {}});
            }

            // --- Ausstehender Neustart ---
            if (!run(QStringLiteral("test -f /var/run/reboot-required && echo yes"))
                     .trimmed().isEmpty()) {
                findings.push_back(
                    {_t("Neustart nötig"), _t("reboot-required"), QStringLiteral("medium"),
                     _t("Ein Neustart steht aus (z. B. nach Kernel-Update). System neu "
                        "starten."), {}});
            }

            // --- Ablaufende Zertifikate ---
            const QString certs = run(QStringLiteral(
                "for c in /etc/letsencrypt/live/*/cert.pem /etc/ssl/certs/*.pem; do "
                "[ -f \"$c\" ] || continue; "
                "if openssl x509 -checkend 1814400 -noout -in \"$c\" >/dev/null 2>&1; "
                "then echo \"OK $c\"; else echo \"SOON $c\"; fi; done 2>/dev/null"));
            for (const QString &path : core::parseCertCheck(certs)) {
                findings.push_back(
                    {_t("Zertifikate"), path, QStringLiteral("medium"),
                     _t("Zertifikat ist abgelaufen oder läuft in < 21 Tagen ab — erneuern."),
                     {}});
            }

            // --- Offene Ports ---
            const auto listening = core::parseListening(run(QStringLiteral("ss -tuln 2>/dev/null")));
            const auto publicPorts = core::publicPorts(listening);
            QStringList portList;
            for (const auto &p : publicPorts) {
                portList << QStringLiteral("%1/%2").arg(p.proto, p.port);
                findings.push_back({_t("Offene Ports"),
                                    QStringLiteral("%1/%2").arg(p.proto, p.port),
                                    QStringLiteral("info"), _t("öffentlich erreichbar"), {}});
            }
            if (!portList.isEmpty()) {
                findings.push_back({_t("Offene Ports"), _t("Zusammenfassung"), QString(),
                                    _t("Öffentlich erreichbare Ports: %1")
                                        .arg(portList.join(QStringLiteral(", "))), {}});
            }

            // --- Konten ---
            for (const auto &a : core::auditAccounts(
                     run(QStringLiteral("cat /etc/passwd 2>/dev/null")),
                     run(QStringLiteral("sudo -n cat /etc/shadow 2>/dev/null")))) {
                findings.push_back({_t("Konten"), a.account, a.severity, a.detail, {}});
            }

            // --- Online-CVE-Abgleich (OSV.dev) fuer Kernkomponenten ---
            const auto ecosystem = core::osvEcosystem(osId, versionId);
            if (!ecosystem) {
                // Ohne bekanntes Ecosystem laesst sich nichts abgleichen — das
                // gehoert in die Liste, sonst wirkt der Abschnitt einfach leer.
                findings.push_back(
                    {QStringLiteral("CVE (OSV.dev)"), _t("Audit nicht möglich"),
                     QStringLiteral("info"),
                     _t("Online-CVE-Abgleich (OSV) für diese Distribution nicht verfügbar."),
                     {}});
            }
            if (ecosystem) {
                QJsonArray queries;
                for (const QString &pkg : core::keyPackages()) {
                    const QString version =
                        run(QStringLiteral("dpkg-query -W -f='${Version}' %1 2>/dev/null").arg(pkg))
                            .trimmed();
                    if (version.isEmpty())
                        continue;
                    queries.append(QJsonObject{
                        {QStringLiteral("package"), QJsonObject{
                            {QStringLiteral("name"), pkg},
                            {QStringLiteral("ecosystem"), *ecosystem}}},
                        {QStringLiteral("version"), version}});
                }
                if (queries.isEmpty()) {
                    findings.push_back(
                        {QStringLiteral("CVE (OSV.dev)"), _t("Audit nicht möglich"),
                         QStringLiteral("info"),
                         _t("Keine Versionsdaten für den OSV-Abgleich gefunden."), {}});
                }
                if (!queries.isEmpty()) {
                    try {
                        const QJsonObject response = core::osvQuerybatch(queries);
                        const QJsonArray results =
                            response.value(QStringLiteral("results")).toArray();
                        for (int i = 0; i < results.size() && i < queries.size(); ++i) {
                            const QJsonArray vulns =
                                results[i].toObject().value(QStringLiteral("vulns")).toArray();
                            const QString pkgName = queries[i].toObject()
                                                        .value(QStringLiteral("package")).toObject()
                                                        .value(QStringLiteral("name")).toString();
                            for (const QJsonValue &v : vulns) {
                                const auto summary = core::summarizeVuln(v.toObject());
                                findings.push_back({QStringLiteral("CVE (OSV.dev)"),
                                                    QStringLiteral("%1: %2").arg(pkgName, summary.id),
                                                    summary.severity, summary.summary, summary.url});
                            }
                        }
                    } catch (const std::exception &exc) {
                        findings.push_back({QStringLiteral("CVE (OSV.dev)"), _t("Audit nicht möglich"),
                                            QStringLiteral("info"),
                                            _t("OSV-Online-Abgleich nicht möglich: %1")
                                                .arg(QString::fromUtf8(exc.what())), {}});
                    }
                }
            }
            return findings;
        },
        [this](const std::vector<Finding> &findings) {
            // Nach Gruppe buendeln
            QHash<QString, QTreeWidgetItem *> groups;
            for (const Finding &f : findings) {
                QTreeWidgetItem *parent = groups.value(f.group, nullptr);
                if (!parent) {
                    parent = new QTreeWidgetItem(m_tree, {f.group});
                    parent->setExpanded(true);
                    groups.insert(f.group, parent);
                }
                auto *item = new QTreeWidgetItem(parent, {f.title, f.severity, f.detail});
                if (!f.severity.isEmpty())
                    item->setForeground(1, severityColor(f.severity));
                if (!f.url.isEmpty()) {
                    item->setData(0, Qt::UserRole, f.url);
                    item->setToolTip(0, _t("Doppelklick öffnet die Advisory-Seite"));
                }
            }
            m_progress->setVisible(false);
            m_runBtn->setEnabled(true);

            // Kopfzeile: System, Kernel und wie viele Befunde wirklich kritisch
            // sind — eine nackte Gesamtzahl sagt darueber nichts.
            QString os, kernel;
            int critical = 0, relevant = 0;
            for (const Finding &f : findings) {
                if (f.group == _t("System") && f.title == _t("Betriebssystem"))
                    os = f.detail;
                else if (f.group == _t("System") && f.title == _t("Kernel"))
                    kernel = f.detail;
                if (f.severity.isEmpty())
                    continue;
                ++relevant;
                const QString sev = f.severity.toLower();
                if (sev.contains(QLatin1String("critical")) || sev.contains(QLatin1String("high")))
                    ++critical;
            }
            QString summary = _t("%1  ·  Kernel %2  ·  %3 Sicherheitshinweis(e)")
                                  .arg(os.isEmpty() ? _t("Unbekannt") : os,
                                       kernel.isEmpty() ? _t("Unbekannt") : kernel)
                                  .arg(relevant);
            if (critical > 0)
                summary += _t(", davon %1 kritisch").arg(critical);
            m_status->setText(summary);
        },
        [this](const QString &err) {
            m_progress->setVisible(false);
            m_runBtn->setEnabled(true);
            m_status->setText(_t("Fehler beim Scan") + QStringLiteral(": ") + err);
        });
}

} // namespace ncssh::gui
