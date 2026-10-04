// GUI-Einstiegspunkt: QApplication + Async-Bruecke + Hauptfenster.
#include "ncssh/gui/app.hpp"

#include "ncssh/core/applock.hpp"
#include "ncssh/core/assets.hpp"
#include "ncssh/core/i18n.hpp"
#include "ncssh/core/profiles.hpp"
#include "ncssh/core/settings.hpp"
#include "ncssh/gui/applock_dialogs.hpp"
#include "ncssh/gui/bridge.hpp"
#include "ncssh/gui/crash_handler.hpp"
#include "ncssh/gui/main_window.hpp"
#include "ncssh/gui/style.hpp"
#include "ncssh/gui/view_state.hpp"

#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QMessageBox>
#include <QPixmap>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <shobjidl.h>
#endif

namespace ncssh::gui {

// Eigene AppUserModelID setzen, damit Windows-Benachrichtigungen als
// "SSHIT-Commander" erscheinen statt unter der EXE-Identitaet.
static void setWindowsAppId()
{
#ifdef Q_OS_WIN
    SetCurrentProcessExplicitAppUserModelID(L"SSHIT-Commander");
#endif
}

// QApplication mit Sicherheitsnetz: Eine Ausnahme, die aus einem Slot oder
// Event-Handler entkommt, beendete bisher sofort die ganze App (so beim Ziehen
// der Trennlinie, als settings.json gesperrt war). Hier wird sie abgefangen,
// protokolliert und gemeldet; die App laeuft weiter.
class SafeApplication : public QApplication {
public:
    using QApplication::QApplication;

    bool notify(QObject *receiver, QEvent *event) override
    {
        try {
            return QApplication::notify(receiver, event);
        } catch (const std::exception &exc) {
            reportCaughtException(QString::fromUtf8(exc.what()));
        } catch (...) {
            reportCaughtException(QStringLiteral("Unbekannte Ausnahme"));
        }
        return false;
    }
};

int appMain(int argc, char *argv[])
{
    // Absturzberichte zuerst — auch Fehler beim Start sollen einen erzeugen.
    installCrashHandler();
    setWindowsAppId();
    SafeApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("SSHIT-Commander"));
    app.setApplicationVersion(QString::fromLatin1(SSHIT_VERSION));
    // Bewusst KEIN applicationDisplayName: Qt haengt ihn sonst an jeden
    // Fenstertitel an — das Hauptfenster fuehrt Name und Version bereits selbst.

    ncssh::core::setLanguage(ncssh::core::getSettingString(
        QStringLiteral("language"), QStringLiteral("de")));  // vor dem UI-Aufbau
    applyTheme(&app, ncssh::core::getSettingString(QStringLiteral("theme"),
                                                   defaultTheme()));

    const QString iconPath = ncssh::core::assetPath(QStringLiteral("sshit.png"));
    if (!iconPath.isEmpty())
        app.setWindowIcon(QIcon(QPixmap(iconPath)));

    // Fenstergroessen und Spaltenansichten aller Dialoge merken/wiederherstellen.
    ViewStateKeeper::install();

    // App-Sperre: vor allem anderen (Hauptfenster, Sitzungs-Wiederherstellung,
    // Auto-Verbinden) Passwort und ggf. Code abfragen. Abbrechen beendet.
    if (ncssh::core::applock::isEnabled()) {
        UnlockDialog unlock;
        if (unlock.exec() != QDialog::Accepted)
            return 0;
    }
    // Verschluesselte Serverprofile, die sich nicht oeffnen lassen (Sperre
    // entfernt bzw. applock.json ersetzt): beiseitelegen statt die App mit
    // einer gesperrten Profilliste zu blockieren.
    const QString lockedProfiles = ncssh::core::setAsideUnreadableProfiles();
    if (!lockedProfiles.isEmpty())
        QMessageBox::warning(
            nullptr, QStringLiteral("SSHIT-Commander"),
            ncssh::core::_t("Die Serverprofile sind verschlüsselt, lassen sich aber nicht "
                            "entschlüsseln (die App-Sperre wurde entfernt oder ersetzt).\n\n"
                            "Die Datei wurde gesichert als:\n%1\n\nEs geht mit einer leeren "
                            "Profilliste weiter.").arg(lockedProfiles));

    AsyncBridge bridge;
    bridge.start();

    MainWindow window(&bridge);
    window.show();
    // Gab es beim letzten Mal einen Absturz? Dann auf den Bericht hinweisen.
    reportPreviousCrash(&window);

    const int exitCode = app.exec();
    bridge.stop();
    // Beim Beenden die zum Ansehen/Bearbeiten heruntergeladenen Remote-Dateien
    // aus dem Temp-Ordner entfernen — sie sollen nicht im Klartext liegen
    // bleiben (waehrend der Sitzung braucht der Editor sie noch).
    QDir(QDir::tempPath() + QStringLiteral("/sshit-open")).removeRecursively();
    return exitCode;
}

} // namespace ncssh::gui
