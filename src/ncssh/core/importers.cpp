#include "ncssh/core/importers.hpp"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSettings>
#include <QUrl>
#include <optional>

namespace ncssh::core {

// Prozent-Dekodierung wie urllib.parse.unquote.
static QString unquote(const QString &s)
{
    return QUrl::fromPercentEncoding(s.toUtf8());
}

#ifdef Q_OS_WIN

static std::vector<ServerProfile> importRegistrySessions(const QString &basePath,
                                                         const QString &sourceLabel)
{
    std::vector<ServerProfile> profiles;
    QSettings reg(basePath, QSettings::NativeFormat);
    for (const QString &rawName : reg.childGroups()) {
        reg.beginGroup(rawName);
        const QString host = reg.value(QStringLiteral("HostName")).toString();
        if (!host.isEmpty()) {
            const QString name = unquote(rawName);
            if (name.toLower() != QLatin1String("default settings")) {
                QString keyfile = reg.value(QStringLiteral("PublicKeyFile")).toString();
                if (sourceLabel == QLatin1String("WinSCP"))
                    keyfile = unquote(keyfile);
                ServerProfile p;
                p.name = QStringLiteral("%1 (%2)").arg(name, sourceLabel);
                p.host = host;
                p.port = reg.value(QStringLiteral("PortNumber"), 22).toInt();
                if (p.port == 0) p.port = 22;
                p.username = reg.value(QStringLiteral("UserName")).toString();
                p.authMethod = keyfile.isEmpty() ? QStringLiteral("password") : QStringLiteral("key");
                p.keyPath = keyfile;
                profiles.push_back(p);
            }
        }
        reg.endGroup();
    }
    return profiles;
}

std::vector<ServerProfile> importPutty()
{
    return importRegistrySessions(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\SimonTatham\\PuTTY\\Sessions"),
        QStringLiteral("PuTTY"));
}

// Vorwaertsdeklaration: gemeinsamer INI-Parser (unten definiert).
static std::vector<ServerProfile> importWinscpIniFile(const QString &path);

static std::vector<ServerProfile> winscpIni()
{
    const QString path = qEnvironmentVariable("APPDATA") + QStringLiteral("/WinSCP.ini");
    if (!QFile::exists(path))
        return {};
    return importWinscpIniFile(path);
}

std::vector<ServerProfile> importWinscp()
{
    auto profiles = importRegistrySessions(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Martin Prikryl\\WinSCP 2\\Sessions"),
        QStringLiteral("WinSCP"));
    if (profiles.empty())
        profiles = winscpIni();
    return profiles;
}

#else

std::vector<ServerProfile> importPutty() { return {}; }
std::vector<ServerProfile> importWinscp() { return {}; }

#endif // Q_OS_WIN

std::vector<ServerProfile> importSshConfig()
{
    const QString path = QDir::homePath() + QStringLiteral("/.ssh/config");
    if (!QFile::exists(path))
        return {};
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    std::vector<ServerProfile> profiles;
    QHash<QString, QString> block;
    bool haveBlock = false;

    const auto flush = [&]() {
        if (!haveBlock)
            return;
        const QString alias = block.value(QStringLiteral("alias"));
        if (alias.isEmpty() || alias.contains(QLatin1Char('*')) || alias.contains(QLatin1Char('?')))
            return;
        const QString keyfile = block.value(QStringLiteral("identityfile"));
        ServerProfile p;
        p.name = QStringLiteral("%1 (ssh)").arg(alias);
        p.host = block.value(QStringLiteral("hostname"), alias);
        if (p.host.isEmpty()) p.host = alias;
        p.port = block.value(QStringLiteral("port"), QStringLiteral("22")).toInt();
        if (p.port == 0) p.port = 22;
        p.username = block.value(QStringLiteral("user"));
        p.authMethod = keyfile.isEmpty() ? QStringLiteral("agent") : QStringLiteral("key");
        if (!keyfile.isEmpty()) {
            QString kf = keyfile;
            if (kf.startsWith(QLatin1Char('~')))
                kf = QDir::homePath() + kf.mid(1);
            p.keyPath = kf;
        }
        profiles.push_back(p);
    };

    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const int sp = line.indexOf(QRegularExpression(QStringLiteral("\\s")));
        const QString key = (sp < 0 ? line : line.left(sp)).toLower();
        const QString val = (sp < 0 ? QString() : line.mid(sp + 1).trimmed());
        if (key == QLatin1String("host")) {
            flush();
            block.clear();
            block.insert(QStringLiteral("alias"), val.split(QRegularExpression(QStringLiteral("\\s+"))).value(0));
            haveBlock = true;
        } else if (haveBlock) {
            block.insert(key, val);
        }
    }
    flush();
    return profiles;
}

std::vector<ServerProfile> importAll()
{
    std::vector<ServerProfile> out = importPutty();
    const auto winscp = importWinscp();
    out.insert(out.end(), winscp.begin(), winscp.end());
    const auto ssh = importSshConfig();
    out.insert(out.end(), ssh.begin(), ssh.end());
    return out;
}

// --- Datei-Import ----------------------------------------------------------

static const QString kPuttyRegPrefix =
    QStringLiteral("software\\simontatham\\putty\\sessions\\");
static const QString kWinscpRegPrefix =
    QStringLiteral("software\\martin prikryl\\winscp 2\\sessions\\");

static std::optional<ServerProfile> profileFromFields(const QString &name, const QString &source,
                                                      const QHash<QString, QString> &fields)
{
    const QString host = fields.value(QStringLiteral("hostname"));
    if (host.isEmpty() || name.toLower() == QLatin1String("default settings"))
        return std::nullopt;
    const QString keyfile = fields.value(QStringLiteral("publickeyfile"));
    bool ok = false;
    int port = fields.value(QStringLiteral("portnumber")).toInt(&ok);
    if (!ok || port == 0)
        port = 22;
    ServerProfile p;
    p.name = QStringLiteral("%1 (%2)").arg(name, source);
    p.host = host;
    p.port = port;
    p.username = fields.value(QStringLiteral("username"));
    p.authMethod = keyfile.isEmpty() ? QStringLiteral("password") : QStringLiteral("key");
    p.keyPath = keyfile;
    return p;
}

// Entfernt .reg-String-Escapes (\\ -> \, \" -> ").
static QString regUnescape(const QString &value)
{
    QString out;
    for (int i = 0; i < value.size(); ++i) {
        if (value[i] == QLatin1Char('\\') && i + 1 < value.size()) {
            out += value[i + 1];
            ++i;
        } else {
            out += value[i];
        }
    }
    return out;
}

// Parst einen .reg-Wert (REG_SZ in Quotes oder dword:HEX).
static QString parseRegValue(const QString &rawIn)
{
    const QString raw = rawIn.trimmed();
    if (raw.startsWith(QLatin1Char('"')) && raw.endsWith(QLatin1Char('"')))
        return regUnescape(raw.mid(1, raw.length() - 2));
    if (raw.toLower().startsWith(QLatin1String("dword:"))) {
        bool ok = false;
        const int v = raw.section(QLatin1Char(':'), 1).toInt(&ok, 16);
        return ok ? QString::number(v) : QString();
    }
    return raw;
}

static std::vector<ServerProfile> importRegFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QByteArray raw = f.readAll();
    QString text;
    if (raw.startsWith("\xFF\xFE"))
        text = QString::fromUtf16(reinterpret_cast<const char16_t *>(raw.constData() + 2),
                                  (raw.size() - 2) / 2);
    else
        text = QString::fromUtf8(raw);
    if (!text.contains(QLatin1String("\\Sessions\\")) && !text.contains(QLatin1String("[HKEY")))
        text = QString::fromUtf8(raw);

    std::vector<ServerProfile> profiles;
    QString currentName, currentSource;
    QHash<QString, QString> fields;
    const auto flush = [&]() {
        if (!currentName.isEmpty() && !currentSource.isEmpty()) {
            if (auto p = profileFromFields(currentName, currentSource, fields))
                profiles.push_back(*p);
        }
    };

    for (const QString &rawLine : text.split(QRegularExpression(QStringLiteral("\\r?\\n")))) {
        const QString line = rawLine.trimmed();
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            flush();
            currentName.clear();
            currentSource.clear();
            fields.clear();
            const QString key = line.mid(1, line.length() - 2);
            const QString tail =
                (key.contains(QLatin1Char('\\')) ? key.section(QLatin1Char('\\'), 1) : key).toLower();
            if (tail.startsWith(kPuttyRegPrefix)) {
                currentSource = QStringLiteral("PuTTY");
                currentName = unquote(key.section(QLatin1Char('\\'), -1));
            } else if (tail.startsWith(kWinscpRegPrefix)) {
                currentSource = QStringLiteral("WinSCP");
                currentName = unquote(key.section(QLatin1Char('\\'), -1));
            }
            continue;
        }
        if (currentName.isEmpty() || !line.contains(QLatin1Char('=')))
            continue;
        QString namePart = line.section(QLatin1Char('='), 0, 0).trimmed();
        const QString valPart = line.section(QLatin1Char('='), 1);
        if (namePart.startsWith(QLatin1Char('"')) && namePart.endsWith(QLatin1Char('"')))
            namePart = namePart.mid(1, namePart.length() - 2);
        fields.insert(namePart.toLower(), parseRegValue(valPart));
    }
    flush();
    return profiles;
}

static std::vector<ServerProfile> importWinscpIniFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    std::vector<ServerProfile> profiles;
    QString section;
    QHash<QString, QString> fields;
    const QString prefix = QStringLiteral("Sessions\\");
    const auto flush = [&]() {
        if (section.startsWith(prefix)) {
            const QString name = unquote(section.mid(prefix.length()));
            QHash<QString, QString> norm;
            norm.insert(QStringLiteral("hostname"), fields.value(QStringLiteral("HostName")));
            norm.insert(QStringLiteral("portnumber"), fields.value(QStringLiteral("PortNumber"), QStringLiteral("22")));
            norm.insert(QStringLiteral("username"), fields.value(QStringLiteral("UserName")));
            norm.insert(QStringLiteral("publickeyfile"), unquote(fields.value(QStringLiteral("PublicKeyFile"))));
            if (auto p = profileFromFields(name, QStringLiteral("WinSCP"), norm))
                profiles.push_back(*p);
        }
    };

    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            flush();
            section = line.mid(1, line.length() - 2);
            fields.clear();
        } else if (line.contains(QLatin1Char('='))) {
            fields.insert(line.section(QLatin1Char('='), 0, 0).trimmed(),
                          line.section(QLatin1Char('='), 1).trimmed());
        }
    }
    flush();
    return profiles;
}

std::vector<ServerProfile> importFromFile(const QString &path)
{
    if (path.isEmpty() || !QFile::exists(path))
        return {};
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QByteArray head = f.read(64);
    f.close();

    QByteArray probe = head;
    if (probe.startsWith("\xFF\xFE"))
        probe = probe.mid(2);
    const QString probeStr = QString::fromLatin1(probe).trimmed().toLower();
    const bool looksReg = probeStr.startsWith(QLatin1String("windows registry"))
                          || probeStr.startsWith(QLatin1String("regedit4"));
    if (looksReg || path.toLower().endsWith(QLatin1String(".reg")))
        return importRegFile(path);
    return importWinscpIniFile(path);
}

} // namespace ncssh::core
