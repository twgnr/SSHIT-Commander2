#include "ncssh/gui/highlighter.hpp"

#include <QFileInfo>

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

QTextCharFormat fmt(const QColor &color, bool bold = false, bool italic = false)
{
    QTextCharFormat f;
    f.setForeground(color);
    if (bold)
        f.setFontWeight(QFont::Bold);
    f.setFontItalic(italic);
    return f;
}
} // namespace

QString SyntaxHighlighter::languageForFile(const QString &fileName)
{
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    const QString base = QFileInfo(fileName).fileName().toLower();
    if (suffix == QLatin1String("json")) return QStringLiteral("json");
    if (suffix == QLatin1String("xml") || suffix == QLatin1String("html")
        || suffix == QLatin1String("htm") || suffix == QLatin1String("svg"))
        return QStringLiteral("xml");
    if (suffix == QLatin1String("yaml") || suffix == QLatin1String("yml"))
        return QStringLiteral("yaml");
    if (suffix == QLatin1String("py")) return QStringLiteral("python");
    if (suffix == QLatin1String("ini") || suffix == QLatin1String("toml")
        || suffix == QLatin1String("cfg") || suffix == QLatin1String("conf"))
        return QStringLiteral("ini");
    if (suffix == QLatin1String("sh") || suffix == QLatin1String("bash")
        || base == QLatin1String(".bashrc") || base == QLatin1String(".profile"))
        return QStringLiteral("shell");
    return {};
}

SyntaxHighlighter::SyntaxHighlighter(QTextDocument *document, const QString &language)
    : QSyntaxHighlighter(document)
{
    setupRules(language);
}

void SyntaxHighlighter::setupRules(const QString &language)
{
    const auto addRule = [this](const QString &pattern, const QTextCharFormat &format,
                                QRegularExpression::PatternOptions opts =
                                    QRegularExpression::NoPatternOption) {
        m_rules.push_back({QRegularExpression(pattern, opts), format});
    };

    if (language == QLatin1String("json")) {
        addRule(QStringLiteral("\"(?:[^\"\\\\]|\\\\.)*\"\\s*:"), fmt(colKey(), true));   // Schluessel
        addRule(QStringLiteral("\"(?:[^\"\\\\]|\\\\.)*\""), fmt(colString()));           // Werte
        addRule(QStringLiteral("\\b-?\\d+(?:\\.\\d+)?(?:[eE][+-]?\\d+)?\\b"), fmt(colNumber()));
        addRule(QStringLiteral("\\b(?:true|false|null)\\b"), fmt(colKeyword()));
    } else if (language == QLatin1String("xml")) {
        addRule(QStringLiteral("</?[\\w:.-]+"), fmt(colTag(), true));
        addRule(QStringLiteral("/?>"), fmt(colTag(), true));
        addRule(QStringLiteral("[\\w:.-]+(?=\\s*=)"), fmt(colKey()));
        addRule(QStringLiteral("\"[^\"]*\"|'[^']*'"), fmt(colString()));
        m_blockStart = QRegularExpression(QStringLiteral("<!--"));
        m_blockEnd = QRegularExpression(QStringLiteral("-->"));
        m_blockFormat = fmt(colComment(), false, true);
        m_hasBlocks = true;
    } else if (language == QLatin1String("yaml")) {
        addRule(QStringLiteral("^\\s*[-\\w.\"']+\\s*:"), fmt(colKey(), true));
        addRule(QStringLiteral("\"[^\"]*\"|'[^']*'"), fmt(colString()));
        addRule(QStringLiteral("\\b-?\\d+(?:\\.\\d+)?\\b"), fmt(colNumber()));
        addRule(QStringLiteral("\\b(?:true|false|null|yes|no|on|off)\\b"), fmt(colKeyword()));
        addRule(QStringLiteral("#.*$"), fmt(colComment(), false, true));
    } else if (language == QLatin1String("python")) {
        static const QStringList keywords = {
            QStringLiteral("and"), QStringLiteral("as"), QStringLiteral("assert"),
            QStringLiteral("async"), QStringLiteral("await"), QStringLiteral("break"),
            QStringLiteral("class"), QStringLiteral("continue"), QStringLiteral("def"),
            QStringLiteral("del"), QStringLiteral("elif"), QStringLiteral("else"),
            QStringLiteral("except"), QStringLiteral("finally"), QStringLiteral("for"),
            QStringLiteral("from"), QStringLiteral("global"), QStringLiteral("if"),
            QStringLiteral("import"), QStringLiteral("in"), QStringLiteral("is"),
            QStringLiteral("lambda"), QStringLiteral("nonlocal"), QStringLiteral("not"),
            QStringLiteral("or"), QStringLiteral("pass"), QStringLiteral("raise"),
            QStringLiteral("return"), QStringLiteral("try"), QStringLiteral("while"),
            QStringLiteral("with"), QStringLiteral("yield"),
        };
        addRule(QStringLiteral("\\b(?:%1)\\b").arg(keywords.join(QLatin1Char('|'))),
                fmt(colKeyword(), true));
        addRule(QStringLiteral("\\b(?:True|False|None|self|cls)\\b"), fmt(colBuiltin()));
        addRule(QStringLiteral("\\bdef\\s+(\\w+)"), fmt(colKey(), true));
        addRule(QStringLiteral("\\bclass\\s+(\\w+)"), fmt(colKey(), true));
        addRule(QStringLiteral("\"(?:[^\"\\\\]|\\\\.)*\"|'(?:[^'\\\\]|\\\\.)*'"), fmt(colString()));
        addRule(QStringLiteral("\\b\\d+(?:\\.\\d+)?\\b"), fmt(colNumber()));
        addRule(QStringLiteral("#.*$"), fmt(colComment(), false, true));
        m_blockStart = QRegularExpression(QStringLiteral("\"\"\"|'''"));
        m_blockEnd = QRegularExpression(QStringLiteral("\"\"\"|'''"));
        m_blockFormat = fmt(colString());
        m_hasBlocks = true;
    } else if (language == QLatin1String("ini")) {
        addRule(QStringLiteral("^\\s*\\[[^\\]]+\\]"), fmt(colTag(), true));    // Sektion
        addRule(QStringLiteral("^\\s*[\\w.-]+(?=\\s*[=:])"), fmt(colKey()));   // Schluessel
        addRule(QStringLiteral("\"[^\"]*\"|'[^']*'"), fmt(colString()));
        addRule(QStringLiteral("\\b\\d+(?:\\.\\d+)?\\b"), fmt(colNumber()));
        addRule(QStringLiteral("[#;].*$"), fmt(colComment(), false, true));
    } else if (language == QLatin1String("shell")) {
        static const QStringList keywords = {
            QStringLiteral("if"), QStringLiteral("then"), QStringLiteral("else"),
            QStringLiteral("elif"), QStringLiteral("fi"), QStringLiteral("for"),
            QStringLiteral("while"), QStringLiteral("do"), QStringLiteral("done"),
            QStringLiteral("case"), QStringLiteral("esac"), QStringLiteral("function"),
            QStringLiteral("return"), QStringLiteral("export"), QStringLiteral("local"),
        };
        addRule(QStringLiteral("\\b(?:%1)\\b").arg(keywords.join(QLatin1Char('|'))),
                fmt(colKeyword(), true));
        addRule(QStringLiteral("\\$\\{?\\w+\\}?"), fmt(colBuiltin()));
        addRule(QStringLiteral("\"(?:[^\"\\\\]|\\\\.)*\"|'[^']*'"), fmt(colString()));
        addRule(QStringLiteral("#.*$"), fmt(colComment(), false, true));
    }
}

void SyntaxHighlighter::highlightBlock(const QString &text)
{
    for (const Rule &rule : m_rules) {
        auto it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            const auto match = it.next();
            // Gibt es eine Capture-Group, nur diese einfaerben (z.B. def NAME).
            const int group = match.lastCapturedIndex() >= 1 ? 1 : 0;
            setFormat(match.capturedStart(group), match.capturedLength(group), rule.format);
        }
    }
    if (!m_hasBlocks)
        return;

    // Mehrzeilige Bloecke (Docstrings / XML-Kommentare)
    setCurrentBlockState(0);
    int start = 0;
    if (previousBlockState() != 1) {
        const auto m = m_blockStart.match(text);
        start = m.hasMatch() ? m.capturedStart() : -1;
    }
    while (start >= 0) {
        const auto endMatch = m_blockEnd.match(text, start + 3);
        int length = 0;
        if (endMatch.hasMatch()) {
            length = endMatch.capturedEnd() - start;
        } else {
            setCurrentBlockState(1);
            length = text.length() - start;
        }
        setFormat(start, length, m_blockFormat);
        if (!endMatch.hasMatch())
            break;
        const auto next = m_blockStart.match(text, start + length);
        start = next.hasMatch() ? next.capturedStart() : -1;
    }
}

} // namespace ncssh::gui
