// Syntax-Highlighting fuer den Editor: JSON, XML/HTML, YAML, Python, INI/TOML,
// Shell. Die Sprache wird an der Dateiendung erkannt.
#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <vector>

namespace ncssh::gui {

class SyntaxHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    // language: "json" | "xml" | "yaml" | "python" | "ini" | "shell" | "" (aus)
    SyntaxHighlighter(QTextDocument *document, const QString &language);

    // Sprache aus einem Dateinamen ableiten ("" wenn unbekannt).
    static QString languageForFile(const QString &fileName);

protected:
    void highlightBlock(const QString &text) override;

private:
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    void setupRules(const QString &language);

    std::vector<Rule> m_rules;
    // Mehrzeilige Kommentare/Strings (Python-Docstrings, XML-Kommentare)
    QRegularExpression m_blockStart;
    QRegularExpression m_blockEnd;
    QTextCharFormat m_blockFormat;
    bool m_hasBlocks = false;
};

} // namespace ncssh::gui
