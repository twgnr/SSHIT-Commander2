// Syntax-Highlighting fuer den Editor. Die Sprache wird an Dateiname und
// Inhalt erkannt (Endung, bekannte Dateinamen, Shebang, <?xml/<?php …) und
// laesst sich im Editor manuell umstellen.
//
// Arbeitsweise je Zeile: zuerst faerben Regex-Regeln Schluesselwoerter, Zahlen
// usw.; danach laeuft ein kleiner Scanner ueber Strings und Kommentare und
// ueberschreibt deren Bereiche. So bleibt "http://x" ein String (kein
// Kommentar ab //) und Schluesselwoerter in Strings/Kommentaren leuchten nicht.
#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <utility>
#include <vector>

namespace ncssh::gui {

class SyntaxHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    // language: eine id aus languages() oder "" (aus).
    SyntaxHighlighter(QTextDocument *document, const QString &language);

    // Alle unterstuetzten Sprachen: (id, Anzeigename), sortiert nach Name.
    static const std::vector<std::pair<QString, QString>> &languages();
    static QString displayName(const QString &language);

    // Sprache nur aus dem Dateinamen ("" wenn unbekannt).
    static QString languageForFile(const QString &fileName);
    // Dateiname plus Dateianfang (Shebang, XML-/PHP-Kopf, JSON …).
    static QString detectLanguage(const QString &fileName, const QString &content);

    QString language() const { return m_language; }
    // Sprache wechseln und das Dokument neu einfaerben.
    void setLanguage(const QString &language);

protected:
    void highlightBlock(const QString &text) override;

private:
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    // Mehrzeiliger Bereich (Blockkommentar, Python-Docstring); Blockzustand =
    // Index + 1, solange er offen ist.
    struct Block {
        QString start;
        QString end;
        QTextCharFormat format;
    };
    void setupRules(const QString &language);
    int scanStrings(const QString &text, int from, int to);

    QString m_language;
    std::vector<Rule> m_rules;
    std::vector<Block> m_blocks;
    QRegularExpression m_lineComment;   // ungueltig = keine Zeilenkommentare
    QString m_stringDelims;             // z.B. "\"'`"
    bool m_escapes = true;              // Backslash maskiert im String
    QTextCharFormat m_stringFormat;
    QTextCharFormat m_commentFormat;
};

} // namespace ncssh::gui
