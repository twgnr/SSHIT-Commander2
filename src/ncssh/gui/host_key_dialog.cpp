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

} // namespace ncssh::gui
