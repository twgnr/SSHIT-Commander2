#include "ncssh/gui/host_key_dialog.hpp"

#include "ncssh/core/i18n.hpp"

#include <QApplication>
#include <QClipboard>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;

HostKeyDialog::HostKeyDialog(const QString &host, int port, const QString &algorithm,
                             const QString &fingerprint, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(_t("Unbekannter Host-Key"));
    resize(620, 300);

    auto *layout = new QVBoxLayout(this);
    auto *warning = new QLabel(
        _t("Dieser Server ist noch nicht bekannt. Prüfe den Fingerprint über einen "
           "vertrauenswürdigen Kanal — stimmt er nicht, könnte ein Angreifer die "
           "Verbindung umleiten (MITM)."), this);
    warning->setWordWrap(true);
    layout->addWidget(warning);

    auto *form = new QFormLayout();
    form->addRow(_t("Server"), new QLabel(QStringLiteral("%1:%2").arg(host).arg(port), this));
    form->addRow(_t("Algorithmus"), new QLabel(algorithm, this));

    auto *fpLabel = new QLabel(fingerprint, this);
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    fpLabel->setFont(mono);
    fpLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(_t("Fingerprint"), fpLabel);
    layout->addLayout(form);

    auto *copyRow = new QHBoxLayout();
    auto *copyBtn = new QPushButton(_t("Fingerprint kopieren"), this);
    connect(copyBtn, &QPushButton::clicked, this, [fingerprint] {
        QApplication::clipboard()->setText(fingerprint);
    });
    copyRow->addWidget(copyBtn);
    copyRow->addStretch(1);
    layout->addLayout(copyRow);

    layout->addStretch(1);
    auto *buttons = new QHBoxLayout();
    auto *rejectBtn = new QPushButton(_t("Abbrechen"), this);
    auto *onceBtn = new QPushButton(_t("Nur diesmal verbinden"), this);
    auto *trustBtn = new QPushButton(_t("Vertrauen und speichern"), this);
    trustBtn->setDefault(true);
    connect(rejectBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(onceBtn, &QPushButton::clicked, this, [this] {
        m_trust = false;
        accept();
    });
    connect(trustBtn, &QPushButton::clicked, this, [this] {
        m_trust = true;
        accept();
    });
    buttons->addWidget(rejectBtn);
    buttons->addStretch(1);
    buttons->addWidget(onceBtn);
    buttons->addWidget(trustBtn);
    layout->addLayout(buttons);
}

bool HostKeyDialog::ask(const QString &host, int port, const QString &algorithm,
                        const QString &fingerprint, QWidget *parent)
{
    HostKeyDialog dlg(host, port, algorithm, fingerprint, parent);
    return dlg.exec() == QDialog::Accepted && dlg.trustPermanently();
}

bool HostKeyDialog::askChanged(const QString &host, int port, const QString &algorithm,
                               const QString &expected, const QString &received,
                               QWidget *parent)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(_t("Host-Key geändert"));
    auto *layout = new QVBoxLayout(&dlg);

    auto *warning = new QLabel(
        QStringLiteral("<b style='color:#f85149'>%1</b><br><br>%2")
            .arg(_t("HOST-KEY HAT SICH GEÄNDERT — möglicher MITM-Angriff!"),
                 _t("Der Server meldet einen anderen Schlüssel als beim letzten Mal. "
                    "Das kann eine Neuinstallation sein — oder jemand hängt sich "
                    "dazwischen. Nur fortfahren, wenn die Änderung bekannt ist.")),
        &dlg);
    warning->setWordWrap(true);
    layout->addWidget(warning);

    auto *info = new QLabel(
        _t("<b>Host:</b> %1:%2<br><b>Key-Typ:</b> %3")
            .arg(host).arg(port).arg(algorithm.isEmpty() ? _t("unbekannt") : algorithm),
        &dlg);
    layout->addWidget(info);

    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    auto *form = new QFormLayout();
    auto *expectedLabel = new QLabel(expected, &dlg);
    expectedLabel->setFont(mono);
    expectedLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *receivedLabel = new QLabel(received, &dlg);
    receivedLabel->setFont(mono);
    receivedLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(_t("Erwartet:"), expectedLabel);
    form->addRow(_t("Erhalten:"), receivedLabel);
    layout->addLayout(form);

    auto *buttons = new QHBoxLayout();
    auto *cancel = new QPushButton(_t("Abbrechen"), &dlg);
    cancel->setDefault(true);   // im Zweifel NICHT verbinden
    auto *trust = new QPushButton(_t("Trotzdem vertrauen"), &dlg);
    QObject::connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    QObject::connect(trust, &QPushButton::clicked, &dlg, &QDialog::accept);
    buttons->addWidget(cancel);
    buttons->addStretch(1);
    buttons->addWidget(trust);
    layout->addLayout(buttons);

    return dlg.exec() == QDialog::Accepted;
}

} // namespace ncssh::gui
