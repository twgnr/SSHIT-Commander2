#include "ncssh/core/venvtools.hpp"

#include "ncssh/core/settings.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>

#include <algorithm>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace ncssh::core {

namespace {

#ifdef Q_OS_WIN
const QChar kSep = QLatin1Char('\\');
#else
const QChar kSep = QLatin1Char('/');
#endif

// Entspricht os.path.normcase: Windows lowercased + Backslashes, sonst identisch.
QString normCase(const QString &path)
{
#ifdef Q_OS_WIN
    QString s = path;
    s.replace(QLatin1Char('/'), QLatin1Char('\\'));
    return s.toLower();
#else
    return path;
#endif
}

QString join(const QString &dir, const QString &name)
{
    return QDir(dir).filePath(name);
}

QString rstripSlash(const QString &path)
{
    QString t = path;
    while (t.endsWith(QLatin1Char('/')) || t.endsWith(QLatin1Char('\\')))
        t.chop(1);
    return t;
}

QJsonObject metaObj()
{
    const QVariant v = getSetting(QStringLiteral("venv_meta"), QVariantMap{});
    if (v.canConvert<QVariantMap>())
        return QJsonObject::fromVariantMap(v.toMap());
    return {};
}

void saveMeta(const QJsonObject &obj)
{
    setSetting(QStringLiteral("venv_meta"), obj);
}

std::vector<QString> searchRoots(const std::vector<QString> &extra)
{
    const QString home = QDir::homePath();
    std::vector<QString> roots = extra;
    roots.push_back(qEnvironmentVariable("WORKON_HOME"));
    roots.push_back(join(home, QStringLiteral(".virtualenvs")));
    roots.push_back(join(home, QStringLiteral(".local/share/virtualenvs")));
    roots.push_back(join(qEnvironmentVariable("USERPROFILE", home), QStringLiteral(".virtualenvs")));
    std::vector<QString> out;
    for (const QString &r : roots)
        if (!r.isEmpty())
            out.push_back(r);
    return out;
}

#ifdef Q_OS_WIN
QString regString(HKEY key, const wchar_t *name)
{
    DWORD type = 0;
    DWORD size = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS)
        return {};
    if (type != REG_SZ && type != REG_EXPAND_SZ)
        return {};
    std::vector<wchar_t> buf(size / sizeof(wchar_t) + 1, L'\0');
    if (RegQueryValueExW(key, name, nullptr, nullptr,
                         reinterpret_cast<LPBYTE>(buf.data()), &size) != ERROR_SUCCESS)
        return {};
    return QString::fromWCharArray(buf.data());
}

// Installierte Pythons aus der Windows-Registry (PythonCore/InstallPath).
std::vector<std::pair<QString, QString>> winRegistryPythons()
{
    std::vector<std::pair<QString, QString>> out;
    struct Root { HKEY hive; const wchar_t *base; };
    const Root roots[] = {
        {HKEY_CURRENT_USER, L"Software\\Python\\PythonCore"},
        {HKEY_LOCAL_MACHINE, L"Software\\Python\\PythonCore"},
        {HKEY_LOCAL_MACHINE, L"Software\\WOW6432Node\\Python\\PythonCore"},
    };
    for (const Root &r : roots) {
        HKEY key = nullptr;
        if (RegOpenKeyExW(r.hive, r.base, 0, KEY_READ, &key) != ERROR_SUCCESS)
            continue;
        for (DWORD i = 0;; ++i) {
            wchar_t nameBuf[256];
            DWORD nameLen = 256;
            const LONG rc = RegEnumKeyExW(key, i, nameBuf, &nameLen, nullptr,
                                          nullptr, nullptr, nullptr);
            if (rc != ERROR_SUCCESS)
                break;
            const QString ver = QString::fromWCharArray(nameBuf, static_cast<int>(nameLen));
            const std::wstring sub =
                (QString::fromWCharArray(r.base) + QLatin1Char('\\') + ver
                 + QStringLiteral("\\InstallPath")).toStdWString();
            HKEY ip = nullptr;
            QString exe;
            if (RegOpenKeyExW(r.hive, sub.c_str(), 0, KEY_READ, &ip) == ERROR_SUCCESS) {
                exe = regString(ip, L"ExecutablePath");
                if (exe.isEmpty()) {
                    const QString base = regString(ip, L"");
                    if (!base.isEmpty())
                        exe = QDir::toNativeSeparators(join(base, QStringLiteral("python.exe")));
                }
                RegCloseKey(ip);
            }
            if (!exe.isEmpty() && QFileInfo(exe).isFile())
                out.push_back({QStringLiteral("Python %1").arg(ver.section(QLatin1Char('-'), 0, 0)),
                               exe});
        }
        RegCloseKey(key);
    }
    return out;
}

// Installierte Pythons aus ueblichen Installationsordnern.
std::vector<std::pair<QString, QString>> winDirPythons()
{
    std::vector<std::pair<QString, QString>> out;
    struct Pat { QString dir; };
    std::vector<QString> dirs;
    const QString local = qEnvironmentVariable("LOCALAPPDATA");
    if (!local.isEmpty())
        dirs.push_back(local + QStringLiteral("\\Programs\\Python"));
    dirs.push_back(QStringLiteral("C:\\"));
    dirs.push_back(QStringLiteral("C:\\Program Files"));
    dirs.push_back(QStringLiteral("C:\\Program Files (x86)"));

    const QRegularExpression re(QStringLiteral("Python(\\d)(\\d+)"),
                                QRegularExpression::CaseInsensitiveOption);
    for (const QString &dir : dirs) {
        QDir d(dir);
        if (!d.exists())
            continue;
        const auto entries = d.entryList({QStringLiteral("Python3*")},
                                         QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &e : entries) {
            const QString exe = join(d.filePath(e), QStringLiteral("python.exe"));
            if (!QFileInfo(exe).isFile())
                continue;
            const auto m = re.match(exe);
            const QString ver = m.hasMatch()
                ? QStringLiteral("%1.%2").arg(m.captured(1), m.captured(2))
                : QStringLiteral("?");
            out.push_back({QStringLiteral("Python %1").arg(ver), QDir::toNativeSeparators(exe)});
        }
    }
    return out;
}
#endif // Q_OS_WIN

} // namespace

QString detectInstall(const QString &projectDir)
{
    const auto has = [&](const QString &name) {
        return QFileInfo(join(projectDir, name)).exists();
    };
    if (has(QStringLiteral("pyproject.toml")) || has(QStringLiteral("setup.py"))
        || has(QStringLiteral("setup.cfg")))
        return QStringLiteral("python -m pip install -e .");
    if (has(QStringLiteral("requirements.txt")))
        return QStringLiteral("python -m pip install -r requirements.txt");
    if (has(QStringLiteral("Pipfile")))
        return QStringLiteral("python -m pip install pipenv && pipenv install");
    return {};
}

QString applySkip(const QString &install, bool skip)
{
    if (!skip || install.isEmpty())
        return install;
    if (install.contains(QLatin1String("pipenv install"))
        && !install.contains(QLatin1String("--skip-lock")))
        return install + QStringLiteral(" --skip-lock");
    if (install.contains(QLatin1String("pip install"))
        && !install.contains(QLatin1String("--no-deps")))
        return install + QStringLiteral(" --no-deps");
    return install;
}

std::vector<std::pair<QString, QString>> discoverPythons()
{
    std::vector<std::pair<QString, QString>> out;
    QSet<QString> seen;
    const auto add = [&](const QString &label, const QString &cmd) {
        if (cmd.isEmpty())
            return;
        const QString key = normCase(cmd);
        if (seen.contains(key))
            return;
        seen.insert(key);
        out.push_back({label, cmd});
    };

#ifdef Q_OS_WIN
    for (const QString &g : {QStringLiteral("python"), QStringLiteral("py"),
                             QStringLiteral("python3")})
        add(g, g);
    std::vector<std::pair<QString, QString>> found = winRegistryPythons();
    const auto dirFound = winDirPythons();
    found.insert(found.end(), dirFound.begin(), dirFound.end());
    // nach Version absteigend (neueste zuerst)
    std::sort(found.begin(), found.end(),
              [](const auto &a, const auto &b) { return a.first > b.first; });
    for (const auto &[label, exe] : found)
        add(QStringLiteral("%1  (%2)").arg(label, exe), exe);
#else
    for (const QString &g : {QStringLiteral("python3"), QStringLiteral("python")})
        add(g, g);
    std::vector<QString> cands;
    for (const QString &base : {QStringLiteral("/usr/bin"), QStringLiteral("/usr/local/bin")}) {
        QDir d(base);
        for (const QString &e : d.entryList({QStringLiteral("python3.*")}, QDir::Files))
            cands.push_back(d.filePath(e));
    }
    QDir pyenv(QDir::homePath() + QStringLiteral("/.pyenv/versions"));
    for (const QString &v : pyenv.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        cands.push_back(pyenv.filePath(v) + QStringLiteral("/bin/python"));
    std::sort(cands.begin(), cands.end());
    const QRegularExpression reName(QStringLiteral("(\\d+\\.\\d+)"));
    const QRegularExpression rePath(QStringLiteral("versions/(\\d+\\.\\d+)"));
    for (const QString &path : cands) {
        if (!QFileInfo(path).isFile())
            continue;
        auto m = reName.match(QFileInfo(path).fileName());
        if (!m.hasMatch())
            m = rePath.match(path);
        add(m.hasMatch() ? QStringLiteral("Python %1 (%2)").arg(m.captured(1), path) : path, path);
    }
#endif
    return out;
}

QString envKind(const QString &venvDir)
{
    if (QFileInfo(join(venvDir, QStringLiteral(".project"))).isFile())
        return QStringLiteral("pipenv");
    const QString home = QDir::homePath();
    const QString low = normCase(QDir::cleanPath(venvDir));
    const std::vector<QString> roots = {
        qEnvironmentVariable("WORKON_HOME"),
        join(home, QStringLiteral(".virtualenvs")),
        join(home, QStringLiteral(".local/share/virtualenvs")),
        join(qEnvironmentVariable("USERPROFILE", home), QStringLiteral(".virtualenvs")),
    };
    for (const QString &root : roots) {
        if (root.isEmpty())
            continue;
        if (low.startsWith(normCase(QDir::cleanPath(root)) + kSep))
            return QStringLiteral("pipenv");
    }
    return QStringLiteral("venv");
}

QString pyvenvVersion(const QString &venvDir)
{
    QFile fh(join(venvDir, QStringLiteral("pyvenv.cfg")));
    if (!fh.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    QTextStream in(&fh);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.trimmed().toLower().startsWith(QLatin1String("version"))) {
            const int eq = line.indexOf(QLatin1Char('='));
            if (eq >= 0)
                return line.mid(eq + 1).trimmed();
        }
    }
    return {};
}

QString envInfo(const QString &path)
{
    const QJsonValue e = metaObj().value(path);
    return e.isObject() ? e.toObject().value(QStringLiteral("info")).toString() : QString();
}

void setEnvInfo(const QString &path, const QString &info)
{
    QJsonObject data = metaObj();
    QJsonObject entry = data.value(path).toObject();
    entry.insert(QStringLiteral("info"), info);
    data.insert(path, entry);
    saveMeta(data);
}

void setEnvProject(const QString &path, const QString &project)
{
    if (project.isEmpty())
        return;
    QJsonObject data = metaObj();
    QJsonObject entry = data.value(path).toObject();
    entry.insert(QStringLiteral("project"), project);
    data.insert(path, entry);
    saveMeta(data);
}

QString envProject(const QString &venvDir)
{
    const QJsonValue e = metaObj().value(venvDir);
    if (e.isObject()) {
        const QString proj = e.toObject().value(QStringLiteral("project")).toString();
        if (!proj.isEmpty())
            return proj;
    }
    QFile fh(join(venvDir, QStringLiteral(".project")));
    if (fh.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString proj = QString::fromUtf8(fh.readAll()).trimmed();
        if (!proj.isEmpty())
            return proj;
    }
    const QString trimmed = rstripSlash(venvDir);
    const QString base = QFileInfo(trimmed).fileName().toLower();
    if (base == QLatin1String(".venv") || base == QLatin1String("venv")
        || base == QLatin1String("env") || base == QLatin1String(".env"))
        return QFileInfo(trimmed).path();
    return {};
}

void deleteEnvMeta(const QString &path)
{
    QJsonObject data = metaObj();
    if (data.contains(path)) {
        data.remove(path);
        saveMeta(data);
    }
}

QString activateCommand(const QString &osType, const QString &venvDir)
{
    if (osType == QLatin1String("windows"))
        return QStringLiteral("\"%1\\Scripts\\activate.bat\"").arg(venvDir);
    return QStringLiteral(". \"%1/bin/activate\"").arg(venvDir);
}

std::vector<QString> setupCommands(const QString &osType, const QString &projectDir,
                                   const QString &venvDir, const QString &python,
                                   const std::optional<QString> &install)
{
    const QString py = !python.isEmpty()
        ? python
        : (osType == QLatin1String("windows") ? QStringLiteral("python")
                                              : QStringLiteral("python3"));
    const QString inst = install.has_value() ? *install : detectInstall(projectDir);
    std::vector<QString> cmds;
    if (osType == QLatin1String("windows"))
        cmds.push_back(QStringLiteral("cd /d \"%1\"").arg(projectDir));
    else
        cmds.push_back(QStringLiteral("cd \"%1\"").arg(projectDir));
    cmds.push_back(QStringLiteral("\"%1\" -m venv \"%2\"").arg(py, venvDir));
    cmds.push_back(activateCommand(osType, venvDir));
    if (!inst.isEmpty())
        cmds.push_back(inst);
    return cmds;
}

bool isVenv(const QString &path)
{
    return QFileInfo(join(path, QStringLiteral("pyvenv.cfg"))).isFile()
        || QFileInfo(join(path, QStringLiteral("Scripts/activate.bat"))).isFile()
        || QFileInfo(join(path, QStringLiteral("bin/activate"))).isFile();
}

std::vector<VenvInfo> discover(const std::vector<QString> &extraDirs)
{
    std::vector<VenvInfo> found;
    QSet<QString> seen;
    const auto add = [&](const QString &path) {
        const QString key = normCase(QDir::cleanPath(path));
        if (seen.contains(key) || !isVenv(path))
            return;
        seen.insert(key);
        const QString trimmed = rstripSlash(path);
        VenvInfo vi;
        vi.name = QFileInfo(trimmed).fileName();
        if (vi.name.isEmpty())
            vi.name = path;
        vi.path = path;
        vi.version = pyvenvVersion(path);
        vi.kind = envKind(path);
        vi.project = envProject(path);
        vi.info = envInfo(path);
        found.push_back(vi);
    };

    for (const QString &root : searchRoots(extraDirs)) {
        if (!QFileInfo(root).isDir())
            continue;
        if (isVenv(root)) {
            add(root);
            continue;
        }
        QDir d(root);
        const auto names = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &n : names) {
            const QString full = d.filePath(n);
            if (QFileInfo(full).isDir())
                add(full);
        }
    }
    return found;
}

} // namespace ncssh::core
