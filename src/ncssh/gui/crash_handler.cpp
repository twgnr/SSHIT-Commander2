#include "ncssh/gui/crash_handler.hpp"

#include "ncssh/config.hpp"
#include "ncssh/core/i18n.hpp"
#include "ncssh/core/settings.hpp"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QUrl>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <dbghelp.h>
#  include <csignal>
#  include <cstdint>
#  include <cstdio>
#  include <cstdlib>
#  include <cstring>
#  include <cwchar>
#  include <exception>
#  include <string>
#endif

namespace ncssh::gui {

using core::_t;

QString crashDirectory()
{
    return ncssh::configDir() + QStringLiteral("/crashes");
}

#ifdef Q_OS_WIN

namespace {

// Alles, was der Handler braucht, liegt vorab bereit — im Absturz wird
// nichts mehr dynamisch angelegt (der Heap kann defekt sein).
wchar_t g_dir[MAX_PATH] = {};
char g_version[64] = {};
char g_lastMessage[1024] = {};   // letzte qFatal/qCritical-Meldung
QtMessageHandler g_prevHandler = nullptr;

constexpr DWORD kAbortCode = 0xE0000AB0;       // abort()/qFatal
constexpr DWORD kTerminateCode = 0xE0000AB1;   // std::terminate (unbehandelte C++-Ausnahme)
constexpr DWORD kPurecallCode = 0xE0000AB2;
constexpr DWORD kInvalidParamCode = 0xE0000AB3;

struct CrashJob {
    EXCEPTION_POINTERS *pointers;
    DWORD threadId;
};

const char *describe(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "Zugriffsverletzung (access violation)";
    case EXCEPTION_STACK_OVERFLOW: return "Stack-Ueberlauf (Endlosrekursion?)";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "Division durch null";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "Ungueltige Anweisung";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "Feldgrenze ueberschritten";
    case EXCEPTION_IN_PAGE_ERROR: return "Seitenfehler beim Lesen";
    case kAbortCode: return "abort() / qFatal";
    case kTerminateCode: return "std::terminate (unbehandelte Ausnahme)";
    case kPurecallCode: return "Aufruf einer rein virtuellen Funktion";
    case kInvalidParamCode: return "Ungueltiger CRT-Parameter";
    default: return "Unbekannt";
    }
}

// "modul.dll+0x1234 Symbol" fuer eine Adresse.
void describeAddress(HANDLE process, DWORD64 address, char *out, size_t size)
{
    HMODULE module = nullptr;
    char name[MAX_PATH] = "?";
    DWORD64 offset = address;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                               | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(address), &module)
        && module) {
        char path[MAX_PATH] = {};
        GetModuleFileNameA(module, path, MAX_PATH);
        const char *base = std::strrchr(path, '\\');
        std::snprintf(name, sizeof(name), "%s", base ? base + 1 : path);
        offset = address - reinterpret_cast<DWORD64>(module);
    }
    alignas(SYMBOL_INFO) char symBuf[sizeof(SYMBOL_INFO) + 256] = {};
    auto *sym = reinterpret_cast<SYMBOL_INFO *>(symBuf);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;
    DWORD64 displacement = 0;
    if (SymFromAddr(process, address, &displacement, sym))
        std::snprintf(out, size, "%s+0x%llx  (%s+0x%llx)", name, offset, sym->Name, displacement);
    else
        std::snprintf(out, size, "%s+0x%llx", name, offset);
}

// Laeuft in einem eigenen Thread: bei einem Stack-Ueberlauf hat der
// abgestuerzte Thread selbst keinen Platz mehr fuer diese Arbeit.
DWORD WINAPI writeReport(LPVOID param)
{
    const auto *job = static_cast<const CrashJob *>(param);
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t base[MAX_PATH];
    std::swprintf(base, MAX_PATH, L"%ls\\crash-%04u%02u%02u-%02u%02u%02u", g_dir, t.wYear, t.wMonth,
                  t.wDay, t.wHour, t.wMinute, t.wSecond);
    wchar_t dmpPath[MAX_PATH], txtPath[MAX_PATH];
    std::swprintf(dmpPath, MAX_PATH, L"%ls.dmp", base);
    std::swprintf(txtPath, MAX_PATH, L"%ls.txt", base);

    HANDLE process = GetCurrentProcess();
    // 1) Minidump (Threads, Stacks, referenzierter Speicher).
    HANDLE dmp = CreateFileW(dmpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (dmp != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION info{job->threadId, job->pointers, FALSE};
        MiniDumpWriteDump(process, GetCurrentProcessId(), dmp,
                          MINIDUMP_TYPE(MiniDumpWithIndirectlyReferencedMemory
                                        | MiniDumpScanMemory | MiniDumpWithThreadInfo),
                          &info, nullptr, nullptr);
        CloseHandle(dmp);
    }

    // 2) Lesbare Kurzfassung mit Aufrufkette (Modul+Offset).
    HANDLE txt = CreateFileW(txtPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (txt == INVALID_HANDLE_VALUE)
        return 0;
    char line[1024];
    DWORD written = 0;
    const auto put = [&](const char *text) {
        WriteFile(txt, text, DWORD(std::strlen(text)), &written, nullptr);
    };
    const EXCEPTION_RECORD *rec = job->pointers->ExceptionRecord;
    std::snprintf(line, sizeof(line),
                  "SSHIT-Commander %s - Absturz %04u-%02u-%02u %02u:%02u:%02u\r\n"
                  "Ausnahme: 0x%08lX %s\r\n",
                  g_version, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond,
                  rec->ExceptionCode, describe(rec->ExceptionCode));
    put(line);
    if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2) {
        std::snprintf(line, sizeof(line), "Zugriff: %s auf Adresse 0x%llx\r\n",
                      rec->ExceptionInformation[0] == 0 ? "Lesen"
                      : rec->ExceptionInformation[0] == 1 ? "Schreiben" : "Ausfuehren",
                      static_cast<unsigned long long>(rec->ExceptionInformation[1]));
        put(line);
    }
    if (g_lastMessage[0]) {
        std::snprintf(line, sizeof(line), "Letzte Qt-Meldung: %s\r\n", g_lastMessage);
        put(line);
    }

    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, nullptr, TRUE);
    char where[600];
    describeAddress(process, reinterpret_cast<DWORD64>(rec->ExceptionAddress), where, sizeof(where));
    std::snprintf(line, sizeof(line), "Ort: %s\r\n\r\nAufrufkette:\r\n", where);
    put(line);

    CONTEXT ctx = *job->pointers->ContextRecord;
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = ctx.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = ctx.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
    HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, job->threadId);
    for (int i = 0; i < 64 && thread; ++i) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &ctx, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr)
            || frame.AddrPC.Offset == 0)
            break;
        describeAddress(process, frame.AddrPC.Offset, where, sizeof(where));
        std::snprintf(line, sizeof(line), "  #%02d %s\r\n", i, where);
        put(line);
    }
    if (thread)
        CloseHandle(thread);
    SymCleanup(process);
    CloseHandle(txt);
    return 0;
}

LONG WINAPI crashFilter(EXCEPTION_POINTERS *pointers)
{
    static volatile LONG once = 0;
    if (InterlockedExchange(&once, 1) != 0)
        return EXCEPTION_CONTINUE_SEARCH;   // nur der erste Absturz zaehlt
    CrashJob job{pointers, GetCurrentThreadId()};
    if (HANDLE worker = CreateThread(nullptr, 256 * 1024, writeReport, &job, 0, nullptr)) {
        WaitForSingleObject(worker, 30000);
        CloseHandle(worker);
    }
    return EXCEPTION_EXECUTE_HANDLER;   // Prozess beenden (ohne Windows-Fehlerdialog)
}

void raiseCrash(DWORD code)
{
    RaiseException(code, EXCEPTION_NONCONTINUABLE, 0, nullptr);
}

void onAbort(int) { raiseCrash(kAbortCode); }
void onTerminate() { raiseCrash(kTerminateCode); }
void onPurecall() { raiseCrash(kPurecallCode); }
void onInvalidParameter(const wchar_t *, const wchar_t *, const wchar_t *, unsigned int, uintptr_t)
{
    raiseCrash(kInvalidParamCode);
}

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    if (type == QtFatalMsg || type == QtCriticalMsg) {
        const QByteArray text = msg.toUtf8().left(int(sizeof(g_lastMessage)) - 1);
        std::memcpy(g_lastMessage, text.constData(), size_t(text.size()));
        g_lastMessage[text.size()] = '\0';
    }
    if (g_prevHandler)
        g_prevHandler(type, context, msg);
    // qFatal beendet den Prozess danach per __fastfail — das umgeht jeden
    // Ausnahme-Filter. Deshalb den Bericht hier selbst ausloesen.
    if (type == QtFatalMsg)
        raiseCrash(kAbortCode);
}

} // namespace

void installCrashHandler()
{
    const QString dir = crashDirectory();
    QDir().mkpath(dir);
    const std::wstring wide = QDir::toNativeSeparators(dir).toStdWString();
    std::wcsncpy(g_dir, wide.c_str(), MAX_PATH - 1);
    std::snprintf(g_version, sizeof(g_version), "%s", SSHIT_VERSION);

    SetUnhandledExceptionFilter(crashFilter);
    // Auch "weiche" Abstuerze (abort, qFatal, unbehandelte C++-Ausnahmen,
    // CRT-Fehler) in denselben Bericht leiten statt still zu enden.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    std::signal(SIGABRT, onAbort);
    std::set_terminate(onTerminate);
    _set_purecall_handler(onPurecall);
    _set_invalid_parameter_handler(onInvalidParameter);
    g_prevHandler = qInstallMessageHandler(messageHandler);
}

#else

void installCrashHandler() {}

#endif

void reportCaughtException(const QString &what)
{
    QDir().mkpath(crashDirectory());
    QFile log(crashDirectory() + QStringLiteral("/errors.log"));
    if (log.open(QIODevice::Append | QIODevice::Text))
        log.write((QDateTime::currentDateTime().toString(Qt::ISODate) + QStringLiteral("  ")
                   + QString::fromLatin1(SSHIT_VERSION) + QStringLiteral("  ") + what
                   + QLatin1Char('\n'))
                      .toUtf8());
    static bool shown = false;
    if (shown)
        return;
    shown = true;
    // Verzoegert: nicht mitten in der Ereignisverarbeitung (z. B. beim Ziehen)
    // einen modalen Dialog oeffnen.
    QTimer::singleShot(0, [what] {
        QMessageBox::warning(nullptr, QStringLiteral("SSHIT-Commander"),
                             _t("Ein interner Fehler wurde abgefangen, die App läuft weiter:\n\n%1\n\n"
                                "Details stehen in errors.log im Ordner der Absturzberichte.")
                                 .arg(what));
    });
}

void reportPreviousCrash(QWidget *parent)
{
    const QDir dir(crashDirectory());
    const QFileInfoList reports =
        dir.entryInfoList({QStringLiteral("crash-*.txt")}, QDir::Files, QDir::Time);
    if (reports.isEmpty())
        return;
    const QDateTime newest = reports.first().lastModified();
    const QDateTime seen = QDateTime::fromString(
        core::getSettingString(QStringLiteral("crash_last_seen")), Qt::ISODate);
    if (seen.isValid() && newest <= seen)
        return;
    core::setSetting(QStringLiteral("crash_last_seen"), newest.toString(Qt::ISODate));

    QMessageBox box(parent);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(QStringLiteral("SSHIT-Commander"));
    box.setText(_t("SSHIT-Commander wurde zuletzt unerwartet beendet."));
    box.setInformativeText(
        _t("Ein Absturzbericht wurde gespeichert:\n%1\n\nBitte die .txt- und .dmp-Datei an den "
           "Entwickler schicken — sie enthalten keine Passwörter, aber ggf. Pfade und Servernamen "
           "aus dem Arbeitsspeicher.")
            .arg(QDir::toNativeSeparators(reports.first().absoluteFilePath())));
    QPushButton *open = box.addButton(_t("Ordner öffnen"), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Ok);
    box.exec();
    if (box.clickedButton() == open)
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir.absolutePath()));
}

} // namespace ncssh::gui
