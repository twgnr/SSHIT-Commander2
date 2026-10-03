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

quintptr foregroundWindowHandle()
{
    return reinterpret_cast<quintptr>(GetForegroundWindow());
}

bool isOwnProcessWindow(quintptr window)
{
    if (!window)
        return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(reinterpret_cast<HWND>(window), &pid);
    return pid == GetCurrentProcessId();
}

bool bringWindowToFront(quintptr window)
{
    const HWND hwnd = reinterpret_cast<HWND>(window);
    if (!hwnd || !IsWindow(hwnd))
        return false;
    if (IsIconic(hwnd))
        ShowWindow(hwnd, SW_RESTORE);
    return SetForegroundWindow(hwnd) != FALSE;
}

#else

std::pair<quint32, QString> foregroundProcess() { return {0, {}}; }
quintptr foregroundWindowHandle() { return 0; }
bool isOwnProcessWindow(quintptr) { return false; }
bool bringWindowToFront(quintptr) { return false; }

#endif

} // namespace ncssh::core
