// (Port von core/ai.py + net/ollama.py)
#include "ncssh/core/ai.hpp"

#include "ncssh/core/settings.hpp"

#include <QEventLoop>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QTimer>
#include <QUrl>

namespace ncssh::core {

// --- Einstellungen -----------------------------------------------------------

bool aiEnabled()
{
    return getSettingBool(QString::fromLatin1(AI_ENABLED), false);
}

QString ollamaUrl()
{
    const QString url = getSettingString(QString::fromLatin1(OLLAMA_URL));
    return url.isEmpty() ? QString::fromLatin1(OLLAMA_DEFAULT_BASE_URL) : url;
}

QString aiModel()
{
    return getSettingString(QString::fromLatin1(AI_MODEL));
}

// --- Ollama-HTTP-Client ------------------------------------------------------

namespace {

QUrl makeUrl(const QString &baseUrl, const QString &path)
{
    QString base = baseUrl;
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    return QUrl(base + path);
}

QNetworkRequest makeRequest(const QString &baseUrl, const QString &path)
{
    QNetworkRequest req(makeUrl(baseUrl, path));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    return req;
}

// Blockierender Request mit JSON-Antwort. Verbindungsfehler werden als
// OllamaUnreachable geworfen (wie im Original die URLError-Familie).
QJsonObject readJson(const QString &baseUrl, const QString &path, int timeoutMs)
{
    QNetworkAccessManager nam;
    QNetworkRequest req = makeRequest(baseUrl, path);
    req.setTransferTimeout(timeoutMs);

    QNetworkReply *reply = nam.get(req);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError)
        throw OllamaUnreachable(reply->errorString().toStdString());

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        throw OllamaError(("Ungültige Antwort des Ollama-Servers: "
                           + parseError.errorString()).toStdString());
    return doc.object();
}

// Oeffnet einen POST-Request und liefert jede Antwortzeile als geparstes
// JSON-Objekt an onChunk (Streaming, zeilenweise JSON). Abbruch ueber das
// CancelToken schliesst die Verbindung — das entspricht dem Schliessen des
// HTTP-Sockets beim Generator-close() im Original.
void streamJsonLines(const QString &baseUrl, const QString &path, const QJsonObject &payload,
                     const std::function<void(const QJsonObject &)> &onChunk,
                     const CancelTokenPtr &cancel)
{
    QNetworkAccessManager nam;
    QNetworkRequest req = makeRequest(baseUrl, path);
    // Kein Timeout — Modell-Antworten/Downloads duerfen lange dauern.

    QNetworkReply *reply =
        nam.post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));

    QByteArray buffer;
    auto handleLine = [&](const QByteArray &raw) {
        const QByteArray line = raw.trimmed();
        if (line.isEmpty())
            return;
        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
            return;  // kaputte Zeilen still ueberspringen (wie im Original)
        onChunk(doc.object());
    };
    auto drainBuffer = [&] {
        int nl = -1;
        while ((nl = buffer.indexOf('\n')) >= 0) {
            const QByteArray raw = buffer.left(nl);
            buffer.remove(0, nl + 1);
            handleLine(raw);
        }
    };

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, [&] {
        buffer += reply->readAll();
        drainBuffer();
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    // Kooperativer Abbruch: Token regelmaessig pruefen und ggf. abbrechen.
    QTimer poll;
    poll.setInterval(100);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (cancel && cancel->isCancelled())
            reply->abort();
    });
    poll.start();

    loop.exec();
    poll.stop();
    reply->deleteLater();

    if (reply->error() == QNetworkReply::OperationCanceledError)
        return;  // Abbruch ist kein Fehler — Stream endet einfach
    if (reply->error() != QNetworkReply::NoError)
        throw OllamaUnreachable(reply->errorString().toStdString());

    // Rest verarbeiten (letzte Zeile darf ohne Zeilenumbruch enden).
    buffer += reply->readAll();
    drainBuffer();
    if (!buffer.isEmpty())
        handleLine(buffer);
}

QJsonArray makeMessages(const char *systemPrompt, const QString &user)
{
    QJsonObject sys;
    sys.insert(QStringLiteral("role"), QStringLiteral("system"));
    sys.insert(QStringLiteral("content"), QString::fromUtf8(systemPrompt));
    QJsonObject usr;
    usr.insert(QStringLiteral("role"), QStringLiteral("user"));
    usr.insert(QStringLiteral("content"), user);
    return QJsonArray{sys, usr};
}

const QString TRUNC_NOTE = QStringLiteral("\n\n[… gekürzt …]\n\n");

} // namespace

QString ollamaVersion(const QString &baseUrl, int timeoutMs)
{
    const QJsonObject data = readJson(baseUrl, QStringLiteral("/api/version"), timeoutMs);
    return data.value(QStringLiteral("version")).toString();
}

QJsonArray listModels(const QString &baseUrl, int timeoutMs)
{
    const QJsonObject data = readJson(baseUrl, QStringLiteral("/api/tags"), timeoutMs);
    return data.value(QStringLiteral("models")).toArray();
}

void chatStream(const QString &baseUrl, const QString &model, const QJsonArray &messages,
                const QJsonObject &options, const LineCallback &onText,
                const CancelTokenPtr &cancel)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("model"), model);
    payload.insert(QStringLiteral("messages"), messages);
    payload.insert(QStringLiteral("stream"), true);
    if (!options.isEmpty())
        payload.insert(QStringLiteral("options"), options);

    streamJsonLines(baseUrl, QStringLiteral("/api/chat"), payload,
                    [&](const QJsonObject &chunk) {
                        const QString text = chunk.value(QStringLiteral("message"))
                                                 .toObject()
                                                 .value(QStringLiteral("content"))
                                                 .toString();
                        if (!text.isEmpty() && onText)
                            onText(text);
                    },
                    cancel);
}

void pullStream(const QString &baseUrl, const QString &model,
                const LineCallback &onLine, const CancelTokenPtr &cancel)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("name"), model);
    payload.insert(QStringLiteral("stream"), true);

    streamJsonLines(baseUrl, QStringLiteral("/api/pull"), payload,
                    [&](const QJsonObject &chunk) {
                        const qint64 completed =
                            static_cast<qint64>(chunk.value(QStringLiteral("completed")).toDouble(0));
                        const qint64 total =
                            static_cast<qint64>(chunk.value(QStringLiteral("total")).toDouble(0));
                        const QString status = chunk.value(QStringLiteral("status")).toString();
                        if (onLine)
                            onLine(QStringLiteral("%1\t%2\t%3")
                                       .arg(completed).arg(total).arg(status));
                    },
                    cancel);
}

PullProgress parsePullLine(const QString &line)
{
    const QStringList parts = line.split(QLatin1Char('\t'));
    if (parts.size() != 3)
        return {0, 0, line};
    bool okCompleted = false;
    bool okTotal = false;
    const qint64 completed = parts.at(0).toLongLong(&okCompleted);
    const qint64 total = parts.at(1).toLongLong(&okTotal);
    if (!okCompleted || !okTotal)
        return {0, 0, parts.at(2)};
    return {completed, total, parts.at(2)};
}

// --- Kontext-Deckelung -------------------------------------------------------

std::pair<QString, bool> truncateFile(const QString &text, int limit)
{
    if (text.size() <= limit)
        return {text, false};
    QString note = TRUNC_NOTE;
    while (note.endsWith(QLatin1Char('\n')))
        note.chop(1);
    return {text.left(limit) + note, true};
}

std::pair<QString, bool> truncateTerminal(const QString &text, int limit)
{
    if (text.size() <= limit)
        return {text, false};
    QString note = TRUNC_NOTE;
    while (note.startsWith(QLatin1Char('\n')))
        note.remove(0, 1);
    return {note + text.right(limit), true};
}

// --- Prompts -----------------------------------------------------------------

const char *const SYSTEM_TERMINAL =
    "Du bist ein hilfreicher Assistent für Linux/Windows-Systemadministration in "
    "einem Datei-Manager mit SSH-Terminal. Erkläre Terminalausgaben und Fehler "
    "präzise und knapp auf Deutsch. Nenne die wahrscheinliche Ursache und konkrete "
    "nächste Schritte. Schlage Befehle nur als Vorschlag in Code-Blöcken vor — "
    "du führst selbst NICHTS aus und hast keinen Zugriff auf das System.";

const char *const SYSTEM_FILE =
    "Du bist Experte für Konfigurationsdateien (Apache, nginx, systemd, SSH, YAML, "
    "INI usw.). Beantworte Fragen zur gezeigten Datei präzise auf Deutsch und gib "
    "die minimal nötigen Änderungen als Code-Block an. Du änderst nichts selbst — "
    "du gibst nur Empfehlungen.";

const char *const SYSTEM_REPAIR =
    "Du bist Experte für Zeichenkodierungen. Der folgende Text wurde durch eine "
    "falsche Encoding-Konvertierung beschädigt (Mojibake, durch '?' oder '\xEF\xBF\xBD' "
    "ersetzte Zeichen, kaputte Umlaute/Sonderzeichen). Rekonstruiere den "
    "ursprünglich gemeinten Text so genau wie möglich aus dem Kontext. "
    "Behalte Struktur, Zeilenumbrüche, Einrückung und alle bereits korrekten "
    "Zeichen EXAKT bei und erfinde keinen neuen Inhalt. Gib AUSSCHLIESSLICH den "
    "korrigierten Text zurück — keine Erklärungen, keine Code-Blöcke, keine "
    "Anführungszeichen darum.";

const char *const SYSTEM_CODECHECK =
    "Du bist ein strenger Code-Reviewer und reiner Fehler-Detektor. Deine EINZIGE "
    "Aufgabe ist es, im gezeigten Quellcode konkrete Fehler zu finden: Syntaxfehler, "
    "Bugs und Logikfehler, Sicherheitslücken, riskante oder veraltete Konstrukte "
    "sowie klare Performance-Probleme.\n"
    "WICHTIG: Beschreibe NICHT, was der Code tut. Gib KEINE Zusammenfassung, KEINE "
    "allgemeine Erklärung des Codes, KEINE Stil- oder Lob-Kommentare und keine "
    "Verbesserungsideen, die keinen Fehler betreffen.\n"
    "Gib AUSSCHLIESSLICH eine Liste der gefundenen Fehler aus. Je Eintrag: "
    "Schweregrad (Kritisch / Wichtig / Hinweis), betroffene Stelle (Zeile bzw. "
    "Funktion), die Ursache und ein konkreter Korrekturvorschlag als Code-Block.\n"
    "Findest du keinen Fehler, antworte NUR mit dem Satz: „Keine Fehler gefunden.\" "
    "Antworte auf Deutsch. Du änderst nichts selbst.";

namespace {
const char *const DEFAULT_TERMINAL_Q =
    "Erkläre diese Ausgabe. Gibt es einen Fehler, und wie behebe ich ihn?";
const char *const DEFAULT_FILE_Q =
    "Erkläre diese Konfiguration und nenne die wichtigsten Optionen.";
} // namespace

QJsonArray buildRepairMessages(const QString &text)
{
    const QString user =
        QStringLiteral("Korrigiere den folgenden, durch falsches Encoding beschädigten Text "
                       "und gib nur den korrigierten Text aus:\n\n") + text;
    return makeMessages(SYSTEM_REPAIR, user);
}

QJsonArray buildTerminalMessages(const QString &output, const QString &question)
{
    QString q = question.trimmed();
    if (q.isEmpty())
        q = QString::fromUtf8(DEFAULT_TERMINAL_Q);
    const QString user =
        QStringLiteral("Terminalausgabe:\n```\n%1\n```\n\n%2").arg(output, q);
    return makeMessages(SYSTEM_TERMINAL, user);
}

QJsonArray buildFileMessages(const QString &filename, const QString &content,
                             const QString &question)
{
    QString q = question.trimmed();
    if (q.isEmpty())
        q = QString::fromUtf8(DEFAULT_FILE_Q);
    const QString user =
        QStringLiteral("Datei `%1`:\n```\n%2\n```\n\nFrage: %3").arg(filename, content, q);
    return makeMessages(SYSTEM_FILE, user);
}

// --- Quellcode-Fehleranalyse -------------------------------------------------

namespace {
// Dateiendung -> Sprachname (breit gefasst; bestimmt den "KI Fehleranalyse"-Button).
const QHash<QString, QString> &sourceLangs()
{
    static const QHash<QString, QString> langs = {
        {QStringLiteral(".py"), QStringLiteral("Python")},
        {QStringLiteral(".pyw"), QStringLiteral("Python")},
        {QStringLiteral(".pyi"), QStringLiteral("Python")},
        {QStringLiteral(".js"), QStringLiteral("JavaScript")},
        {QStringLiteral(".mjs"), QStringLiteral("JavaScript")},
        {QStringLiteral(".cjs"), QStringLiteral("JavaScript")},
        {QStringLiteral(".jsx"), QStringLiteral("JavaScript")},
        {QStringLiteral(".ts"), QStringLiteral("TypeScript")},
        {QStringLiteral(".tsx"), QStringLiteral("TypeScript")},
        {QStringLiteral(".html"), QStringLiteral("HTML")},
        {QStringLiteral(".htm"), QStringLiteral("HTML")},
        {QStringLiteral(".xhtml"), QStringLiteral("HTML")},
        {QStringLiteral(".css"), QStringLiteral("CSS")},
        {QStringLiteral(".scss"), QStringLiteral("SCSS")},
        {QStringLiteral(".sass"), QStringLiteral("Sass")},
        {QStringLiteral(".less"), QStringLiteral("Less")},
        {QStringLiteral(".php"), QStringLiteral("PHP")},
        {QStringLiteral(".phtml"), QStringLiteral("PHP")},
        {QStringLiteral(".php3"), QStringLiteral("PHP")},
        {QStringLiteral(".php4"), QStringLiteral("PHP")},
        {QStringLiteral(".php5"), QStringLiteral("PHP")},
        {QStringLiteral(".c"), QStringLiteral("C")},
        {QStringLiteral(".h"), QStringLiteral("C")},
        {QStringLiteral(".cpp"), QStringLiteral("C++")},
        {QStringLiteral(".cc"), QStringLiteral("C++")},
        {QStringLiteral(".cxx"), QStringLiteral("C++")},
        {QStringLiteral(".hpp"), QStringLiteral("C++")},
        {QStringLiteral(".hh"), QStringLiteral("C++")},
        {QStringLiteral(".hxx"), QStringLiteral("C++")},
        {QStringLiteral(".cs"), QStringLiteral("C#")},
        {QStringLiteral(".java"), QStringLiteral("Java")},
        {QStringLiteral(".go"), QStringLiteral("Go")},
        {QStringLiteral(".rs"), QStringLiteral("Rust")},
        {QStringLiteral(".rb"), QStringLiteral("Ruby")},
        {QStringLiteral(".swift"), QStringLiteral("Swift")},
        {QStringLiteral(".kt"), QStringLiteral("Kotlin")},
        {QStringLiteral(".kts"), QStringLiteral("Kotlin")},
        {QStringLiteral(".scala"), QStringLiteral("Scala")},
        {QStringLiteral(".dart"), QStringLiteral("Dart")},
        {QStringLiteral(".lua"), QStringLiteral("Lua")},
        {QStringLiteral(".pl"), QStringLiteral("Perl")},
        {QStringLiteral(".pm"), QStringLiteral("Perl")},
        {QStringLiteral(".r"), QStringLiteral("R")},
        {QStringLiteral(".groovy"), QStringLiteral("Groovy")},
        {QStringLiteral(".sh"), QStringLiteral("Shell")},
        {QStringLiteral(".bash"), QStringLiteral("Shell")},
        {QStringLiteral(".zsh"), QStringLiteral("Shell")},
        {QStringLiteral(".ps1"), QStringLiteral("PowerShell")},
        {QStringLiteral(".psm1"), QStringLiteral("PowerShell")},
        {QStringLiteral(".bat"), QStringLiteral("Batch")},
        {QStringLiteral(".cmd"), QStringLiteral("Batch")},
        {QStringLiteral(".bas"), QStringLiteral("BASIC")},
        {QStringLiteral(".vb"), QStringLiteral("Visual Basic")},
        {QStringLiteral(".vbs"), QStringLiteral("VBScript")},
        {QStringLiteral(".sql"), QStringLiteral("SQL")},
        {QStringLiteral(".vue"), QStringLiteral("Vue")},
        {QStringLiteral(".svelte"), QStringLiteral("Svelte")},
    };
    return langs;
}
} // namespace

QString sourceLanguage(const QString &filename)
{
    // Wie os.path.splitext: Endung ab dem letzten Punkt; ein fuehrender Punkt
    // (".bashrc") zaehlt nicht als Endung.
    const QString base = QFileInfo(filename).fileName();
    const int dot = base.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0)
        return {};
    return sourceLangs().value(base.mid(dot).toLower());
}

QJsonArray buildCodecheckMessages(const QString &filename, const QString &content,
                                  const QString &language)
{
    const QString lang =
        language.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(language);
    const QString user =
        QStringLiteral("Finde Fehler im folgenden Quellcode%1 aus der Datei `%2`. "
                       "Erkläre NICHT, was der Code macht — liste ausschließlich konkrete "
                       "Fehler. Gibt es keine, antworte nur mit „Keine Fehler gefunden.\":"
                       "\n\n```\n%3\n```")
            .arg(lang, filename, content);
    return makeMessages(SYSTEM_CODECHECK, user);
}

} // namespace ncssh::core
