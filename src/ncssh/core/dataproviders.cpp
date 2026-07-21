// Live-Datenquellen fuer dynamische Makro-Tasten.  (Port von core/dataproviders.py)
#include "ncssh/core/dataproviders.hpp"

#include <QDate>
#include <QDateTime>
#include <QStorageInfo>
#include <QTime>

#include <stdexcept>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace ncssh::core::dataproviders {

namespace {

QString clock_()
{
    return QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
}

QString clockShort()
{
    return QTime::currentTime().toString(QStringLiteral("HH:mm"));
}

QString date_()
{
    return QDate::currentDate().toString(QStringLiteral("dd.MM.yyyy"));
}

QString dateTime_()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("dd.MM. HH:mm"));
}

#ifdef Q_OS_WIN
quint64 fileTimeToU64(const FILETIME &ft)
{
    return (quint64(ft.dwHighDateTime) << 32) | quint64(ft.dwLowDateTime);
}
#endif

// CPU-Auslastung in Prozent. Wie psutil.cpu_percent(): Differenz seit dem
// letzten Aufruf; der allererste Aufruf liefert 0.
double cpuPercent()
{
#ifdef Q_OS_WIN
    static quint64 lastIdle = 0;
    static quint64 lastTotal = 0;
    FILETIME idleFt, kernelFt, userFt;
    if (!GetSystemTimes(&idleFt, &kernelFt, &userFt))
        throw std::runtime_error("GetSystemTimes fehlgeschlagen");
    const quint64 idle = fileTimeToU64(idleFt);
    // "kernel" enthaelt die Idle-Zeit bereits.
    const quint64 total = fileTimeToU64(kernelFt) + fileTimeToU64(userFt);
    double pct = 0.0;
    if (lastTotal != 0 && total > lastTotal) {
        const quint64 dTotal = total - lastTotal;
        const quint64 dIdle = idle - lastIdle;
        pct = 100.0 * double(dTotal - dIdle) / double(dTotal);
    }
    lastIdle = idle;
    lastTotal = total;
    return pct;
#else
    throw std::runtime_error("CPU-Statistik nicht verfuegbar");
#endif
}

// RAM-Auslastung in Prozent (wie psutil.virtual_memory().percent).
double ramPercent()
{
#ifdef Q_OS_WIN
    MEMORYSTATUSEX st{};
    st.dwLength = sizeof(st);
    if (!GlobalMemoryStatusEx(&st))
        throw std::runtime_error("GlobalMemoryStatusEx fehlgeschlagen");
    return double(st.dwMemoryLoad);
#else
    throw std::runtime_error("RAM-Statistik nicht verfuegbar");
#endif
}

QString cpu()
{
    return QStringLiteral("CPU %1%").arg(qRound(cpuPercent()));
}

QString ram()
{
    return QStringLiteral("RAM %1%").arg(qRound(ramPercent()));
}

QString disk()
{
#ifdef Q_OS_WIN
    const QStorageInfo info(QStringLiteral("C:/"));
#else
    const QStorageInfo info(QStringLiteral("/"));
#endif
    if (!info.isValid() || info.bytesTotal() <= 0)
        throw std::runtime_error("Speicherplatz-Statistik nicht verfuegbar");
    const double used = double(info.bytesTotal() - info.bytesFree());
    const double pct = 100.0 * used / double(info.bytesTotal());
    return QStringLiteral("C: %1%").arg(qRound(pct));
}

// Anzeigename -> (Beschriftung im Editor, Funktion, braucht System-Statistik?)
// Beschriftungen laufen wie im Original NICHT durch die Uebersetzung.
struct Provider {
    const char *name;
    const char *label;
    QString (*fn)();
    bool needsPsutil;
};

constexpr Provider kProviders[] = {
    {"clock", "Uhr (HH:MM:SS)", &clock_, false},
    {"clock_short", "Uhr (HH:MM)", &clockShort, false},
    {"date", "Datum", &date_, false},
    {"datetime", "Datum + Uhrzeit", &dateTime_, false},
    {"cpu", "CPU-Auslastung", &cpu, true},
    {"ram", "RAM-Auslastung", &ram, true},
    {"disk", "Speicherplatz C:", &disk, true},
};

const Provider *find(const QString &name)
{
    for (const Provider &p : kProviders)
        if (name == QLatin1String(p.name))
            return &p;
    return nullptr;
}

} // namespace

QStringList providerNames()
{
    QStringList names;
    for (const Provider &p : kProviders)
        names << QString::fromLatin1(p.name);
    return names;
}

QString providerLabel(const QString &name)
{
    const Provider *p = find(name);
    return p ? QString::fromUtf8(p->label) : QStringLiteral("?");
}

bool needsPsutil(const QString &name)
{
    const Provider *p = find(name);
    return p && p->needsPsutil;
}

QString value(const QString &name)
{
    const Provider *p = find(name);
    if (!p)
        return {};
    try {
        return p->fn();
    } catch (...) {
        return QStringLiteral("—");  // "—" bei Fehler/Nichtverfuegbarkeit
    }
}

bool psutilAvailable()
{
    // System-Statistiken sind unter Windows per WinAPI eingebaut; die
    // psutil-Abfrage des Originals entfaellt dort.
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

} // namespace ncssh::core::dataproviders
