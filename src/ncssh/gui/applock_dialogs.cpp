#include "ncssh/gui/applock_dialogs.hpp"

#include "ncssh/core/applock.hpp"
#include "ncssh/core/i18n.hpp"
#include "ncssh/core/profiles.hpp"
#include "ncssh/gui/file_dialogs.hpp"

#include <qrcodegen.hpp>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFile>
#include <QFont>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace ncssh::gui {

using core::_t;
namespace lock = core::applock;

namespace {

QLineEdit *passwordField(QWidget *parent)
{
    auto *edit = new QLineEdit(parent);
    edit->setEchoMode(QLineEdit::Password);
    return edit;
}

QLabel *errorLabel(QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setStyleSheet(QStringLiteral("color: #f85149;"));
    label->setWordWrap(true);
    label->setVisible(false);
    return label;
}

void showError(QLabel *label, const QString &text)
{
    label->setText(text);
    label->setVisible(!text.isEmpty());
}

qint64 now()
{
    return QDateTime::currentSecsSinceEpoch();
}

// "ABCD EFGH IJKL …" — leichter abzutippen.
QString grouped(const QString &secret)
{
    QString out;
    for (int i = 0; i < secret.size(); ++i) {
        if (i > 0 && i % 4 == 0)
            out += QLatin1Char(' ');
        out += secret.at(i);
    }
    return out;
}

} // namespace

QImage renderQrCode(const QString &text, int moduleSize)
{
    using qrcodegen::QrCode;
    // Stufe M (15 % Fehlerkorrektur): robust genug fuer Handykameras, ohne
    // den Code fuer otpauth-Adressen unnoetig dicht zu machen.
    const QrCode code = QrCode::encodeText(text.toUtf8().constData(), QrCode::Ecc::MEDIUM);
    constexpr int quiet = 4;   // vorgeschriebene helle Randzone (Module)
    const int modules = code.getSize() + 2 * quiet;
    QImage image(modules * moduleSize, modules * moduleSize, QImage::Format_RGB32);
    // Immer schwarz auf weiss — invertierte Codes (dunkles Theme) lesen viele
    // Apps nicht.
    image.fill(Qt::white);
    for (int y = 0; y < code.getSize(); ++y)
        for (int x = 0; x < code.getSize(); ++x)
            if (code.getModule(x, y))
                for (int dy = 0; dy < moduleSize; ++dy)
                    for (int dx = 0; dx < moduleSize; ++dx)
                        image.setPixel((x + quiet) * moduleSize + dx,
                                       (y + quiet) * moduleSize + dy, qRgb(0, 0, 0));
    return image;
}

// --- Entsperren ------------------------------------------------------------

UnlockDialog::UnlockDialog(QWidget *parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("UnlockDialog"));
    setWindowTitle(_t("SSHIT-Commander entsperren"));
    setMinimumWidth(420);
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel(_t("Die App ist mit einem Passwort geschützt."), this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *form = new QFormLayout();
    m_password = passwordField(this);
    m_password->setObjectName(QStringLiteral("UnlockPassword"));
    form->addRow(_t("Passwort:"), m_password);
    if (lock::hasTwoFactor()) {
        m_code = new QLineEdit(this);
        m_code->setObjectName(QStringLiteral("UnlockCode"));
        m_code->setPlaceholderText(_t("6-stelliger Code oder Wiederherstellungscode"));
        form->addRow(_t("Bestätigungscode:"), m_code);
    }
    layout->addLayout(form);
    if (m_code && !lock::totpSecret()) {
        auto *hint = new QLabel(
            _t("Der Authenticator-Schlüssel ist auf diesem Windows-Konto nicht lesbar — "
               "bitte einen Wiederherstellungscode verwenden."), this);
        hint->setWordWrap(true);
        hint->setObjectName(QStringLiteral("Muted"));
        layout->addWidget(hint);
    }
    m_error = errorLabel(this);
    layout->addWidget(m_error);

    auto *buttons = new QHBoxLayout();
    auto *quit = new QPushButton(_t("Beenden"), this);
    connect(quit, &QPushButton::clicked, this, &QDialog::reject);
    m_unlock = new QPushButton(_t("Entsperren"), this);
    m_unlock->setDefault(true);
    connect(m_unlock, &QPushButton::clicked, this, &UnlockDialog::tryUnlock);
    buttons->addStretch(1);
    buttons->addWidget(quit);
    buttons->addWidget(m_unlock);
    layout->addLayout(buttons);

    m_cooldown = new QTimer(this);
    m_cooldown->setInterval(1000);
    connect(m_cooldown, &QTimer::timeout, this, [this] {
        if (--m_remaining > 0) {
            showError(m_error, _t("Zu viele Fehlversuche — bitte %1 s warten.").arg(m_remaining));
            return;
        }
        m_cooldown->stop();
        m_unlock->setEnabled(true);
        showError(m_error, _t("Passwort oder Code falsch."));
    });
    m_password->setFocus();
}

void UnlockDialog::tryUnlock()
{
    if (!m_unlock->isEnabled())
        return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    // Erst das Passwort; der zweite Faktor wird nur geprueft (und ein
    // Wiederherstellungscode nur verbraucht), wenn das Passwort stimmt.
    // unlock() holt zugleich den Datenschluessel fuer die Serverprofile.
    bool ok = lock::unlock(m_password->text());
    if (ok && m_code) {
        ok = lock::verifySecondFactor(m_code->text(), now());
        if (!ok)
            lock::forgetDataKey();   // ohne zweiten Faktor kein Zugriff
    }
    QApplication::restoreOverrideCursor();
    if (ok) {
        accept();
        return;
    }
    ++m_failures;
    m_password->clear();
    if (m_code)
        m_code->clear();
    m_password->setFocus();
    showError(m_error, _t("Passwort oder Code falsch."));
    // Ab dem dritten Fehlversuch: 2, 4, 8 … bis 60 Sekunden Sperre.
    if (m_throttle && m_failures >= 3)
        startCooldown(qMin(60, 2 << qMin(m_failures - 3, 5)));
}

void UnlockDialog::startCooldown(int seconds)
{
    m_remaining = seconds;
    m_unlock->setEnabled(false);
    showError(m_error, _t("Zu viele Fehlversuche — bitte %1 s warten.").arg(m_remaining));
    m_cooldown->start();
}

// --- Passwort festlegen ------------------------------------------------------

PasswordDialog::PasswordDialog(bool requireCurrent, QWidget *parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("AppLockPasswordDialog"));
    setWindowTitle(requireCurrent ? _t("App-Passwort ändern") : _t("App-Passwort festlegen"));
    setMinimumWidth(420);
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout();
    if (requireCurrent) {
        m_current = passwordField(this);
        m_current->setObjectName(QStringLiteral("CurrentPassword"));
        form->addRow(_t("Bisheriges Passwort:"), m_current);
    }
    m_new = passwordField(this);
    m_new->setObjectName(QStringLiteral("NewPassword"));
    m_repeat = passwordField(this);
    m_repeat->setObjectName(QStringLiteral("RepeatPassword"));
    form->addRow(_t("Neues Passwort:"), m_new);
    form->addRow(_t("Wiederholen:"), m_repeat);
    layout->addLayout(form);
    auto *hint = new QLabel(
        _t("Mindestens %1 Zeichen. Die Serverprofile werden mit diesem Passwort "
           "verschlüsselt — wird es vergessen, sind sie verloren.").arg(lock::kMinPasswordLength), this);
    hint->setObjectName(QStringLiteral("Muted"));
    hint->setWordWrap(true);
    layout->addWidget(hint);
    m_error = errorLabel(this);
    layout->addWidget(m_error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &PasswordDialog::apply);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void PasswordDialog::apply()
{
    if (m_current && !lock::verifyPassword(m_current->text())) {
        showError(m_error, _t("Das bisherige Passwort ist falsch."));
        return;
    }
    if (m_new->text().size() < lock::kMinPasswordLength) {
        showError(m_error, _t("Das Passwort muss mindestens %1 Zeichen haben.")
                               .arg(lock::kMinPasswordLength));
        return;
    }
    if (m_new->text() != m_repeat->text()) {
        showError(m_error, _t("Die Passwörter stimmen nicht überein."));
        return;
    }
    try {
        lock::setPassword(m_new->text(), m_current ? m_current->text() : QString());
        // Serverprofile (neu) verschluesselt ablegen — beim ersten Setzen
        // wandert die Klartext-Datei so in den verschluesselten Zustand.
        core::ProfileStore store;
        if (!store.unreadable())
            store.save();
    } catch (const std::exception &exc) {
        showError(m_error, QString::fromUtf8(exc.what()));
        return;
    }
    accept();
}

// --- Wiederherstellungscodes -------------------------------------------------

RecoveryCodesDialog::RecoveryCodesDialog(const QStringList &codes, QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("RecoveryCodesDialog"));
    setWindowTitle(_t("Wiederherstellungscodes"));
    setMinimumWidth(440);
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel(
        _t("Mit diesen Codes kommst du ohne Authenticator-App in die App — jeder Code "
           "gilt genau einmal. Sie werden nur JETZT angezeigt: kopieren oder speichern "
           "und sicher aufbewahren."), this);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto *view = new QPlainTextEdit(this);
    view->setObjectName(QStringLiteral("RecoveryCodes"));
    view->setReadOnly(true);
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(12);
    view->setFont(mono);
    view->setPlainText(codes.join(QLatin1Char('\n')));
    view->setMinimumHeight(200);
    layout->addWidget(view);

    auto *row = new QHBoxLayout();
    auto *copy = new QPushButton(_t("Kopieren"), this);
    connect(copy, &QPushButton::clicked, this,
            [codes] { QGuiApplication::clipboard()->setText(codes.join(QLatin1Char('\n'))); });
    auto *saveBtn = new QPushButton(_t("Als Datei speichern …"), this);
    connect(saveBtn, &QPushButton::clicked, this, [this, codes] {
        const QString path = getSaveFileName(this, _t("Wiederherstellungscodes speichern"),
                                             QStringLiteral("sshit-wiederherstellungscodes.txt"),
                                             _t("Textdateien (*.txt)"));
        if (path.isEmpty())
            return;
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text))
            f.write((QStringLiteral("SSHIT-Commander — ") + _t("Wiederherstellungscodes")
                     + QStringLiteral("\n\n") + codes.join(QLatin1Char('\n')) + QLatin1Char('\n'))
                        .toUtf8());
        else
            QMessageBox::warning(this, _t("Fehler"), _t("Datei nicht schreibbar."));
    });
    row->addWidget(copy);
    row->addWidget(saveBtn);
    row->addStretch(1);
    layout->addLayout(row);

    auto *saved = new QCheckBox(_t("Ich habe die Codes sicher aufbewahrt."), this);
    layout->addWidget(saved);
    auto *done = new QPushButton(_t("Fertig"), this);
    done->setEnabled(false);
    connect(saved, &QCheckBox::toggled, done, &QPushButton::setEnabled);
    connect(done, &QPushButton::clicked, this, &QDialog::accept);
    auto *doneRow = new QHBoxLayout();
    doneRow->addStretch(1);
    doneRow->addWidget(done);
    layout->addLayout(doneRow);
}

// --- Zwei-Faktor einrichten ---------------------------------------------------

TwoFactorSetupDialog::TwoFactorSetupDialog(QWidget *parent)
    : QDialog(parent), m_secret(lock::newTotpSecret())
{
    setObjectName(QStringLiteral("TwoFactorSetupDialog"));
    setWindowTitle(_t("Zwei-Faktor-Authentifizierung einrichten"));
    setMinimumWidth(520);
    auto *layout = new QVBoxLayout(this);
    const QString account = qEnvironmentVariable("USERNAME", QStringLiteral("user"));
    const QString uri = lock::otpauthUri(m_secret, account);

    auto *intro = new QLabel(
        _t("1. In der Authenticator-App (z. B. Google/Microsoft Authenticator, Aegis, "
           "1Password) ein neues Konto hinzufügen und diesen QR-Code scannen:"), this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    // QR-Code mit der otpauth://-Adresse — die App uebernimmt Name, Schluessel
    // und Einstellungen automatisch.
    auto *qr = new QLabel(this);
    qr->setObjectName(QStringLiteral("TotpQrCode"));
    qr->setAlignment(Qt::AlignCenter);
    qr->setPixmap(QPixmap::fromImage(renderQrCode(uri)));
    layout->addWidget(qr);

    auto *manual = new QLabel(_t("2. Oder stattdessen „Schlüssel eingeben“ wählen und diesen "
                                 "Schlüssel eintragen (zeitbasiert, 6 Stellen):"), this);
    manual->setWordWrap(true);
    layout->addWidget(manual);

    auto *key = new QLineEdit(grouped(m_secret), this);
    key->setObjectName(QStringLiteral("TotpSecret"));
    key->setReadOnly(true);
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(13);
    key->setFont(mono);
    layout->addWidget(key);

    auto *copyRow = new QHBoxLayout();
    auto *copyKey = new QPushButton(_t("Schlüssel kopieren"), this);
    connect(copyKey, &QPushButton::clicked, this,
            [this] { QGuiApplication::clipboard()->setText(m_secret); });
    auto *copyUri = new QPushButton(_t("otpauth-Link kopieren"), this);
    copyUri->setToolTip(_t("Für Passwort-Manager, die otpauth://-Links übernehmen"));
    connect(copyUri, &QPushButton::clicked, this,
            [uri] { QGuiApplication::clipboard()->setText(uri); });
    copyRow->addWidget(copyKey);
    copyRow->addWidget(copyUri);
    copyRow->addStretch(1);
    layout->addLayout(copyRow);

    auto *step3 = new QLabel(_t("3. Den Code, den die App jetzt anzeigt, zur Bestätigung eingeben:"), this);
    step3->setWordWrap(true);
    layout->addWidget(step3);
    m_code = new QLineEdit(this);
    m_code->setObjectName(QStringLiteral("TotpConfirm"));
    m_code->setPlaceholderText(QStringLiteral("123456"));
    m_code->setMaxLength(7);
    layout->addWidget(m_code);
    m_error = errorLabel(this);
    layout->addWidget(m_error);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto *activate = buttons->addButton(_t("Aktivieren"), QDialogButtonBox::AcceptRole);
    activate->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &TwoFactorSetupDialog::confirm);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void TwoFactorSetupDialog::confirm()
{
    if (!lock::verifyTotp(m_secret, m_code->text(), now())) {
        showError(m_error, _t("Der Code passt nicht. Uhrzeit des Handys prüfen und den "
                              "aktuellen Code eingeben."));
        return;
    }
    const QStringList codes = lock::newRecoveryCodes();
    lock::enableTwoFactor(m_secret, codes);
    RecoveryCodesDialog show(codes, this);
    show.exec();
    accept();
}

bool confirmCurrentPassword(QWidget *parent, const QString &title)
{
    bool ok = false;
    const QString password = QInputDialog::getText(parent, title, _t("Aktuelles App-Passwort:"),
                                                   QLineEdit::Password, QString(), &ok);
    if (!ok)
        return false;
    if (lock::verifyPassword(password))
        return true;
    QMessageBox::warning(parent, title, _t("Das Passwort ist falsch."));
    return false;
}

} // namespace ncssh::gui
