#include "ncssh/core/appmonitor.hpp"

#include <QFileInfo>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace ncssh::core {

#ifdef Q_OS_WIN

std::pair<quint32, QString> foregroundProcess()
{
    const HWND hwnd = GetForegroundWindow();
    if (!hwnd)
        return {0, {}};
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid)
        return {0, {}};

    const HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!handle)
        return {pid, {}};
    wchar_t buf[32768];
    DWORD size = static_cast<DWORD>(std::size(buf));
    QString exe;
    if (QueryFullProcessImageNameW(handle, 0, buf, &size))
        exe = QFileInfo(QString::fromWCharArray(buf, size)).fileName().toLower();
    CloseHandle(handle);
    return {pid, exe};
}

#else

std::pair<quint32, QString> foregroundProcess() { return {0, {}}; }

#endif

} // namespace ncssh::core
