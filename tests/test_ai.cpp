// Tests fuer die reine KI-Logik (core/ai, core/markdown) — ohne Netzwerk.
//
// Die Streaming-Tests pruefen hier die Chunk->Ausgabe-Abbildungen
// (chatChunkText / pullChunkLine) statt einen gestubbten HTTP-Generator: das
// ist genau die Logik, die ohne laufenden Server pruefbar ist.
#include "tests/harness.hpp"

#include "ncssh/core/ai.hpp"
#include "ncssh/core/markdown.hpp"

#include <QJsonDocument>
#include <QJsonObject>

using namespace ncssh::core;

namespace {
QJsonObject json(const char *text)
{
    return QJsonDocument::fromJson(QByteArray(text)).object();
}

QString contentOf(const QJsonArray &messages, int index)
{
    return messages.at(index).toObject().value(QStringLiteral("content")).toString();
}

QString roleOf(const QJsonArray &messages, int index)
{
    return messages.at(index).toObject().value(QStringLiteral("role")).toString();
}
} // namespace

// --- Markdown ----------------------------------------------------------------
TEST(ai, md_fenced_code_block)
{
    const QString out = mdToHtml(QStringLiteral("Text\n```\nLine 1\nLine 2\n```"));
    CHECK(out.contains(QStringLiteral("<pre")));
    CHECK(out.contains(QStringLiteral("Line 1\nLine 2")));
}

TEST(ai, md_inline_code_and_bold)
{
    const QString out = mdToHtml(QStringLiteral("Setze `Listen 80` und **wichtig**."));
    CHECK(out.contains(QStringLiteral("<code")));
    CHECK(out.contains(QStringLiteral("Listen 80")));
    CHECK(out.contains(QStringLiteral("<b>wichtig</b>")));
}

TEST(ai, md_escapes_html_injection)
{
    const QString out = mdToHtml(QStringLiteral("<script>alert(1)</script>"));
    CHECK(!out.contains(QStringLiteral("<script>")));
    CHECK(out.contains(QStringLiteral("&lt;script&gt;")));
}

TEST(ai, md_lists)
{
    const QString out = mdToHtml(QStringLiteral("- eins\n- zwei"));
    CHECK_EQ(out.count(QStringLiteral("<li>")), qsizetype(2));
    CHECK(out.contains(QStringLiteral("<ul")));
}

// --- Kontext-Deckelung -------------------------------------------------------
TEST(ai, truncate_file_keeps_head)
{
    const QString text = QStringLiteral("HEAD") + QString(50000, QLatin1Char('x'))
                         + QStringLiteral("TAIL");
    const auto [out, cut] = truncateFile(text, 1000);
    CHECK_EQ(cut, true);
    CHECK(out.startsWith(QStringLiteral("HEAD")));
    CHECK(!out.contains(QStringLiteral("TAIL")));
}

TEST(ai, truncate_terminal_keeps_tail)
{
    const QString text = QStringLiteral("HEAD") + QString(50000, QLatin1Char('x'))
                         + QStringLiteral("TAIL");
    const auto [out, cut] = truncateTerminal(text, 1000);
    CHECK_EQ(cut, true);
    CHECK(out.endsWith(QStringLiteral("TAIL")));
    CHECK(!out.contains(QStringLiteral("HEAD")));
}

TEST(ai, truncate_under_limit_unchanged)
{
    const auto [out, cut] = truncateFile(QStringLiteral("kurz"));
    CHECK_EQ(out, QStringLiteral("kurz"));
    CHECK_EQ(cut, false);
}

// --- Prompt-Builder ----------------------------------------------------------
TEST(ai, build_terminal_messages_roles_and_context)
{
    const QJsonArray msgs = buildTerminalMessages(QStringLiteral("permission denied"));
    CHECK_EQ(msgs.size(), qsizetype(2));
    CHECK_EQ(roleOf(msgs, 0), QStringLiteral("system"));
    CHECK_EQ(roleOf(msgs, 1), QStringLiteral("user"));
    CHECK(contentOf(msgs, 1).contains(QStringLiteral("permission denied")));
    CHECK(contentOf(msgs, 1).contains(QStringLiteral("```")));   // Kontext im Code-Fence
}

TEST(ai, build_file_messages_custom_question)
{
    const QJsonArray msgs = buildFileMessages(QStringLiteral("httpd.conf"),
                                              QStringLiteral("Listen 80"),
                                              QStringLiteral("Wie aktiviere ich SSL?"));
    CHECK(contentOf(msgs, 1).contains(QStringLiteral("httpd.conf")));
    CHECK(contentOf(msgs, 1).contains(QStringLiteral("Wie aktiviere ich SSL?")));
}

TEST(ai, build_file_messages_default_question)
{
    const QJsonArray msgs = buildFileMessages(QStringLiteral("a.conf"), QStringLiteral("x"));
    CHECK(contentOf(msgs, 1).contains(QString::fromUtf8("Erkläre")));
}

// --- Streaming-Abbildungen ---------------------------------------------------
TEST(ai, chat_chunks_yield_token_text)
{
    QString joined;
    for (const char *raw : {R"({"message":{"role":"assistant","content":"Hallo "},"done":false})",
                            R"({"message":{"role":"assistant","content":"Welt"},"done":false})",
                            R"({"message":{"role":"assistant","content":""},"done":true})"}) {
        joined += chatChunkText(json(raw));
    }
    CHECK_EQ(joined, QStringLiteral("Hallo Welt"));
}

TEST(ai, pull_chunks_format_progress)
{
    const QString line =
        pullChunkLine(json(R"({"status":"pulling","completed":50,"total":100})"));
    CHECK_EQ(line, QStringLiteral("50\t100\tpulling"));

    const PullProgress p = parsePullLine(line);
    CHECK_EQ(p.completed, qint64(50));
    CHECK_EQ(p.total, qint64(100));
    CHECK_EQ(p.status, QStringLiteral("pulling"));
}

TEST(ai, unreachable_server_raises)
{
    // Port 1 ist praktisch nie belegt -> Verbindungsfehler muss als
    // OllamaUnreachable ankommen.
    bool threw = false;
    try {
        ollamaVersion(QStringLiteral("http://127.0.0.1:1"), 2000);
    } catch (const OllamaUnreachable &) {
        threw = true;
    } catch (const std::exception &) {
        // andere Ollama-Fehler zaehlen hier nicht
    }
    CHECK(threw);
}

// --- Quellcode-Erkennung -----------------------------------------------------
TEST(ai, source_language)
{
    CHECK_EQ(sourceLanguage(QStringLiteral("foo.py")), QStringLiteral("Python"));
    CHECK_EQ(sourceLanguage(QStringLiteral("a/b/App.java")), QStringLiteral("Java"));
    CHECK_EQ(sourceLanguage(QStringLiteral("index.PHP")), QStringLiteral("PHP"));
    CHECK_EQ(sourceLanguage(QStringLiteral("main.c")), QStringLiteral("C"));
    CHECK_EQ(sourceLanguage(QStringLiteral("script.js")), QStringLiteral("JavaScript"));
    CHECK_EQ(sourceLanguage(QStringLiteral("page.html")), QStringLiteral("HTML"));
    CHECK_EQ(sourceLanguage(QStringLiteral("legacy.bas")), QStringLiteral("BASIC"));
    CHECK_EQ(sourceLanguage(QStringLiteral("notes.txt")), QString());
    CHECK_EQ(sourceLanguage(QStringLiteral("config.json")), QString());
    CHECK_EQ(sourceLanguage(QString()), QString());
}

TEST(ai, build_codecheck_messages)
{
    const QJsonArray msgs = buildCodecheckMessages(QStringLiteral("app.py"),
                                                   QStringLiteral("print("),
                                                   QStringLiteral("Python"));
    CHECK_EQ(roleOf(msgs, 0), QStringLiteral("system"));
    CHECK(contentOf(msgs, 0).contains(QStringLiteral("Code-Reviewer")));
    CHECK(contentOf(msgs, 1).contains(QStringLiteral("app.py")));
    CHECK(contentOf(msgs, 1).contains(QStringLiteral("(Python)")));
    CHECK(contentOf(msgs, 1).contains(QStringLiteral("print(")));
}
