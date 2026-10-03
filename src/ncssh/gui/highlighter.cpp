#include "ncssh/gui/highlighter.hpp"

#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QStringList>

namespace ncssh::gui {

namespace {
// Farbpalette (an das dunkle Standard-Theme angelehnt).
QColor colKeyword()  { return QColor(QStringLiteral("#c678dd")); }
QColor colString()   { return QColor(QStringLiteral("#98c379")); }
QColor colNumber()   { return QColor(QStringLiteral("#d19a66")); }
QColor colComment()  { return QColor(QStringLiteral("#7f848e")); }
QColor colKey()      { return QColor(QStringLiteral("#61afef")); }
QColor colTag()      { return QColor(QStringLiteral("#e06c75")); }
QColor colBuiltin()  { return QColor(QStringLiteral("#56b6c2")); }
QColor colType()     { return QColor(QStringLiteral("#e5c07b")); }

QTextCharFormat fmt(const QColor &color, bool bold = false, bool italic = false)
{
    QTextCharFormat f;
    f.setForeground(color);
    if (bold)
        f.setFontWeight(QFont::Bold);
    f.setFontItalic(italic);
    return f;
}

// \b(?:a|b|c)\b aus einer Leerzeichen-getrennten Wortliste.
QString words(const char *list)
{
    QStringList parts = QString::fromLatin1(list).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (QString &p : parts)
        p = QRegularExpression::escape(p);
    return QStringLiteral("\\b(?:%1)\\b").arg(parts.join(QLatin1Char('|')));
}

const QString kNumber =
    QStringLiteral("\\b(?:0[xX][0-9a-fA-F]+|\\d+(?:\\.\\d+)?(?:[eE][+-]?\\d+)?)\\b");
const QString kCall = QStringLiteral("\\b([A-Za-z_]\\w*)(?=\\s*\\()");
// '#' nur als Kommentar, wenn davor nichts (ausser Leerraum) steht — sonst
// waeren $# oder ${#var} in Shell-Skripten Kommentare.
const QString kHashComment = QStringLiteral("(?<![^\\s])#");

const QHash<QString, QString> &extensionMap()
{
    static const QHash<QString, QString> map = [] {
        QHash<QString, QString> m;
        const auto add = [&m](const QString &lang, const char *exts) {
            for (const QString &e :
                 QString::fromLatin1(exts).split(QLatin1Char(' '), Qt::SkipEmptyParts))
                m.insert(e, lang);
        };
        add(QStringLiteral("json"), "json jsonc json5 geojson webmanifest");
        add(QStringLiteral("xml"), "xml html htm xhtml svg xaml csproj vcxproj props targets "
                                   "plist resx xsd xsl xslt wsdl vue");
        add(QStringLiteral("yaml"), "yaml yml");
        add(QStringLiteral("python"), "py pyw pyi");
        add(QStringLiteral("ini"), "ini toml cfg conf properties env service desktop "
                                   "timer socket mount inf reg");
        add(QStringLiteral("shell"), "sh bash zsh ksh");
        add(QStringLiteral("cpp"), "c h cpp hpp cc cxx hh hxx ino ipp tpp m mm");
        add(QStringLiteral("csharp"), "cs csx");
        add(QStringLiteral("java"), "java kt kts groovy gradle scala");
        add(QStringLiteral("javascript"), "js mjs cjs jsx ts tsx");
        add(QStringLiteral("css"), "css scss sass less");
        add(QStringLiteral("sql"), "sql ddl psql");
        add(QStringLiteral("php"), "php phtml php3 php4 php5 phps");
        add(QStringLiteral("go"), "go");
        add(QStringLiteral("rust"), "rs");
        add(QStringLiteral("powershell"), "ps1 psm1 psd1");
        add(QStringLiteral("batch"), "bat cmd");
        add(QStringLiteral("markdown"), "md markdown mdown mkd");
        add(QStringLiteral("ruby"), "rb rake gemspec ru");
        add(QStringLiteral("lua"), "lua");
        add(QStringLiteral("makefile"), "mk mak");
        add(QStringLiteral("dockerfile"), "dockerfile containerfile");
        return m;
    }();
    return map;
}

const QHash<QString, QString> &fileNameMap()
{
    static const QHash<QString, QString> map = {
        {QStringLiteral("dockerfile"), QStringLiteral("dockerfile")},
        {QStringLiteral("containerfile"), QStringLiteral("dockerfile")},
        {QStringLiteral("makefile"), QStringLiteral("makefile")},
        {QStringLiteral("gnumakefile"), QStringLiteral("makefile")},
        {QStringLiteral(".bashrc"), QStringLiteral("shell")},
        {QStringLiteral(".bash_profile"), QStringLiteral("shell")},
        {QStringLiteral(".bash_aliases"), QStringLiteral("shell")},
        {QStringLiteral(".bash_logout"), QStringLiteral("shell")},
        {QStringLiteral(".profile"), QStringLiteral("shell")},
        {QStringLiteral(".zshrc"), QStringLiteral("shell")},
        {QStringLiteral(".zprofile"), QStringLiteral("shell")},
        {QStringLiteral(".kshrc"), QStringLiteral("shell")},
        {QStringLiteral(".gitconfig"), QStringLiteral("ini")},
        {QStringLiteral(".editorconfig"), QStringLiteral("ini")},
        {QStringLiteral(".npmrc"), QStringLiteral("ini")},
        {QStringLiteral(".env"), QStringLiteral("ini")},
        {QStringLiteral("gemfile"), QStringLiteral("ruby")},
        {QStringLiteral("rakefile"), QStringLiteral("ruby")},
        {QStringLiteral("vagrantfile"), QStringLiteral("ruby")},
    };
    return map;
}
} // namespace

const std::vector<std::pair<QString, QString>> &SyntaxHighlighter::languages()
{
    static const std::vector<std::pair<QString, QString>> list = {
        {QStringLiteral("batch"), QStringLiteral("Batch (CMD)")},
        {QStringLiteral("cpp"), QStringLiteral("C / C++")},
        {QStringLiteral("csharp"), QStringLiteral("C#")},
        {QStringLiteral("css"), QStringLiteral("CSS / SCSS")},
        {QStringLiteral("dockerfile"), QStringLiteral("Dockerfile")},
        {QStringLiteral("go"), QStringLiteral("Go")},
        {QStringLiteral("ini"), QStringLiteral("INI / TOML / Conf")},
        {QStringLiteral("java"), QStringLiteral("Java / Kotlin")},
        {QStringLiteral("javascript"), QStringLiteral("JavaScript / TypeScript")},
        {QStringLiteral("json"), QStringLiteral("JSON")},
        {QStringLiteral("lua"), QStringLiteral("Lua")},
        {QStringLiteral("makefile"), QStringLiteral("Makefile")},
        {QStringLiteral("markdown"), QStringLiteral("Markdown")},
        {QStringLiteral("php"), QStringLiteral("PHP")},
        {QStringLiteral("powershell"), QStringLiteral("PowerShell")},
        {QStringLiteral("python"), QStringLiteral("Python")},
        {QStringLiteral("ruby"), QStringLiteral("Ruby")},
        {QStringLiteral("rust"), QStringLiteral("Rust")},
        {QStringLiteral("shell"), QStringLiteral("Shell (Bash)")},
        {QStringLiteral("sql"), QStringLiteral("SQL")},
        {QStringLiteral("xml"), QStringLiteral("XML / HTML")},
        {QStringLiteral("yaml"), QStringLiteral("YAML")},
    };
    return list;
}

QString SyntaxHighlighter::displayName(const QString &language)
{
    for (const auto &[id, name] : languages())
        if (id == language)
            return name;
    return {};
}

QString SyntaxHighlighter::languageForFile(const QString &fileName)
{
    const QString base = QFileInfo(fileName).fileName().toLower();
    if (const auto it = fileNameMap().constFind(base); it != fileNameMap().constEnd())
        return it.value();
    // "Dockerfile.dev", "Makefile.win" …
    if (base.startsWith(QLatin1String("dockerfile.")))
        return QStringLiteral("dockerfile");
    if (base.startsWith(QLatin1String("makefile.")))
        return QStringLiteral("makefile");
    const int dot = base.lastIndexOf(QLatin1Char('.'));
    if (dot < 0)
        return {};
    return extensionMap().value(base.mid(dot + 1));
}

QString SyntaxHighlighter::detectLanguage(const QString &fileName, const QString &content)
{
    const QString byName = languageForFile(fileName);
    if (!byName.isEmpty())
        return byName;

    // Shebang: "#!/usr/bin/env python3", "#!/bin/bash" …
    const QString firstLine = content.section(QLatin1Char('\n'), 0, 0).trimmed();
    if (firstLine.startsWith(QLatin1String("#!"))) {
        static const std::vector<std::pair<const char *, const char *>> interpreters = {
            {"python", "python"}, {"bash", "shell"}, {"zsh", "shell"}, {"ksh", "shell"},
            {"dash", "shell"},    {"/sh", "shell"},  {" sh", "shell"}, {"node", "javascript"},
            {"deno", "javascript"}, {"ruby", "ruby"}, {"php", "php"},  {"lua", "lua"},
            {"pwsh", "powershell"},
        };
        for (const auto &[needle, lang] : interpreters)
            if (firstLine.contains(QLatin1String(needle)))
                return QString::fromLatin1(lang);
    }

    const QString head = content.left(4000).trimmed();
    if (head.startsWith(QLatin1String("<?php")))
        return QStringLiteral("php");
    if (head.startsWith(QLatin1String("<?xml"))
        || head.startsWith(QLatin1String("<!DOCTYPE"), Qt::CaseInsensitive)
        || head.startsWith(QLatin1String("<html"), Qt::CaseInsensitive)
        || head.startsWith(QLatin1String("<svg")))
        return QStringLiteral("xml");
    if ((head.startsWith(QLatin1Char('{')) || head.startsWith(QLatin1Char('[')))
        && content.size() < 2'000'000) {
        QJsonParseError err;
        QJsonDocument::fromJson(content.toUtf8(), &err);
        if (err.error == QJsonParseError::NoError)
            return QStringLiteral("json");
    }
    if (head.startsWith(QLatin1String("---\n")) || head.startsWith(QLatin1String("%YAML")))
        return QStringLiteral("yaml");
    // Erste "echte" Zeile eine [Sektion]? -> INI
    for (const QString &line : head.split(QLatin1Char('\n'))) {
        const QString t = line.trimmed();
        if (t.isEmpty() || t.startsWith(QLatin1Char('#')) || t.startsWith(QLatin1Char(';')))
            continue;
        static const QRegularExpression section(QStringLiteral("^\\[[^\\]]+\\]$"));
        if (section.match(t).hasMatch())
            return QStringLiteral("ini");
        break;
    }
    return {};
}

SyntaxHighlighter::SyntaxHighlighter(QTextDocument *document, const QString &language)
    : QSyntaxHighlighter(document)
{
    setupRules(language);
}

void SyntaxHighlighter::setLanguage(const QString &language)
{
    if (language == m_language)
        return;
    setupRules(language);
    rehighlight();
}

void SyntaxHighlighter::setupRules(const QString &language)
{
    m_language = language;
    m_rules.clear();
    m_blocks.clear();
    m_lineComment = QRegularExpression();
    m_stringDelims.clear();
    m_escapes = true;
    m_stringFormat = fmt(colString());
    m_commentFormat = fmt(colComment(), false, true);

    const auto rule = [this](const QString &pattern, const QTextCharFormat &format,
                             bool caseInsensitive = false) {
        m_rules.push_back({QRegularExpression(pattern,
                                              caseInsensitive
                                                  ? QRegularExpression::CaseInsensitiveOption
                                                  : QRegularExpression::NoPatternOption),
                           format});
    };
    const auto lineComment = [this](const QString &pattern, bool caseInsensitive = false) {
        m_lineComment = QRegularExpression(
            pattern, caseInsensitive ? QRegularExpression::CaseInsensitiveOption
                                     : QRegularExpression::NoPatternOption);
    };
    const auto block = [this](const QString &start, const QString &end,
                              const QTextCharFormat &format) {
        m_blocks.push_back({start, end, format});
    };
    // Gemeinsamer Rahmen der C-artigen Sprachen.
    const auto cLike = [&](const QString &keywords, const QString &types,
                           const QString &constants) {
        rule(kCall, fmt(colKey()));
        rule(keywords, fmt(colKeyword(), true));
        if (!types.isEmpty())
            rule(types, fmt(colType()));
        if (!constants.isEmpty())
            rule(constants, fmt(colBuiltin()));
        rule(kNumber, fmt(colNumber()));
        lineComment(QStringLiteral("//"));
        block(QStringLiteral("/*"), QStringLiteral("*/"), m_commentFormat);
        m_stringDelims = QStringLiteral("\"'");
    };

    if (language == QLatin1String("json")) {
        // Keine String-Erkennung im Scanner: Schluessel ("x":) und Werte
        // brauchen verschiedene Farben — die Regeln unterscheiden sie.
        rule(QStringLiteral("\"(?:[^\"\\\\]|\\\\.)*\""), fmt(colString()));
        rule(QStringLiteral("\"(?:[^\"\\\\]|\\\\.)*\"(?=\\s*:)"), fmt(colKey(), true));
        rule(QStringLiteral("(?<![\\w\"])-?\\d+(?:\\.\\d+)?(?:[eE][+-]?\\d+)?\\b"),
             fmt(colNumber()));
        rule(QStringLiteral("\\b(?:true|false|null)\\b"), fmt(colKeyword()));
    } else if (language == QLatin1String("xml")) {
        rule(QStringLiteral("</?[\\w:.-]+"), fmt(colTag(), true));
        rule(QStringLiteral("/?>"), fmt(colTag(), true));
        rule(QStringLiteral("[\\w:.-]+(?=\\s*=)"), fmt(colKey()));
        rule(QStringLiteral("\"[^\"]*\"|'[^']*'"), fmt(colString()));
        rule(QStringLiteral("&\\w+;|&#\\d+;"), fmt(colBuiltin()));
        rule(QStringLiteral("<\\?[\\s\\S]*?\\?>|<!DOCTYPE[^>]*>"), fmt(colComment()));
        block(QStringLiteral("<!--"), QStringLiteral("-->"), m_commentFormat);
    } else if (language == QLatin1String("yaml")) {
        rule(QStringLiteral("^\\s*(?:-\\s+)?[\\w.\"'/-]+\\s*:(?=\\s|$)"), fmt(colKey(), true));
        rule(QStringLiteral("\"(?:[^\"\\\\]|\\\\.)*\"|'[^']*'"), fmt(colString()));
        rule(QStringLiteral("(?<![\\w.])-?\\d+(?:\\.\\d+)?\\b"), fmt(colNumber()));
        rule(QStringLiteral("\\b(?:true|false|null|yes|no|on|off|True|False|Null)\\b"),
             fmt(colKeyword()));
        rule(QStringLiteral("[&*][\\w-]+|![\\w!]+"), fmt(colBuiltin()));
        rule(QStringLiteral("^(?:---|\\.\\.\\.)\\s*$"), fmt(colTag(), true));
        lineComment(kHashComment);
    } else if (language == QLatin1String("python")) {
        rule(kCall, fmt(colKey()));
        rule(words("and as assert async await break class continue def del elif else except "
                   "finally for from global if import in is lambda match case nonlocal not or "
                   "pass raise return try while with yield"),
             fmt(colKeyword(), true));
        rule(QStringLiteral("\\b(?:True|False|None|self|cls)\\b"), fmt(colBuiltin()));
        rule(words("int float str bool list dict set tuple bytes object type len range print "
                   "open isinstance super enumerate zip map filter sorted min max sum any all"),
             fmt(colType()));
        rule(QStringLiteral("\\bdef\\s+(\\w+)"), fmt(colKey(), true));
        rule(QStringLiteral("\\bclass\\s+(\\w+)"), fmt(colType(), true));
        rule(QStringLiteral("^\\s*@[\\w.]+"), fmt(colBuiltin()));
        rule(kNumber, fmt(colNumber()));
        lineComment(QStringLiteral("#"));
        block(QStringLiteral("\"\"\""), QStringLiteral("\"\"\""), m_stringFormat);
        block(QStringLiteral("'''"), QStringLiteral("'''"), m_stringFormat);
        m_stringDelims = QStringLiteral("\"'");
    } else if (language == QLatin1String("ini")) {
        rule(QStringLiteral("^\\s*\\[[^\\]]+\\]"), fmt(colTag(), true));   // Sektion
        rule(QStringLiteral("^\\s*[\\w.-]+(?=\\s*[=:])"), fmt(colKey()));   // Schluessel
        rule(QStringLiteral("\"[^\"]*\"|'[^']*'"), fmt(colString()));
        rule(QStringLiteral("(?<![\\w.])-?\\d+(?:\\.\\d+)?\\b"), fmt(colNumber()));
        rule(QStringLiteral("\\b(?:true|false|yes|no|on|off)\\b"), fmt(colKeyword()),
             /*caseInsensitive=*/true);
        // Kommentare nur am Zeilenanfang — ";" und "#" kommen in Werten vor.
        lineComment(QStringLiteral("^\\s*[#;]"));
    } else if (language == QLatin1String("shell")) {
        rule(words("if then else elif fi for while until do done case esac function return "
                   "export local readonly declare in select break continue exit source"),
             fmt(colKeyword(), true));
        rule(words("echo printf cd ls cp mv rm mkdir chmod chown grep sed awk cat test "
                   "sudo set unset shift eval exec trap read"),
             fmt(colType()));
        rule(QStringLiteral("\\$\\{[^}]*\\}|\\$\\(|\\$[\\w@#?$!*-]"), fmt(colBuiltin()));
        rule(QStringLiteral("^\\s*[\\w-]+(?=\\s*\\(\\s*\\))"), fmt(colKey(), true));
        rule(QStringLiteral("(?<![\\w.-])\\d+\\b"), fmt(colNumber()));
        lineComment(kHashComment);
        m_stringDelims = QStringLiteral("\"'");
    } else if (language == QLatin1String("cpp")) {
        cLike(words("alignas alignof asm auto break case catch class const consteval constexpr "
                    "constinit const_cast continue co_await co_return co_yield decltype default "
                    "delete do dynamic_cast else enum explicit export extern final for friend "
                    "goto if inline mutable namespace new noexcept operator override private "
                    "protected public register reinterpret_cast return sizeof static "
                    "static_assert static_cast struct switch template this thread_local throw "
                    "try typedef typeid typename union using virtual volatile while"),
              words("bool char char8_t char16_t char32_t double float int long short signed "
                    "unsigned void wchar_t size_t ssize_t ptrdiff_t int8_t int16_t int32_t "
                    "int64_t uint8_t uint16_t uint32_t uint64_t std string vector map"),
              words("true false nullptr NULL this"));
        rule(QStringLiteral("^\\s*#\\s*\\w+"), fmt(colTag(), true));   // Praeprozessor
        rule(QStringLiteral("(?<=#include)\\s*<[^>]+>"), fmt(colString()));
    } else if (language == QLatin1String("csharp")) {
        cLike(words("abstract as async await base break case catch checked class const "
                    "continue default delegate do else enum event explicit extern finally fixed "
                    "for foreach get goto if implicit in init interface internal is lock "
                    "namespace new operator out override params partial private protected "
                    "public readonly record ref return sealed set sizeof stackalloc static "
                    "struct switch this throw try typeof unchecked unsafe using value var "
                    "virtual volatile when where while yield"),
              words("bool byte char decimal double dynamic float int long nint nuint object "
                    "sbyte short string uint ulong ushort void Task List Dictionary"),
              words("true false null"));
        rule(QStringLiteral("^\\s*#\\s*\\w+"), fmt(colTag(), true));
        rule(QStringLiteral("^\\s*\\[[\\w.]+"), fmt(colBuiltin()));   // Attribute
    } else if (language == QLatin1String("java")) {
        cLike(words("abstract assert break case catch class const continue default do else "
                    "enum extends final finally for goto if implements import instanceof "
                    "interface native new package private protected public return static "
                    "strictfp super switch synchronized this throw throws transient try "
                    "volatile while var record sealed permits yield fun val when object "
                    "companion data override open internal lateinit suspend"),
              words("boolean byte char double float int long short void String Integer Long "
                    "Boolean Object List Map Set Unit Any"),
              words("true false null"));
        rule(QStringLiteral("@\\w+"), fmt(colBuiltin()));   // Annotationen
    } else if (language == QLatin1String("javascript")) {
        cLike(words("abstract as async await break case catch class const continue debugger "
                    "declare default delete do else enum export extends finally for from "
                    "function get if implements import in instanceof interface keyof let "
                    "namespace new of private protected public readonly return set static "
                    "super switch this throw try type typeof var void while with yield"),
              words("string number boolean any unknown never object symbol bigint Array "
                    "Promise Map Set Date Error JSON Math Object"),
              words("true false null undefined NaN Infinity console window document"));
        rule(QStringLiteral("@\\w+"), fmt(colBuiltin()));   // Dekoratoren
        m_stringDelims = QStringLiteral("\"'`");
    } else if (language == QLatin1String("go")) {
        cLike(words("break case chan const continue default defer else fallthrough for func "
                    "go goto if import interface map package range return select struct "
                    "switch type var"),
              words("bool byte complex64 complex128 error float32 float64 int int8 int16 "
                    "int32 int64 rune string uint uint8 uint16 uint32 uint64 uintptr any"),
              words("true false nil iota append cap close copy delete len make new panic "
                    "print println recover"));
        m_stringDelims = QStringLiteral("\"'`");
    } else if (language == QLatin1String("rust")) {
        cLike(words("as async await break const continue crate dyn else enum extern fn for "
                    "if impl in let loop match mod move mut pub ref return self Self static "
                    "struct super trait type unsafe use where while"),
              words("i8 i16 i32 i64 i128 isize u8 u16 u32 u64 u128 usize f32 f64 bool char "
                    "str String Vec Option Result Box HashMap"),
              words("true false Some None Ok Err"));
        rule(QStringLiteral("\\b\\w+!"), fmt(colBuiltin()));         // Makros
        rule(QStringLiteral("#!?\\[[^\\]]*\\]"), fmt(colTag()));     // Attribute
        m_stringDelims = QStringLiteral("\"");   // ' ist auch Lifetime ('a)
    } else if (language == QLatin1String("php")) {
        cLike(words("abstract and array as break callable case catch class clone const "
                    "continue declare default do echo else elseif empty enddeclare endfor "
                    "endforeach endif endswitch endwhile enum extends final finally fn for "
                    "foreach function global goto if implements include include_once "
                    "instanceof insteadof interface isset list match namespace new or print "
                    "private protected public readonly require require_once return static "
                    "switch throw trait try unset use var while xor yield"),
              words("int float string bool void mixed object iterable self parent"),
              words("true false null TRUE FALSE NULL"));
        rule(QStringLiteral("\\$\\w+"), fmt(colBuiltin()));
        rule(QStringLiteral("<\\?php|<\\?=|\\?>"), fmt(colTag(), true));
        lineComment(QStringLiteral("//|#(?!\\[)"));
    } else if (language == QLatin1String("css")) {
        rule(QStringLiteral("[.#]?[\\w-]+(?=[^{};]*\\{)"), fmt(colTag()));       // Selektoren
        rule(QStringLiteral("[\\w-]+(?=\\s*:[^:])"), fmt(colKey()));            // Eigenschaften
        rule(QStringLiteral("@[\\w-]+"), fmt(colKeyword(), true));
        rule(QStringLiteral("\\$[\\w-]+|--[\\w-]+"), fmt(colBuiltin()));        // Variablen
        rule(QStringLiteral("#[0-9a-fA-F]{3,8}\\b"), fmt(colNumber()));
        rule(QStringLiteral("(?<![\\w-])-?\\d+(?:\\.\\d+)?(?:px|em|rem|%|vh|vw|vmin|vmax|"
                            "s|ms|deg|pt|fr|ch|ex)?\\b"),
             fmt(colNumber()));
        rule(QStringLiteral("!important"), fmt(colKeyword(), true));
        block(QStringLiteral("/*"), QStringLiteral("*/"), m_commentFormat);
        m_stringDelims = QStringLiteral("\"'");
    } else if (language == QLatin1String("sql")) {
        rule(words("add all alter and any as asc begin between by cascade case check column "
                   "commit constraint create cross database declare default delete desc "
                   "distinct drop else end exists foreign from full function grant group "
                   "having if in index inner insert into is join key left like limit merge "
                   "not null offset on or order outer over partition primary procedure "
                   "references replace return returns revoke right rollback schema select "
                   "set show table then top transaction trigger truncate union unique update "
                   "use using values view when where with"),
             fmt(colKeyword(), true), /*caseInsensitive=*/true);
        rule(words("int integer bigint smallint tinyint serial bigserial varchar nvarchar "
                   "char nchar text date time datetime timestamp timestamptz boolean bool bit "
                   "decimal numeric float real double money uuid json jsonb blob clob"),
             fmt(colType()), /*caseInsensitive=*/true);
        rule(words("count sum avg min max coalesce nullif cast convert now current_date "
                   "current_timestamp upper lower substring trim length concat"),
             fmt(colBuiltin()), /*caseInsensitive=*/true);
        rule(kNumber, fmt(colNumber()));
        lineComment(QStringLiteral("--"));
        block(QStringLiteral("/*"), QStringLiteral("*/"), m_commentFormat);
        m_stringDelims = QStringLiteral("'\"`");
        m_escapes = false;   // SQL verdoppelt Quotes statt Backslash
    } else if (language == QLatin1String("powershell")) {
        rule(words("begin break catch class continue data do dynamicparam else elseif end "
                   "enum exit filter finally for foreach function hidden if in param process "
                   "return static switch throw trap try until using while"),
             fmt(colKeyword(), true), /*caseInsensitive=*/true);
        rule(QStringLiteral("-(?:eq|ne|gt|ge|lt|le|like|notlike|match|notmatch|contains|"
                            "notcontains|in|notin|and|or|not|xor|replace|split|join|is|isnot|"
                            "as|f)\\b"),
             fmt(colKeyword()), /*caseInsensitive=*/true);
        rule(QStringLiteral("\\b[A-Z][a-zA-Z]+-[A-Z]\\w+"), fmt(colKey()));   // Cmdlets
        rule(QStringLiteral("\\$[\\w:]+|\\$\\{[^}]+\\}"), fmt(colBuiltin()));
        rule(QStringLiteral("\\[[\\w.]+\\]"), fmt(colType()));
        rule(kNumber, fmt(colNumber()));
        lineComment(QStringLiteral("#"));
        block(QStringLiteral("<#"), QStringLiteral("#>"), m_commentFormat);
        m_stringDelims = QStringLiteral("\"'");
        m_escapes = false;   // Escape-Zeichen ist der Backtick
    } else if (language == QLatin1String("batch")) {
        rule(words("echo set if else goto call exit for in do not exist defined errorlevel "
                   "setlocal endlocal enabledelayedexpansion shift pause cls cd chdir pushd "
                   "popd start copy xcopy robocopy del move ren mkdir rmdir type title "
                   "equ neq lss leq gtr geq nul"),
             fmt(colKeyword(), true), /*caseInsensitive=*/true);
        rule(QStringLiteral("^\\s*:\\w+"), fmt(colTag(), true));           // Sprungmarken
        rule(QStringLiteral("%~?[\\w]*%|%%~?\\w|%~?\\d|!\\w+!"), fmt(colBuiltin()));
        rule(QStringLiteral("^\\s*@"), fmt(colTag()));
        lineComment(QStringLiteral("^\\s*(?:@?rem\\b|::)"), /*caseInsensitive=*/true);
        m_stringDelims = QStringLiteral("\"");
        m_escapes = false;
    } else if (language == QLatin1String("markdown")) {
        rule(QStringLiteral("^\\s*(?:[-*+]|\\d+\\.)\\s"), fmt(colTag(), true));
        rule(QStringLiteral("\\*\\*[^*]+\\*\\*|__[^_]+__"), fmt(colKeyword(), true));
        rule(QStringLiteral("(?<![*\\w])\\*[^*\\s][^*]*\\*(?!\\*)|(?<![_\\w])_[^_\\s][^_]*_(?![_\\w])"),
             fmt(colKeyword(), false, true));
        rule(QStringLiteral("!?\\[[^\\]]*\\]\\([^)]*\\)"), fmt(colBuiltin()));
        rule(QStringLiteral("`[^`]+`"), fmt(colString()));
        rule(QStringLiteral("^\\s*>.*$"), fmt(colComment(), false, true));
        rule(QStringLiteral("^#{1,6}\\s.*$"), fmt(colKey(), true));
        rule(QStringLiteral("^\\s*(?:-{3,}|\\*{3,}|_{3,})\\s*$"), fmt(colTag()));
        block(QStringLiteral("```"), QStringLiteral("```"), m_stringFormat);
    } else if (language == QLatin1String("dockerfile")) {
        rule(QStringLiteral("^\\s*(?:FROM|RUN|CMD|LABEL|MAINTAINER|EXPOSE|ENV|ADD|COPY|"
                            "ENTRYPOINT|VOLUME|USER|WORKDIR|ARG|ONBUILD|STOPSIGNAL|HEALTHCHECK|"
                            "SHELL)\\b"),
             fmt(colKeyword(), true), /*caseInsensitive=*/true);
        rule(QStringLiteral("\\bAS\\b"), fmt(colKeyword()), /*caseInsensitive=*/false);
        rule(QStringLiteral("\\$\\{?\\w+\\}?"), fmt(colBuiltin()));
        rule(QStringLiteral("--[\\w-]+"), fmt(colKey()));
        lineComment(QStringLiteral("^\\s*#"));
        m_stringDelims = QStringLiteral("\"'");
    } else if (language == QLatin1String("makefile")) {
        rule(QStringLiteral("^[\\w./%$(){}-]+(?=\\s*:(?!=))"), fmt(colKey(), true));   // Ziele
        rule(QStringLiteral("^\\s*[\\w.-]+(?=\\s*[:+?]?=)"), fmt(colTag()));             // Variablen
        rule(QStringLiteral("^\\s*(?:include|-include|ifeq|ifneq|ifdef|ifndef|else|endif|"
                            "define|endef|export|override|\\.PHONY)\\b"),
             fmt(colKeyword(), true));
        rule(QStringLiteral("\\$\\([^)]*\\)|\\$\\{[^}]*\\}|\\$[@<^*?%]"), fmt(colBuiltin()));
        lineComment(kHashComment);
    } else if (language == QLatin1String("ruby")) {
        rule(kCall, fmt(colKey()));
        rule(words("alias and begin break case class def defined do else elsif end ensure "
                   "for if in module next not or redo rescue retry return super then undef "
                   "unless until when while yield require require_relative include extend "
                   "attr_accessor attr_reader attr_writer private protected public puts"),
             fmt(colKeyword(), true));
        rule(QStringLiteral("\\b(?:true|false|nil|self)\\b"), fmt(colBuiltin()));
        rule(QStringLiteral("(?<![:\\w]):\\w+|@@?\\w+|\\$\\w+"), fmt(colBuiltin()));
        rule(QStringLiteral("\\bdef\\s+(?:self\\.)?(\\w+[?!]?)"), fmt(colKey(), true));
        rule(QStringLiteral("\\b(?:class|module)\\s+([\\w:]+)"), fmt(colType(), true));
        rule(kNumber, fmt(colNumber()));
        lineComment(kHashComment);
        block(QStringLiteral("=begin"), QStringLiteral("=end"), m_commentFormat);
        m_stringDelims = QStringLiteral("\"'");
    } else if (language == QLatin1String("lua")) {
        rule(kCall, fmt(colKey()));
        rule(words("and break do else elseif end for function goto if in local not or "
                   "repeat return then until while"),
             fmt(colKeyword(), true));
        rule(QStringLiteral("\\b(?:true|false|nil|self)\\b"), fmt(colBuiltin()));
        rule(words("print pairs ipairs require type tostring tonumber table string math os io"),
             fmt(colType()));
        rule(kNumber, fmt(colNumber()));
        // Blockkommentar vor dem Zeilenkommentar pruefen (beide beginnen mit --).
        block(QStringLiteral("--[["), QStringLiteral("]]"), m_commentFormat);
        lineComment(QStringLiteral("--"));
        m_stringDelims = QStringLiteral("\"'");
    }
}

void SyntaxHighlighter::highlightBlock(const QString &text)
{
    if (m_language.isEmpty())
        return;
    for (const Rule &rule : m_rules) {
        auto it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            const auto match = it.next();
            // Gibt es eine Capture-Group, nur diese einfaerben (z.B. def NAME).
            const int group = match.lastCapturedIndex() >= 1 ? 1 : 0;
            setFormat(match.capturedStart(group), match.capturedLength(group), rule.format);
        }
    }
    setCurrentBlockState(0);
    scanStrings(text, 0, int(text.size()));
}

// Laeuft von links nach rechts ueber Bloecke, Zeilenkommentare und Strings und
// ueberschreibt deren Bereiche. Was zuerst beginnt, gewinnt — damit ist // in
// einem String kein Kommentar und ein " in einem Kommentar kein String.
int SyntaxHighlighter::scanStrings(const QString &text, int from, int to)
{
    int i = from;
    // Offener Block aus der Vorzeile fortsetzen.
    const int prev = previousBlockState();
    if (prev >= 1 && prev <= int(m_blocks.size())) {
        const Block &b = m_blocks[size_t(prev - 1)];
        const int end = int(text.indexOf(b.end));
        if (end < 0) {
            setFormat(0, to, b.format);
            setCurrentBlockState(prev);
            return to;
        }
        setFormat(0, end + int(b.end.size()), b.format);
        i = end + int(b.end.size());
    }
    while (i < to) {
        bool consumed = false;
        for (size_t k = 0; k < m_blocks.size(); ++k) {
            const Block &b = m_blocks[k];
            if (!QStringView(text).mid(i).startsWith(b.start))
                continue;
            const int end = int(text.indexOf(b.end, i + int(b.start.size())));
            if (end < 0) {
                setFormat(i, to - i, b.format);
                setCurrentBlockState(int(k) + 1);
                return to;
            }
            const int stop = end + int(b.end.size());
            setFormat(i, stop - i, b.format);
            i = stop;
            consumed = true;
            break;
        }
        if (consumed)
            continue;
        if (m_lineComment.isValid() && !m_lineComment.pattern().isEmpty()
            && m_lineComment.match(text, i, QRegularExpression::NormalMatch,
                                   QRegularExpression::AnchorAtOffsetMatchOption)
                   .hasMatch()) {
            setFormat(i, to - i, m_commentFormat);
            return to;
        }
        const QChar c = text.at(i);
        if (m_stringDelims.contains(c)) {
            int j = i + 1;
            while (j < to) {
                if (m_escapes && text.at(j) == QLatin1Char('\\')) {
                    j += 2;
                    continue;
                }
                if (text.at(j) == c) {
                    ++j;
                    break;
                }
                ++j;
            }
            j = qMin(j, to);
            setFormat(i, j - i, m_stringFormat);
            i = j;
            continue;
        }
        ++i;
    }
    return to;
}

} // namespace ncssh::gui
