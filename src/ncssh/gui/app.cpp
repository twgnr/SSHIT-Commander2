// GUI-Einstiegspunkt: QApplication + Async-Bruecke + Hauptfenster.
// (Port von gui/app.py)
#include "ncssh/gui/app.hpp"

#include "ncssh/core/assets.hpp"
#include "ncssh/core/i18n.hpp"
#include "ncssh/core/settings.hpp"
#include "ncssh/gui/bridge.hpp"
#include "ncssh/gui/main_window.hpp"
#include "ncssh/gui/style.hpp"

#include <QApplication>
#include <QIcon>
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

int appMain(int argc, char *argv[])
{
    setWindowsAppId();
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("SSHIT-Commander"));
    app.setApplicationDisplayName(QStringLiteral("SSHIT-Commander"));

    ncssh::core::setLanguage(ncssh::core::getSettingString(
        QStringLiteral("language"), QStringLiteral("de")));  // vor dem UI-Aufbau
    applyTheme(&app, ncssh::core::getSettingString(QStringLiteral("theme"),
                                                   defaultTheme()));

    const QString iconPath = ncssh::core::assetPath(QStringLiteral("sshit.png"));
    if (!iconPath.isEmpty())
        app.setWindowIcon(QIcon(QPixmap(iconPath)));

    AsyncBridge bridge;
    bridge.start();

    MainWindow window(&bridge);
    window.show();

    const int exitCode = app.exec();
    bridge.stop();
    return exitCode;
}

} // namespace ncssh::gui
