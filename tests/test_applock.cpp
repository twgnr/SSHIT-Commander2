// App-Sperre: Passwort-Hash, TOTP (RFC 6238), Base32 (RFC 4648),
// Wiederherstellungscodes und der Entsperr-Dialog.
#include "tests/harness.hpp"

#include "ncssh/core/applock.hpp"
#include "ncssh/core/configio.hpp"
#include "ncssh/gui/applock_dialogs.hpp"

#include <QColor>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>

using namespace ncssh;
namespace lock = ncssh::core::applock;

namespace {

// Sperrdaten in ein Temp-Verzeichnis isolieren (nie die echte applock.json).
class IsolatedConfig {
public:
    IsolatedConfig() : m_old(qgetenv("APPDATA"))
    {
        qputenv("APPDATA", m_dir.path().toUtf8());
        lock::forgetDataKey();   // Datenschluessel ist prozessweit
    }
    ~IsolatedConfig()
    {
        lock::forgetDataKey();
        qputenv("APPDATA", m_old);
    }
    bool valid() const { return m_dir.isValid(); }

private:
    QTemporaryDir m_dir;
    QByteArray m_old;
};

QPushButton *buttonWithText(QWidget *parent, const QString &text)
{
    for (QPushButton *b : parent->findChildren<QPushButton *>())
        if (b->text() == text)
            return b;
    return nullptr;
}

} // namespace

TEST(applock, totp_matches_rfc6238_vectors)
{
    const QByteArray key("12345678901234567890");
    CHECK_EQ(lock::totpCode(key, 59, 8), QStringLiteral("94287082"));
    CHECK_EQ(lock::totpCode(key, 1111111109, 8), QStringLiteral("07081804"));
    CHECK_EQ(lock::totpCode(key, 1234567890, 8), QStringLiteral("89005924"));
    CHECK_EQ(lock::totpCode(key, 2000000000, 8), QStringLiteral("69279037"));
    CHECK_EQ(lock::totpCode(key, 59), QStringLiteral("287082"));   // 6 Stellen
}

TEST(applock, base32_matches_rfc4648)
{
    CHECK_EQ(lock::base32Encode("f"), QStringLiteral("MY"));
    CHECK_EQ(lock::base32Encode("fo"), QStringLiteral("MZXQ"));
    CHECK_EQ(lock::base32Encode("foobar"), QStringLiteral("MZXW6YTBOI"));
    CHECK_EQ(lock::base32Decode(QStringLiteral("MZXW6YTBOI======")), QByteArray("foobar"));
    CHECK_EQ(lock::base32Decode(QStringLiteral("mzxw 6ytb oi")), QByteArray("foobar"));
    CHECK(lock::base32Decode(QStringLiteral("MZ1!")).isEmpty());   // ungueltig
    const QString secret = lock::newTotpSecret();
    CHECK_EQ(lock::base32Decode(secret).size(), 20);
}

TEST(applock, totp_accepts_one_step_of_clock_drift)
{
    const QString secret = lock::base32Encode("12345678901234567890");
    const QByteArray key = lock::base32Decode(secret);
    const qint64 t = 1111111109;
    CHECK(lock::verifyTotp(secret, lock::totpCode(key, t), t));
    CHECK(lock::verifyTotp(secret, lock::totpCode(key, t - 30), t));   // Handy leicht nach
    CHECK(lock::verifyTotp(secret, lock::totpCode(key, t + 30), t));   // … oder vor
    CHECK(!lock::verifyTotp(secret, lock::totpCode(key, t - 90), t));
    CHECK(!lock::verifyTotp(secret, QStringLiteral("12345"), t));
    CHECK(lock::otpauthUri(secret, QStringLiteral("tobias"))
              .startsWith(QStringLiteral("otpauth://totp/SSHIT-Commander:tobias?secret=")));
}

TEST(applock, password_is_hashed_and_verified)
{
    IsolatedConfig cfg;
    CHECK(cfg.valid());
    CHECK(!lock::isEnabled());
    lock::setPassword(QStringLiteral("geheim-123"));
    CHECK(lock::isEnabled());
    CHECK(!lock::hasTwoFactor());
    QElapsedTimer timer;
    timer.start();
    CHECK(lock::verifyPassword(QStringLiteral("geheim-123")));
    const qint64 ms = timer.elapsed();
    CHECK(ms < 5000);   // PBKDF2 bremst Raten, darf den Start aber nicht laehmen
    CHECK(!lock::verifyPassword(QStringLiteral("geheim-124")));
    CHECK(!lock::verifyPassword(QString()));
    // Klartext steht nirgends in der Datei.
    QFile f(lock::lockFile());
    CHECK(f.open(QIODevice::ReadOnly));
    CHECK(!f.readAll().contains("geheim-123"));
    f.close();
    // Nicht Teil des Konfigurations-Exports (Import kann die Sperre nicht kippen).
    const QJsonObject files = core::buildBundle().value(QStringLiteral("files")).toObject();
    for (auto it = files.begin(); it != files.end(); ++it)
        CHECK(!it.key().contains(QStringLiteral("lock")));
    lock::disable();
    CHECK(!lock::isEnabled());
}

TEST(applock, second_factor_totp_once_and_recovery_codes)
{
    IsolatedConfig cfg;
    CHECK(cfg.valid());
    lock::setPassword(QStringLiteral("geheim-123"));
    const QString secret = lock::newTotpSecret();
    const QStringList codes = lock::newRecoveryCodes();
    CHECK_EQ(codes.size(), 8);
    CHECK(codes.first().contains(QLatin1Char('-')));
    lock::enableTwoFactor(secret, codes);
    CHECK(lock::hasTwoFactor());
    CHECK(lock::totpSecret().has_value());
    CHECK_EQ(lock::totpSecret().value_or(QString()), secret);   // DPAPI hin und zurueck

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const QString code = lock::totpCode(lock::base32Decode(secret), now);
    CHECK(lock::verifySecondFactor(code, now));
    CHECK(!lock::verifySecondFactor(code, now));   // derselbe Code nicht zweimal
    CHECK(!lock::verifySecondFactor(QStringLiteral("000000"), now + 3600));

    // Wiederherstellungscode: Schreibweise egal, aber nur einmal gueltig.
    QString sloppy = codes.at(2).toLower();
    sloppy.remove(QLatin1Char('-'));
    CHECK(lock::verifySecondFactor(sloppy, now));
    CHECK_EQ(lock::remainingRecoveryCodes(), 7);
    CHECK(!lock::verifySecondFactor(codes.at(2), now));

    // Passwort aendern laesst 2FA bestehen.
    lock::setPassword(QStringLiteral("neues-passwort"));
    CHECK(lock::hasTwoFactor());
    CHECK(lock::verifySecondFactor(codes.at(3), now));
    lock::disableTwoFactor();
    CHECK(!lock::hasTwoFactor());
    CHECK(lock::isEnabled());
}

TEST(applock, unlock_dialog_requires_password_and_code)
{
    IsolatedConfig cfg;
    CHECK(cfg.valid());
    lock::setPassword(QStringLiteral("geheim-123"));
    {
        gui::UnlockDialog dlg;
        dlg.setThrottleEnabled(false);
        auto *pw = dlg.findChild<QLineEdit *>(QStringLiteral("UnlockPassword"));
        CHECK(pw != nullptr);
        CHECK(dlg.findChild<QLineEdit *>(QStringLiteral("UnlockCode")) == nullptr);
        QPushButton *unlock = buttonWithText(&dlg, QStringLiteral("Entsperren"));
        CHECK(unlock != nullptr);
        pw->setText(QStringLiteral("falsch-123"));
        unlock->click();
        CHECK(dlg.result() != QDialog::Accepted);
        pw->setText(QStringLiteral("geheim-123"));
        unlock->click();
        CHECK(dlg.result() == QDialog::Accepted);
    }

    const QString secret = lock::newTotpSecret();
    lock::enableTwoFactor(secret, lock::newRecoveryCodes());
    {
        gui::UnlockDialog dlg;
        dlg.setThrottleEnabled(false);
        auto *pw = dlg.findChild<QLineEdit *>(QStringLiteral("UnlockPassword"));
        auto *code = dlg.findChild<QLineEdit *>(QStringLiteral("UnlockCode"));
        CHECK(pw && code);
        QPushButton *unlock = buttonWithText(&dlg, QStringLiteral("Entsperren"));
        pw->setText(QStringLiteral("geheim-123"));
        code->setText(QStringLiteral("000000"));
        unlock->click();
        CHECK(dlg.result() != QDialog::Accepted);   // Passwort allein reicht nicht
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        pw->setText(QStringLiteral("geheim-123"));
        code->setText(lock::totpCode(lock::base32Decode(secret), now));
        unlock->click();
        CHECK(dlg.result() == QDialog::Accepted);
    }
}

TEST(applock, unlock_dialog_throttles_after_three_failures)
{
    IsolatedConfig cfg;
    CHECK(cfg.valid());
    lock::setPassword(QStringLiteral("geheim-123"));
    gui::UnlockDialog dlg;
    auto *pw = dlg.findChild<QLineEdit *>(QStringLiteral("UnlockPassword"));
    QPushButton *unlock = buttonWithText(&dlg, QStringLiteral("Entsperren"));
    for (int i = 0; i < 3; ++i) {
        pw->setText(QStringLiteral("falsch"));
        unlock->click();
    }
    CHECK(!unlock->isEnabled());   // Wartezeit laeuft
    pw->setText(QStringLiteral("geheim-123"));
    unlock->click();
    CHECK(dlg.result() != QDialog::Accepted);   // auch richtig erst nach der Pause
}

TEST(applock, password_dialog_validates)
{
    IsolatedConfig cfg;
    CHECK(cfg.valid());
    gui::PasswordDialog dlg(false);
    auto *pw = dlg.findChild<QLineEdit *>(QStringLiteral("NewPassword"));
    auto *rep = dlg.findChild<QLineEdit *>(QStringLiteral("RepeatPassword"));
    auto *box = dlg.findChild<QDialogButtonBox *>();
    CHECK(pw && rep && box);
    pw->setText(QStringLiteral("kurz"));
    rep->setText(QStringLiteral("kurz"));
    emit box->accepted();
    CHECK(!lock::isEnabled());
    pw->setText(QStringLiteral("lang-genug-1"));
    rep->setText(QStringLiteral("lang-genug-2"));
    emit box->accepted();
    CHECK(!lock::isEnabled());
    rep->setText(QStringLiteral("lang-genug-1"));
    emit box->accepted();
    CHECK(lock::isEnabled());
    CHECK(lock::verifyPassword(QStringLiteral("lang-genug-1")));
}

TEST(applock, setup_dialog_shows_scannable_qr_code)
{
    IsolatedConfig cfg;
    CHECK(cfg.valid());
    const QImage img = gui::renderQrCode(QStringLiteral("otpauth://totp/SSHIT-Commander:tobias?secret=ABC"), 5);
    CHECK(!img.isNull());
    CHECK_EQ(img.width(), img.height());
    CHECK_EQ(img.width() % 5, 0);
    // Helle Randzone aussen, Suchmuster (dunkel) direkt dahinter oben links.
    CHECK(img.pixelColor(2, 2) == QColor(Qt::white));
    CHECK(img.pixelColor(4 * 5 + 2, 4 * 5 + 2) == QColor(Qt::black));

    lock::setPassword(QStringLiteral("geheim-123"));
    gui::TwoFactorSetupDialog dlg;
    auto *qr = dlg.findChild<QLabel *>(QStringLiteral("TotpQrCode"));
    CHECK(qr != nullptr);
    CHECK(qr && !qr->pixmap().isNull());
}
