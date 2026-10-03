#include "ncssh/core/command_params.hpp"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <utility>

namespace ncssh::core {

namespace {

// Gaengige Unix-Werkzeuge, die zuverlaessig "--help" verstehen (zusaetzlich
// zu allen Befehlen des Katalogs). Shell-Builtins (cd, export …) und
// Befehle mit Nebenwirkungen (shutdown, reboot …) bewusst NICHT.
const QSet<QString> &knownPosixTools()
{
    static const QSet<QString> tools = {
        QStringLiteral("ls"), QStringLiteral("cp"), QStringLiteral("mv"), QStringLiteral("rm"),
        QStringLiteral("mkdir"), QStringLiteral("rmdir"), QStringLiteral("touch"),
        QStringLiteral("cat"), QStringLiteral("head"), QStringLiteral("tail"),
        QStringLiteral("grep"), QStringLiteral("egrep"), QStringLiteral("find"),
        QStringLiteral("du"), QStringLiteral("df"), QStringLiteral("free"), QStringLiteral("ps"),
        QStringLiteral("kill"), QStringLiteral("killall"), QStringLiteral("pkill"),
        QStringLiteral("pgrep"), QStringLiteral("chmod"), QStringLiteral("chown"),
        QStringLiteral("chgrp"), QStringLiteral("ln"), QStringLiteral("stat"),
        QStringLiteral("file"), QStringLiteral("tree"), QStringLiteral("wc"),
        QStringLiteral("sort"), QStringLiteral("uniq"), QStringLiteral("cut"),
        QStringLiteral("tr"), QStringLiteral("sed"), QStringLiteral("awk"),
        QStringLiteral("diff"), QStringLiteral("cmp"), QStringLiteral("tar"),
        QStringLiteral("gzip"), QStringLiteral("gunzip"), QStringLiteral("zip"),
        QStringLiteral("unzip"), QStringLiteral("xz"), QStringLiteral("bzip2"),
        QStringLiteral("rsync"), QStringLiteral("scp"), QStringLiteral("ssh"),
        QStringLiteral("curl"), QStringLiteral("wget"), QStringLiteral("ping"),
        QStringLiteral("traceroute"), QStringLiteral("dig"), QStringLiteral("host"),
        QStringLiteral("ip"), QStringLiteral("ss"), QStringLiteral("netstat"),
        QStringLiteral("systemctl"), QStringLiteral("journalctl"), QStringLiteral("crontab"),
        QStringLiteral("mount"), QStringLiteral("umount"), QStringLiteral("lsblk"),
        QStringLiteral("blkid"), QStringLiteral("useradd"), QStringLiteral("userdel"),
        QStringLiteral("usermod"), QStringLiteral("passwd"), QStringLiteral("groupadd"),
        QStringLiteral("id"), QStringLiteral("who"), QStringLiteral("w"), QStringLiteral("last"),
        QStringLiteral("uname"), QStringLiteral("hostname"), QStringLiteral("date"),
        QStringLiteral("uptime"), QStringLiteral("dmesg"), QStringLiteral("lsof"),
        QStringLiteral("env"), QStringLiteral("xargs"), QStringLiteral("tee"),
        QStringLiteral("watch"), QStringLiteral("nohup"), QStringLiteral("git"),
        QStringLiteral("docker"), QStringLiteral("podman"), QStringLiteral("kubectl"),
        QStringLiteral("apt"), QStringLiteral("apt-get"), QStringLiteral("dpkg"),
        QStringLiteral("dnf"), QStringLiteral("yum"), QStringLiteral("rpm"),
        QStringLiteral("snap"), QStringLiteral("pip"), QStringLiteral("pip3"),
        QStringLiteral("npm"), QStringLiteral("node"), QStringLiteral("python3"),
        QStringLiteral("make"), QStringLiteral("nano"), QStringLiteral("vim"),
        QStringLiteral("openssl"), QStringLiteral("base64"), QStringLiteral("md5sum"),
        QStringLiteral("sha256sum"), QStringLiteral("basename"), QStringLiteral("dirname"),
        QStringLiteral("realpath"), QStringLiteral("readlink"), QStringLiteral("which"),
        QStringLiteral("chattr"), QStringLiteral("lsattr"), QStringLiteral("nice"),
        QStringLiteral("renice"), QStringLiteral("timeout"), QStringLiteral("sleep"),
        QStringLiteral("ufw"), QStringLiteral("certbot"), QStringLiteral("mysql"),
        QStringLiteral("psql"), QStringLiteral("redis-cli"), QStringLiteral("less"),
        QStringLiteral("top"), QStringLiteral("htop"), QStringLiteral("sudo"),
        QStringLiteral("tmux"), QStringLiteral("screen"), QStringLiteral("gpg"),
    };
    return tools;
}

// Befehle mit Unterbefehl: die Hilfe gilt dann dem Unterbefehl.
const QSet<QString> &subcommandTools()
{
    static const QSet<QString> tools = {
        QStringLiteral("git"), QStringLiteral("docker"), QStringLiteral("podman"),
        QStringLiteral("kubectl"), QStringLiteral("systemctl"), QStringLiteral("apt"),
        QStringLiteral("npm"), QStringLiteral("pip"), QStringLiteral("pip3"),
    };
    return tools;
}

QString platformOf(const QString &osType)
{
    return osType == QLatin1String("windows") ? QStringLiteral("windows")
                                              : QStringLiteral("posix");
}

// Woerter des Templates vor dem ersten Platzhalter ("tar -czf {archive}" -> tar, -czf).
QStringList literalPrefix(const QString &templateText)
{
    const int brace = templateText.indexOf(QLatin1Char('{'));
    const QString head = brace < 0 ? templateText : templateText.left(brace);
    return head.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

QString quoteIfNeeded(const QString &value)
{
    if (value.isEmpty() || !value.contains(QLatin1Char(' ')) || value.startsWith(QLatin1Char('"'))
        || value.startsWith(QLatin1Char('\'')))
        return value;
    return QLatin1Char('"') + value + QLatin1Char('"');
}

} // namespace

QString CommandOption::display() const
{
    if (!literal.isEmpty())
        return literal;
    QStringList flags;
    if (!shortFlag.isEmpty())
        flags << shortFlag;
    if (!longFlag.isEmpty())
        flags << longFlag;
    QString out = flags.join(QStringLiteral(", "));
    if (!arg.isEmpty())
        out += (argWithEquals ? QStringLiteral("=") : QStringLiteral(" ")) + arg;
    return out;
}

QString CommandOption::insertText(const QString &value) const
{
    if (!literal.isEmpty())
        return literal;
    // Kurzform bevorzugen (kompakter, ueberall gleich).
    const QString flag = !shortFlag.isEmpty() ? shortFlag : longFlag;
    if (arg.isEmpty())
        return flag;
    const QString v = quoteIfNeeded(value);
    if (flag.startsWith(QLatin1String("--")) && argWithEquals)
        return flag + QLatin1Char('=') + v;
    return v.isEmpty() ? flag : flag + QLatin1Char(' ') + v;
}

QStringList commandTokens(const QString &line)
{
    QStringList tokens = line.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    static const QRegularExpression reAssign(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*="));
    while (!tokens.isEmpty()) {
        const QString t = tokens.first();
        if (t == QLatin1String("sudo")) {
            tokens.removeFirst();
            // sudo-Optionen; -u/-g erwarten einen Wert.
            while (!tokens.isEmpty() && tokens.first().startsWith(QLatin1Char('-'))) {
                const QString opt = tokens.takeFirst();
                if ((opt == QLatin1String("-u") || opt == QLatin1String("-g")) && !tokens.isEmpty())
                    tokens.removeFirst();
            }
        } else if (reAssign.match(t).hasMatch() || t == QLatin1String("nohup")
                   || t == QLatin1String("time")) {
            tokens.removeFirst();
        } else {
            break;
        }
    }
    return tokens;
}

QString commandName(const QString &line, const QString &osType)
{
    const QStringList tokens = commandTokens(line);
    if (tokens.isEmpty())
        return {};
    QString name = tokens.first();
    const int slash = std::max(name.lastIndexOf(QLatin1Char('/')), name.lastIndexOf(QLatin1Char('\\')));
    if (slash >= 0)
        name = name.mid(slash + 1);
    if (platformOf(osType) == QLatin1String("windows"))
        name = name.toLower();
    return name;
}

bool isKnownCommand(const QString &name, const QString &osType)
{
    if (name.isEmpty())
        return false;
    const QString platform = platformOf(osType);
    if (platform == QLatin1String("posix") && knownPosixTools().contains(name))
        return true;
    for (const CommandSpec &spec : catalog()) {
        if (spec.platform != platform && spec.platform != QLatin1String("any"))
            continue;
        if (commandName(spec.templateText, osType) == name)
            return true;
    }
    return false;
}

std::vector<CommandSpec> specsForLine(const QString &line, const QString &osType)
{
    const QString platform = platformOf(osType);
    const QString name = commandName(line, osType);
    const QStringList typed = commandTokens(line);
    std::vector<CommandSpec> all;
    for (const CommandSpec &spec : catalog()) {
        if (spec.platform != platform && spec.platform != QLatin1String("any"))
            continue;
        if (!name.isEmpty() && commandName(spec.templateText, osType) == name)
            all.push_back(spec);
    }
    // Unterbefehl getippt ("git commit")? Dann nur die passenden Varianten.
    if (all.size() > 1 && typed.size() >= 2 && !typed.at(1).startsWith(QLatin1Char('-'))) {
        std::vector<CommandSpec> matching;
        for (const CommandSpec &spec : all) {
            const QStringList lit = commandTokens(literalPrefix(spec.templateText).join(QLatin1Char(' ')));
            if (lit.size() >= 2 && lit.at(1) == typed.at(1))
                matching.push_back(spec);
        }
        if (!matching.empty())
            return matching;
    }
    return all;
}

std::vector<CommandOption> optionsFromSpec(const CommandSpec &spec)
{
    std::vector<CommandOption> out;
    for (const CommandParam &p : spec.params) {
        if (p.kind != QLatin1String("flag") || p.flagValue.trimmed().isEmpty())
            continue;
        CommandOption o;
        o.literal = p.flagValue.trimmed();
        o.description = p.description.isEmpty() ? p.label : p.label + QStringLiteral(" — ") + p.description;
        out.push_back(o);
    }
    return out;
}

std::vector<CommandParam> positionalParams(const CommandSpec &spec)
{
    std::vector<std::pair<int, CommandParam>> found;
    for (const CommandParam &p : spec.params) {
        if (p.kind == QLatin1String("flag"))
            continue;
        const int at = spec.templateText.indexOf(QLatin1Char('{') + p.name + QLatin1Char('}'));
        if (at >= 0)
            found.emplace_back(at, p);
    }
    std::stable_sort(found.begin(), found.end(),
                     [](const auto &a, const auto &b) { return a.first < b.first; });
    std::vector<CommandParam> out;
    for (const auto &f : found)
        out.push_back(f.second);
    return out;
}

QString positionalAppend(const CommandSpec &spec, const QHash<QString, QString> &values,
                         const QString &typedLine)
{
    // Schalter bleiben aus (die waehlt man in der Optionsliste), Werte mit
    // Leerzeichen werden gequotet.
    QHash<QString, QString> filled;
    for (const CommandParam &p : spec.params) {
        if (p.kind == QLatin1String("flag"))
            filled.insert(p.name, QStringLiteral("0"));
        else
            filled.insert(p.name, quoteIfNeeded(values.value(p.name).trimmed()));
    }
    const QString rendered = render(spec, filled).simplified();
    const QStringList literal = literalPrefix(spec.templateText);
    const QString literalText = literal.join(QLatin1Char(' '));
    QString rest = rendered.startsWith(literalText) ? rendered.mid(literalText.size()).trimmed()
                                                     : rendered;
    // Befehlswoerter, die noch fehlen (z. B. "-czf" bei "tar"), voranstellen;
    // sudo nie — das steht vorne oder gar nicht.
    const QStringList typed = typedLine.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QStringList missing;
    for (const QString &word : literal) {
        if (word == QLatin1String("sudo"))
            continue;
        if (!typed.contains(word))
            missing << word;
    }
    if (!rest.isEmpty())
        missing << rest;
    return missing.join(QLatin1Char(' ')).trimmed();
}

QString helpCommandFor(const QString &line)
{
    const QStringList tokens = commandTokens(line);
    if (tokens.isEmpty())
        return {};
    QString name = tokens.first();
    static const QRegularExpression reName(QStringLiteral("^[A-Za-z0-9._+-]+$"));
    static const QRegularExpression reSub(QStringLiteral("^[a-z][a-z0-9-]*$"));
    if (!reName.match(name).hasMatch())
        return {};
    bool known = knownPosixTools().contains(name);
    if (!known) {
        for (const CommandSpec &spec : catalog())
            if (spec.platform != QLatin1String("windows")
                && commandName(spec.templateText, QStringLiteral("posix")) == name) {
                known = true;
                break;
            }
    }
    if (!known)
        return {};
    QString cmd = name;
    if (subcommandTools().contains(name) && tokens.size() >= 2 && reSub.match(tokens.at(1)).hasMatch()) {
        // git <sub> --help oeffnet die Manpage — "-h" liefert die Kurzhilfe.
        cmd += QLatin1Char(' ') + tokens.at(1)
               + (name == QLatin1String("git") ? QStringLiteral(" -h") : QStringLiteral(" --help"));
    } else if (name == QLatin1String("ps")) {
        cmd += QStringLiteral(" --help all");   // "--help" allein zeigt nur Themen
    } else {
        cmd += QStringLiteral(" --help");
    }
    return QStringLiteral("LC_ALL=C %1 2>&1 | head -n 400").arg(cmd);
}

std::vector<CommandOption> parseHelpOptions(const QString &helpText)
{
    static const QRegularExpression reFlag(QStringLiteral("^--?[A-Za-z0-9?#][A-Za-z0-9_-]*$"));
    static const QRegularExpression reGap(QStringLiteral("\\s{2,}"));
    std::vector<CommandOption> out;
    QSet<QString> seen;
    int last = -1;   // Index (Zeiger wuerden beim Wachsen ungueltig)
    int lastIndent = 0;
    for (QString line : helpText.split(QLatin1Char('\n'))) {
        line.replace(QLatin1Char('\t'), QStringLiteral("    "));
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        int indent = 0;
        while (indent < line.size() && line.at(indent) == QLatin1Char(' '))
            ++indent;
        const QString body = line.mid(indent).trimmed();
        if (body.isEmpty()) {
            last = -1;
            continue;
        }
        // Optionszeilen beginnen mit "-" (rsync ohne Einrueckung, sonst eingerueckt).
        if (!body.startsWith(QLatin1Char('-')) || indent > 12) {
            // Fortsetzung einer Beschreibung (tiefer eingerueckt).
            if (last >= 0 && indent >= lastIndent + 4 && out[last].description.size() < 400)
                out[last].description = (out[last].description + QLatin1Char(' ') + body).trimmed();
            else
                last = -1;
            continue;
        }
        // Spezifikation und Beschreibung trennt eine Luecke aus 2+ Leerzeichen.
        QString spec = body;
        QString desc;
        const QRegularExpressionMatch gap = reGap.match(body);
        if (gap.hasMatch()) {
            spec = body.left(gap.capturedStart());
            desc = body.mid(gap.capturedEnd()).trimmed();
        }
        // Woerter der Spezifikation der Reihe nach: Flags bilden Optionen,
        // andere Woerter sind deren Argument. Kurz- und Langform ("-a, --all",
        // "-h --help", "--verbose, -v") gehoeren zusammen; mehrere gleichartige
        // ("-depth -maxdepth LEVELS" bei find) sind je eine eigene Option.
        std::vector<CommandOption> lineOpts;
        spec.replace(QLatin1Char(','), QStringLiteral(" , "));
        bool afterComma = false;
        for (QString word : spec.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            if (word == QLatin1String(",")) {
                afterComma = true;
                continue;
            }
            const bool comma = std::exchange(afterComma, false);
            word.remove(QStringLiteral("[no-]"));   // git: --[no-]quiet
            if (!word.startsWith(QLatin1Char('-'))) {
                if (!lineOpts.empty() && lineOpts.back().arg.isEmpty())
                    lineOpts.back().arg = word;
                continue;
            }
            int cut = word.size();
            for (const QChar ch : {QLatin1Char('='), QLatin1Char('[')}) {
                const int i = word.indexOf(ch);
                if (i > 0 && i < cut)
                    cut = i;
            }
            const QString flag = word.left(cut);
            QString arg = word.mid(cut);
            const bool equals = arg.startsWith(QLatin1Char('=')) || arg.startsWith(QLatin1String("[="));
            arg.remove(QRegularExpression(QStringLiteral("^\\[?=?")));
            if (arg.endsWith(QLatin1Char(']')) && !arg.contains(QLatin1Char('[')))
                arg.chop(1);
            if (!reFlag.match(flag).hasMatch())
                continue;
            const bool isLong = flag.startsWith(QLatin1String("--"));
            const bool isShort = !isLong && flag.size() == 2;
            CommandOption *cur = lineOpts.empty() ? nullptr : &lineOpts.back();
            const bool merge = cur && cur->arg.isEmpty()
                               && ((isLong && cur->longFlag.isEmpty() && cur->shortFlag.size() == 2)
                                   || (isShort && cur->shortFlag.isEmpty() && !cur->longFlag.isEmpty()));
            // "-A, -e" (ps): gleichwertige Schreibweise — nur die erste zeigen.
            if (!merge && comma && cur && cur->arg.isEmpty()
                && (isLong ? !cur->longFlag.isEmpty() : !cur->shortFlag.isEmpty()))
                continue;
            if (!merge) {
                lineOpts.emplace_back();
                cur = &lineOpts.back();
            }
            if (isLong)
                cur->longFlag = flag;
            else
                cur->shortFlag = flag;
            if (cur->arg.isEmpty() && !arg.isEmpty()) {
                cur->arg = arg;
                cur->argWithEquals = equals;
            }
        }
        last = -1;
        if (lineOpts.empty())
            continue;
        for (CommandOption &opt : lineOpts) {
            const QString key = opt.shortFlag + QLatin1Char('|') + opt.longFlag;
            if (seen.contains(key))
                continue;
            seen.insert(key);
            // Beschreibung nur bei genau einer Option je Zeile eindeutig.
            if (lineOpts.size() == 1)
                opt.description = desc;
            out.push_back(opt);
            if (lineOpts.size() == 1) {
                last = int(out.size()) - 1;
                lastIndent = indent;
            }
        }
    }
    return out;
}

} // namespace ncssh::core
