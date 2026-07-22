#include "ncssh/gui/key_dialog.hpp"

#include "ncssh/core/i18n.hpp"
#include "ncssh/core/keytools.hpp"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include "ncssh/gui/file_dialogs.hpp"
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;

KeyDialog::KeyDialog(AsyncBridge *bridge, QWidget *parent)
    : QDialog(parent), m_bridge(bridge)
{
    setWindowTitle(_t("SSH-Schlüssel"));
    resize(760, 560);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout();

    m_keyType = new QComboBox(this);
    for (const auto &[label, value] : core::keyTypes())
        m_keyType->addItem(label, value);
    form->addRow(_t("Schlüsseltyp"), m_keyType);

    m_comment = new QLineEdit(this);
    m_comment->setPlaceholderText(_t("z. B. benutzer@rechner"));
    form->addRow(_t("Kommentar"), m_comment);
    layout->addLayout(form);

    auto *genRow = new QHBoxLayout();
    auto *genBtn = new QPushButton(_t("Schlüsselpaar erzeugen"), this);
    connect(genBtn, &QPushButton::clicked, this, &KeyDialog::generate);
    genRow->addWidget(genBtn);
    genRow->addStretch(1);
    layout->addLayout(genRow);

    layout->addWidget(new QLabel(_t("Öffentlicher Schlüssel"), this));
    m_publicView = new QPlainTextEdit(this);
    m_publicView->setReadOnly(true);
    m_publicView->setPlaceholderText(
        _t("Noch kein Schlüssel erzeugt. Der öffentliche Teil gehört in "
           "~/.ssh/authorized_keys auf dem Server."));
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    m_publicView->setFont(mono);
    layout->addWidget(m_publicView, 1);

    auto *copyRow = new QHBoxLayout();
    auto *copyBtn = new QPushButton(_t("Öffentlichen Schlüssel kopieren"), this);
    connect(copyBtn, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(QString::fromUtf8(m_publicKey));
        m_status->setText(_t("In die Zwischenablage kopiert."));
    });
    auto *saveBtn = new QPushButton(_t("Schlüssel speichern …"), this);
    connect(saveBtn, &QPushButton::clicked, this, &KeyDialog::saveKeys);
    copyRow->addWidget(copyBtn);
    copyRow->addWidget(saveBtn);
    copyRow->addStretch(1);
    layout->addLayout(copyRow);

    // --- Konvertierung bestehender Schluesseldateien ---
    layout->addWidget(new QLabel(_t("Konvertieren"), this));
    auto *convRow = new QHBoxLayout();
    auto *toPpkBtn = new QPushButton(_t("OpenSSH → PPK …"), this);
    auto *toOpensshBtn = new QPushButton(_t("PPK → OpenSSH …"), this);
    connect(toPpkBtn, &QPushButton::clicked, this, &KeyDialog::convertToPpk);
    connect(toOpensshBtn, &QPushButton::clicked, this, &KeyDialog::convertToOpenssh);
    convRow->addWidget(toPpkBtn);
    convRow->addWidget(toOpensshBtn);
    convRow->addStretch(1);
    layout->addLayout(convRow);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("Muted"));
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    auto *closeBtn = new QPushButton(_t("Schließen"), this);
    closeBtn->setDefault(true);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    layout->addWidget(closeBtn);
}

void KeyDialog::generate()
{
    const QString type = m_keyType->currentData().toString();
    const QString comment = m_comment->text().trimmed();
    m_status->setText(_t("Erzeuge …"));
    m_bridge->run<QPair<QByteArray, QByteArray>>(
        [type, comment] {
            const auto [priv, pub] = core::generate(type, comment);
            return QPair<QByteArray, QByteArray>(priv, pub);
        },
        [this](const QPair<QByteArray, QByteArray> &keys) {
            m_privateKey = keys.first;
            m_publicKey = keys.second;
            m_publicView->setPlainText(QString::fromUtf8(m_publicKey));
            m_status->setText(_t("Schlüsselpaar erzeugt — jetzt speichern."));
        },
        [this](const QString &err) {
            m_status->setText(err);
            QMessageBox::warning(this, _t("Fehler"), err);
        });
}

void KeyDialog::saveKeys()
{
    if (m_privateKey.isEmpty()) {
        QMessageBox::information(this, _t("Speichern"),
                                 _t("Bitte zuerst ein Schlüsselpaar erzeugen."));
        return;
    }
    const QString path = getSaveFileName(
        this, _t("Privatschlüssel speichern"),
        QDir::homePath() + QStringLiteral("/.ssh/id_new"));
    if (path.isEmpty())
        return;
    QFile priv(path);
    QFile pub(path + QStringLiteral(".pub"));
    if (!priv.open(QIODevice::WriteOnly) || !pub.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, _t("Fehler"), _t("Konnte nicht schreiben."));
        return;
    }
    priv.write(m_privateKey);
    pub.write(m_publicKey);
    priv.close();
    pub.close();
    // Privatschluessel nur fuer den Eigentuemer lesbar machen.
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    m_savedKeyPath = path;
    m_status->setText(QStringLiteral("Gespeichert: %1 (+ .pub)").arg(path));
}

void KeyDialog::convertToPpk()
{
    const QString source = getOpenFileName(
        this, _t("OpenSSH-Privatschlüssel wählen"),
        QDir::homePath() + QStringLiteral("/.ssh"));
    if (source.isEmpty())
        return;
    QFile in(source);
    if (!in.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, _t("Fehler"), _t("Datei nicht lesbar."));
        return;
    }
    const QByteArray data = in.readAll();
    const QString target = getSaveFileName(
        this, _t("PPK speichern"), source + QStringLiteral(".ppk"),
        QStringLiteral("PuTTY (*.ppk)"));
    if (target.isEmpty())
        return;
    try {
        const QByteArray ppk = core::toPpk(data, m_comment->text().trimmed());
        QFile out(target);
        if (!out.open(QIODevice::WriteOnly))
            throw std::runtime_error("Konnte nicht schreiben.");
        out.write(ppk);
        m_status->setText(QStringLiteral("PPK geschrieben: %1").arg(target));
    } catch (const std::exception &exc) {
        QMessageBox::warning(this, _t("Konvertierung fehlgeschlagen"),
                             QString::fromUtf8(exc.what()));
    }
}

void KeyDialog::convertToOpenssh()
{
    const QString source = getOpenFileName(
        this, _t("PPK-Datei wählen"), QDir::homePath(), QStringLiteral("PuTTY (*.ppk)"));
    if (source.isEmpty())
        return;
    QFile in(source);
    if (!in.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, _t("Fehler"), _t("Datei nicht lesbar."));
        return;
    }
    const QByteArray data = in.readAll();
    const QString target = getSaveFileName(
        this, _t("OpenSSH-Schlüssel speichern"),
        QFileInfo(source).absolutePath() + QStringLiteral("/")
            + QFileInfo(source).completeBaseName());
    if (target.isEmpty())
        return;
    try {
        const QByteArray openssh = core::toOpenssh(data);
        QFile out(target);
        if (!out.open(QIODevice::WriteOnly))
            throw std::runtime_error("Konnte nicht schreiben.");
        out.write(openssh);
        out.close();
        QFile::setPermissions(target, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        m_savedKeyPath = target;
        m_status->setText(QStringLiteral("OpenSSH-Schlüssel geschrieben: %1").arg(target));
    } catch (const std::exception &exc) {
        QMessageBox::warning(this, _t("Konvertierung fehlgeschlagen"),
                             QString::fromUtf8(exc.what()));
    }
}

} // namespace ncssh::gui
