#include "ncssh/core/openwith.hpp"

#include <QFileInfo>
#include <QSet>
#include <QSettings>

namespace ncssh::core {

#ifdef Q_OS_WIN

// Default-Wert eines Registry-Schluessels (QSettings: Wert "Default"/".").
static QString regDefault(const QString &path)
{
    QSettings reg(path, QSettings::NativeFormat);
    return reg.value(QStringLiteral("Default")).toString();
}

static QString regValue(const QString &path, const QString &name)
{
    QSettings reg(path, QSettings::NativeFormat);
    return reg.value(name).toString();
}

// %VAR%-Platzhalter (REG_EXPAND_SZ, z.B. "%SystemRoot%\system32\notepad.exe")
// aufloesen. Unbekannte Namen — etwa die Argument-Platzhalter "%1"/"%L" —
// bleiben unveraendert stehen.
static QString expandEnvVars(const QString &s)
{
    QString out;
    int i = 0;
    while (i < s.size()) {
        if (s.at(i) == QLatin1Char('%')) {
            const int end = s.indexOf(QLatin1Char('%'), i + 1);
            if (end > i + 1) {
                const QString name = s.mid(i + 1, end - i - 1);
                const QByteArray key = name.toLocal8Bit();
                if (qEnvironmentVariableIsSet(key.constData())) {
                    out += qEnvironmentVariable(key.constData());
                    i = end + 1;
                    continue;
                }
            }
        }
        out += s.at(i);
        ++i;
    }
    return out;
}

static bool isExistingFile(const QString &path)
{
    if (QFileInfo(path).isFile())
        return true;
    return !path.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive)
           && QFileInfo(path + QLatin1String(".exe")).isFile();
}

// Programmpfad aus einem shell\open\command-String extrahieren.
static QString exeFromCommand(const QString &cmd)
{
    const QString c = expandEnvVars(cmd.trimmed());
    if (c.isEmpty())
        return {};
    if (c.startsWith(QLatin1Char('"'))) {
        const int end = c.indexOf(QLatin1Char('"'), 1);
        return end > 0 ? c.mid(1, end - 1) : c.mid(1);
    }
    // Ungequotete Pfade mit Leerzeichen ("C:\Program Files\x\x.exe %1"):
    // frueher wurde am ersten Leerzeichen abgeschnitten -> "C:\Program".
    // Wie CreateProcess die Praefixe an Leerzeichen der Reihe nach probieren
    // und den ersten nehmen, der als Datei existiert.
    int sp = c.indexOf(QLatin1Char(' '));
    while (sp > 0) {
        const QString cand = c.left(sp);
        if (isExistingFile(cand))
            return QFileInfo(cand).isFile() ? cand : cand + QLatin1String(".exe");
        sp = c.indexOf(QLatin1Char(' '), sp + 1);
    }
    if (isExistingFile(c))
        return QFileInfo(c).isFile() ? c : c + QLatin1String(".exe");
    // Nichts gefunden: altes Verhalten (erstes Wort, z.B. Programm im PATH).
    sp = c.indexOf(QLatin1Char(' '));
    return sp < 0 ? c : c.left(sp);
}

static QString friendly(const QString &exePath, const QString &fallbackName)
{
    QString base = QFileInfo(exePath).fileName();
    if (base.isEmpty())
        base = fallbackName;
    QString name = regValue(
        QStringLiteral("HKEY_CLASSES_ROOT\\Applications\\%1").arg(base),
        QStringLiteral("FriendlyAppName"));
    if (name.isEmpty())
        name = base;
    if (name.toLower().endsWith(QLatin1String(".exe")))
        name.chop(4);
    return name;
}

// exe-Name -> (Anzeigename, vollstaendiger Pfad).
static std::pair<QString, QString> resolveExe(const QString &exeName)
{
    QString path = exeFromCommand(regDefault(
        QStringLiteral("HKEY_CLASSES_ROOT\\Applications\\%1\\shell\\open\\command")
            .arg(exeName)));
    if (path.isEmpty()) {
        for (const QString &root :
             {QStringLiteral("HKEY_CURRENT_USER"), QStringLiteral("HKEY_LOCAL_MACHINE")}) {
            QString p = regDefault(
                QStringLiteral("%1\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\%2")
                    .arg(root, exeName));
            if (!p.isEmpty()) {
                p.remove(QLatin1Char('"'));
                path = expandEnvVars(p);   // App Paths darf %VAR% enthalten
                break;
            }
        }
    }
    if (path.isEmpty())
        path = exeName;  // auf PATH hoffen
    return {friendly(path, exeName), path};
}

// Liest die OpenWithList-Werte (a, b, c ...) -> exe-Namen.
static QStringList openwithList(const QString &path)
{
    QStringList names;
    QSettings reg(path, QSettings::NativeFormat);
    for (const QString &vname : reg.childKeys()) {
        if (vname.toLower() == QLatin1String("mrulist"))
            continue;
        const QString vval = reg.value(vname).toString();
        if (vval.toLower().endsWith(QLatin1String(".exe")))
            names.append(vval);
    }
    return names;
}

std::vector<std::pair<QString, QString>> programsForExtension(const QString &extIn)
{
    if (extIn.isEmpty())
        return {};
    QString ext = extIn.toLower();
    if (!ext.startsWith(QLatin1Char('.')))
        ext.prepend(QLatin1Char('.'));

    std::vector<std::pair<QString, QString>> out;
    QSet<QString> seen;

    // 1) Standardprogramm (UserChoice bevorzugt, sonst HKCR-Default-ProgId)
    QString progid = regValue(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion"
                       "\\Explorer\\FileExts\\%1\\UserChoice").arg(ext),
        QStringLiteral("ProgId"));
    if (progid.isEmpty())
        progid = regDefault(QStringLiteral("HKEY_CLASSES_ROOT\\%1").arg(ext));
    if (!progid.isEmpty()) {
        const QString path = exeFromCommand(regDefault(
            QStringLiteral("HKEY_CLASSES_ROOT\\%1\\shell\\open\\command").arg(progid)));
        if (!path.isEmpty() && !seen.contains(path.toLower())) {
            seen.insert(path.toLower());
            out.emplace_back(friendly(path, progid), path);
        }
    }

    // 2) OpenWithList aus HKCR\.ext und HKCU\...\FileExts\.ext
    QStringList exeNames = openwithList(
        QStringLiteral("HKEY_CLASSES_ROOT\\%1\\OpenWithList").arg(ext));
    exeNames += openwithList(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion"
                       "\\Explorer\\FileExts\\%1\\OpenWithList").arg(ext));
    for (const QString &exe : exeNames) {
        const auto [name, path] = resolveExe(exe);
        if (seen.contains(path.toLower()))
            continue;
        seen.insert(path.toLower());
        out.emplace_back(name, path);
    }
    return out;
}

#else

std::vector<std::pair<QString, QString>> programsForExtension(const QString &) { return {}; }

#endif

} // namespace ncssh::core
