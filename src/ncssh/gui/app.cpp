// GUI-Einstiegspunkt.  (Port von gui/app.py)
// HINWEIS: Uebergangsversion fuer den Skelett-Build — wird durch den vollen
// Port ersetzt, sobald MainWindow und Style portiert sind.
#include "ncssh/gui/app.hpp"

#include "ncssh/core/i18n.hpp"
#include "ncssh/core/settings.hpp"
#include "ncssh/gui/bridge.hpp"

#include <QApplication>
#include <QMainWindow>

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

int appMain(int argc, char *argv[])
{
    setWindowsAppId();
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("SSHIT-Commander"));
    app.setApplicationDisplayName(QStringLiteral("SSHIT-Commander"));
    ncssh::core::setLanguage(ncssh::core::getSettingString(
        QStringLiteral("language"), QStringLiteral("de")));  // vor dem UI-Aufbau

    AsyncBridge bridge;
    bridge.start();

    QMainWindow window;  // TODO: MainWindow(bridge) nach Port von main_window.py
    window.setWindowTitle(QStringLiteral("SSHIT-Commander"));
    window.show();

    const int exitCode = app.exec();
    bridge.stop();
    return exitCode;
}

} // namespace ncssh::gui
