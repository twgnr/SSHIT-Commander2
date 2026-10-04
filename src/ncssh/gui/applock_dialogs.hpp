// Dialoge der App-Sperre: Entsperren beim Start, Passwort setzen/aendern,
// Zwei-Faktor einrichten und Wiederherstellungscodes anzeigen.
#pragma once

#include <QDialog>
#include <QImage>
#include <QStringList>

class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;

namespace ncssh::gui {

// Beim Start: Passwort (und ggf. Code) abfragen. Rejected = App beenden.
// Nach drei Fehlversuchen wachsende Wartezeit gegen Durchprobieren.
class UnlockDialog : public QDialog {
    Q_OBJECT
public:
    explicit UnlockDialog(QWidget *parent = nullptr);

    // Fuer Tests: Wartezeit nach Fehlversuchen abschalten.
    void setThrottleEnabled(bool enabled) { m_throttle = enabled; }

private:
    void tryUnlock();
    void startCooldown(int seconds);

    QLineEdit *m_password = nullptr;
    QLineEdit *m_code = nullptr;
    QLabel *m_error = nullptr;
    QPushButton *m_unlock = nullptr;
    QTimer *m_cooldown = nullptr;
    int m_remaining = 0;
    int m_failures = 0;
    bool m_throttle = true;
};

// Neues Passwort festlegen; mit requireCurrent zuerst das bisherige.
class PasswordDialog : public QDialog {
    Q_OBJECT
public:
    explicit PasswordDialog(bool requireCurrent, QWidget *parent = nullptr);

private:
    void apply();

    QLineEdit *m_current = nullptr;
    QLineEdit *m_new = nullptr;
    QLineEdit *m_repeat = nullptr;
    QLabel *m_error = nullptr;
};

// Wiederherstellungscodes einmalig anzeigen (kopieren/speichern); "Fertig"
// erst nach Bestaetigung, dass sie gesichert sind.
class RecoveryCodesDialog : public QDialog {
    Q_OBJECT
public:
    explicit RecoveryCodesDialog(const QStringList &codes, QWidget *parent = nullptr);
};

// Authenticator-App verbinden: Schluessel anzeigen, mit einem Code bestaetigen,
// danach Wiederherstellungscodes zeigen.
class TwoFactorSetupDialog : public QDialog {
    Q_OBJECT
public:
    explicit TwoFactorSetupDialog(QWidget *parent = nullptr);

    QString secret() const { return m_secret; }   // fuer Tests

private:
    void confirm();

    QString m_secret;
    QLineEdit *m_code = nullptr;
    QLabel *m_error = nullptr;
};

// QR-Code als Bild (schwarz auf weiss, mit Randzone), z. B. fuer otpauth://.
QImage renderQrCode(const QString &text, int moduleSize = 5);

// Bisheriges Passwort abfragen (z. B. vor dem Abschalten). true = korrekt.
bool confirmCurrentPassword(QWidget *parent, const QString &title);

} // namespace ncssh::gui
