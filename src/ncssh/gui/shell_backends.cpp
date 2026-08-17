#include "ncssh/gui/shell_backends.hpp"

#include <QDir>
#include <QMetaObject>
#include <chrono>
#include <thread>

#ifdef Q_OS_WIN
#  include <windows.h>
#else
#  include <cstdlib>
#endif

namespace ncssh::gui {

// ---------------------------------------------------------------------------
// LocalShellBackend — Windows ConPTY
// ---------------------------------------------------------------------------

struct LocalShellBackend::Impl {
#ifdef Q_OS_WIN
    HPCON hpc = nullptr;
    HANDLE inWrite = nullptr;   // wir schreiben -> Shell liest
    HANDLE outRead = nullptr;   // Shell schreibt -> wir lesen
    PROCESS_INFORMATION pi{};
    LPPROC_THREAD_ATTRIBUTE_LIST attrList = nullptr;
#endif
};

LocalShellBackend::LocalShellBackend(QObject *parent)
    : ShellBackend(parent), m_impl(std::make_unique<Impl>())
{
}

LocalShellBackend::~LocalShellBackend()
{
    close();
    if (m_thread.joinable())
        m_thread.join();
}

void LocalShellBackend::start(int cols, int rows)
{
#ifdef Q_OS_WIN
    HANDLE inRead = nullptr, outWrite = nullptr;
    if (!CreatePipe(&inRead, &m_impl->inWrite, nullptr, 0)
        || !CreatePipe(&m_impl->outRead, &outWrite, nullptr, 0)) {
        emit dataReceived(QStringLiteral("\r\n\x1b[31m[PTY-Pipes fehlgeschlagen]\x1b[0m\r\n"));
        emit closed();
        return;
    }

    COORD size;
    size.X = static_cast<SHORT>(qMax(2, cols));
    size.Y = static_cast<SHORT>(qMax(2, rows));
    if (FAILED(CreatePseudoConsole(size, inRead, outWrite, 0, &m_impl->hpc))) {
        emit dataReceived(QStringLiteral("\r\n\x1b[31m[ConPTY nicht verfügbar]\x1b[0m\r\n"));
        emit closed();
        return;
    }
    // Die an die ConPTY uebergebenen Enden werden nicht mehr gebraucht.
    CloseHandle(inRead);
    CloseHandle(outWrite);

    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(STARTUPINFOEXW);
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    m_impl->attrList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
        HeapAlloc(GetProcessHeap(), 0, bytes));
    InitializeProcThreadAttributeList(m_impl->attrList, 1, 0, &bytes);
    UpdateProcThreadAttribute(m_impl->attrList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                              m_impl->hpc, sizeof(HPCON), nullptr, nullptr);
    si.lpAttributeList = m_impl->attrList;

    QString shell = qEnvironmentVariable("COMSPEC");
    if (shell.isEmpty())
        shell = QStringLiteral("cmd.exe");
    std::wstring cmdline(reinterpret_cast<const wchar_t *>(shell.utf16()));
    std::vector<wchar_t> mutableCmd(cmdline.begin(), cmdline.end());
    mutableCmd.push_back(L'\0');

    if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
                        EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
                        &si.StartupInfo, &m_impl->pi)) {
        emit dataReceived(QStringLiteral("\r\n\x1b[31m[Shell konnte nicht gestartet werden]\x1b[0m\r\n"));
        emit closed();
        return;
    }

    m_alive = true;
    m_thread = std::thread([this] { readLoop(); });
#else
    Q_UNUSED(cols);
    Q_UNUSED(rows);
    emit dataReceived(QStringLiteral("\r\n\x1b[31m[Lokales PTY nur unter Windows]\x1b[0m\r\n"));
    emit closed();
#endif
}

void LocalShellBackend::readLoop()
{
#ifdef Q_OS_WIN
    char buf[4096];
    DWORD read = 0;
    while (m_alive.load()) {
        if (!ReadFile(m_impl->outRead, buf, sizeof(buf), &read, nullptr) || read == 0)
            break;
        const QString text = QString::fromUtf8(buf, static_cast<int>(read));
        QMetaObject::invokeMethod(this, [this, text] { emit dataReceived(text); },
                                  Qt::QueuedConnection);
    }
    QMetaObject::invokeMethod(this, [this] { emit closed(); }, Qt::QueuedConnection);
#endif
}

void LocalShellBackend::write(const QString &text)
{
#ifdef Q_OS_WIN
    if (!m_alive.load() || !m_impl->inWrite)
        return;
    const QByteArray data = text.toUtf8();
    DWORD written = 0;
    WriteFile(m_impl->inWrite, data.constData(), static_cast<DWORD>(data.size()),
              &written, nullptr);
#else
    Q_UNUSED(text);
#endif
}

void LocalShellBackend::resize(int cols, int rows)
{
#ifdef Q_OS_WIN
    if (m_impl->hpc) {
        COORD size;
        size.X = static_cast<SHORT>(qMax(2, cols));
        size.Y = static_cast<SHORT>(qMax(2, rows));
        ResizePseudoConsole(m_impl->hpc, size);
    }
#else
    Q_UNUSED(cols);
    Q_UNUSED(rows);
#endif
}

void LocalShellBackend::close()
{
    if (m_closing.exchange(true))
        return;
#ifdef Q_OS_WIN
    // Reihenfolge gegen den dokumentierten ConPTY-Deadlock: erst den Client
    // beenden, dann die Konsole schliessen, WAEHREND der Lesethread die
    // Ausgabe-Pipe weiter leert (m_alive bleibt so lange true) —
    // ClosePseudoConsole blockiert sonst endlos, wenn ungelesene Ausgabe
    // ansteht. Genau das liess den Prozess nach dem Schliessen des Fensters
    // als fensterlosen Zombie weiterleben. Erst danach den Thread joinen und
    // zuletzt die Handles freigeben.
    if (m_impl->pi.hProcess) {
        TerminateProcess(m_impl->pi.hProcess, 0);
        CloseHandle(m_impl->pi.hProcess);
        CloseHandle(m_impl->pi.hThread);
        m_impl->pi = {};
    }
    if (m_impl->hpc) {
        ClosePseudoConsole(m_impl->hpc);  // Lesethread draint parallel weiter
        m_impl->hpc = nullptr;
    }
    m_alive = false;   // Pipe laeuft leer, ReadFile liefert EOF -> Thread endet
    if (m_thread.joinable())
        m_thread.join();
    if (m_impl->inWrite) {
        CloseHandle(m_impl->inWrite);
        m_impl->inWrite = nullptr;
    }
    if (m_impl->outRead) {
        CloseHandle(m_impl->outRead);
        m_impl->outRead = nullptr;
    }
    if (m_impl->attrList) {
        DeleteProcThreadAttributeList(m_impl->attrList);
        HeapFree(GetProcessHeap(), 0, m_impl->attrList);
        m_impl->attrList = nullptr;
    }
#else
    m_alive = false;
#endif
}

// ---------------------------------------------------------------------------
// RemoteShellBackend — interaktiver SSH-Shell-Channel
// ---------------------------------------------------------------------------

RemoteShellBackend::RemoteShellBackend(AsyncBridge *bridge, QObject *parent)
    : ShellBackend(parent), m_bridge(bridge)
{
}

RemoteShellBackend::~RemoteShellBackend()
{
    close();
    if (m_thread.joinable())
        m_thread.join();
}

void RemoteShellBackend::start(const net::SSHSessionPtr &session, int cols, int rows)
{
    try {
        m_shell = net::RemoteShell::open(session, cols, rows);
    } catch (const std::exception &exc) {
        emit dataReceived(QStringLiteral("\r\n\x1b[31m[Shell konnte nicht gestartet werden: %1]\x1b[0m\r\n")
                              .arg(QString::fromUtf8(exc.what())));
        emit closed();
        return;
    }
    m_alive = true;
    m_thread = std::thread([this] {
        while (m_alive.load()) {
            QByteArray data;
            try {
                data = m_shell->read(32768, 100);
            } catch (const std::exception &exc) {
                if (m_alive.load()) {
                    const QString msg = QString::fromUtf8(exc.what());
                    QMetaObject::invokeMethod(this, [this, msg] {
                        emit dataReceived(QStringLiteral("\r\n\x1b[31m[Verbindung unterbrochen: %1]\x1b[0m\r\n").arg(msg));
                    }, Qt::QueuedConnection);
                }
                break;
            }
            if (data.isEmpty())
                continue;
            const QString text = QString::fromUtf8(data);
            QMetaObject::invokeMethod(this, [this, text] { emit dataReceived(text); },
                                      Qt::QueuedConnection);
            // Winzige Pause zwischen vollen Puffern: der Windows-Mutex ist
            // nicht fair, bei stroemender Terminalausgabe wuerde die Schleife
            // sonst alle SFTP-Jobs (Listings, Transfers) verhungern lassen.
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        QMetaObject::invokeMethod(this, [this] { emit closed(); }, Qt::QueuedConnection);
    });
}

void RemoteShellBackend::write(const QString &text)
{
    if (m_alive.load() && m_shell)
        m_shell->write(text.toUtf8());
}

void RemoteShellBackend::resize(int cols, int rows)
{
    if (m_alive.load() && m_shell)
        m_shell->resize(cols, rows);
}

void RemoteShellBackend::close()
{
    if (!m_alive.exchange(false))
        return;
    if (m_shell)
        m_shell->close();
    // Lesethread SOFORT einsammeln (nicht erst im Destruktor): das Backend
    // wird per deleteLater entsorgt, und beim App-Ende laeuft keine
    // Ereignisschleife mehr, die es zustellen wuerde — der Thread bliebe
    // sonst ungejoint auf der gerade schliessenden Session zurueck.
    if (m_thread.joinable() && m_thread.get_id() != std::this_thread::get_id())
        m_thread.join();
}

} // namespace ncssh::gui
