#include "ncssh/core/macroactions.hpp"

#include <QDateTime>
#include <QDesktopServices>
#include <QEventLoop>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QScreen>
#include <QThread>
#include <QUrl>
#include <algorithm>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace ncssh::core::macroactions {

const std::vector<ActionSpec> &actionSpecs()
{
    static const std::vector<ActionSpec> specs = {
        // --- Programme / System ---
        {QStringLiteral("execute"), QStringLiteral("Programm/Befehl ausführen"),
         QStringLiteral("Programme & System"), QStringLiteral("text"),
         QStringLiteral("Befehl oder vollständiger Pfad zur ausführbaren Datei.\nBeispiel: C:\\Windows\\System32\\calc.exe")},
        {QStringLiteral("smart_open"), QStringLiteral("Öffnen oder fokussieren"),
         QStringLiteral("Programme & System"), QStringLiteral("text"),
         QStringLiteral("Läuft die App bereits, wird ihr Fenster fokussiert, sonst gestartet.\n"
                        "Syntax: Start | Prozessname | Fenstertitel  (die letzten beiden optional).\n"
                        "Beispiel: notepad.exe | notepad.exe | Editor")},
        {QStringLiteral("open"), QStringLiteral("Datei/URL/Ordner öffnen"),
         QStringLiteral("Programme & System"), QStringLiteral("text"),
         QStringLiteral("Öffnet Pfad oder URL mit dem Standardprogramm.\nBeispiel: https://example.com")},
        {QStringLiteral("screenshot"), QStringLiteral("Bildschirmfoto"),
         QStringLiteral("Programme & System"), QStringLiteral("text"),
         QStringLiteral("Speichert ein Bildschirmfoto (PNG) und legt es in die Zwischenablage.\n"
                        "Zielordner angeben (leer = Bilder/Home). Beispiel: C:\\Screenshots"), false, true},
        {QStringLiteral("lock_screen"), QStringLiteral("Bildschirm sperren"),
         QStringLiteral("Programme & System"), QStringLiteral("none")},
        {QStringLiteral("monitor_off"), QStringLiteral("Monitore ausschalten"),
         QStringLiteral("Programme & System"), QStringLiteral("none")},
        // --- Tastatur ---
        {QStringLiteral("write"), QStringLiteral("Text tippen"), QStringLiteral("Tastatur"),
         QStringLiteral("text"), QStringLiteral("Tippt den Text Zeichen für Zeichen.\nBeispiel: Hallo Welt")},
        {QStringLiteral("insert_text"), QStringLiteral("Text einfügen"), QStringLiteral("Tastatur"),
         QStringLiteral("text"),
         QStringLiteral("Fügt Text über die Zwischenablage ein (schnell, erhält Zeilenumbrüche).\n"
                        "Beispiel: Mit freundlichen Grüßen")},
        {QStringLiteral("hotkey"), QStringLiteral("Tastenkürzel"), QStringLiteral("Tastatur"),
         QStringLiteral("text"), QStringLiteral("Tastenkombination, mit + getrennt.\nBeispiel: ctrl+shift+m")},
        {QStringLiteral("key_press"), QStringLiteral("Taste drücken (halten)"), QStringLiteral("Tastatur"),
         QStringLiteral("text"), QStringLiteral("Drückt eine Taste und hält sie.\nBeispiel: shift")},
        {QStringLiteral("key_release"), QStringLiteral("Taste loslassen"), QStringLiteral("Tastatur"),
         QStringLiteral("text"), QStringLiteral("Lässt eine zuvor gedrückte Taste los.\nBeispiel: shift")},
        {QStringLiteral("toggle_key"), QStringLiteral("Modifier umschalten"), QStringLiteral("Tastatur"),
         QStringLiteral("text"), QStringLiteral("Erster Druck hält den Modifier, zweiter lässt ihn los.\nBeispiel: alt")},
        {QStringLiteral("insert_datetime"), QStringLiteral("Datum/Uhrzeit einfügen"), QStringLiteral("Tastatur"),
         QStringLiteral("text"),
         QStringLiteral("Fügt das aktuelle Datum/die Uhrzeit ein (Format wie strftime).\n"
                        "Leer = %d.%m.%Y %H:%M. Beispiel: %Y-%m-%d")},
        {QStringLiteral("toggle_key_timer"), QStringLiteral("Taste halten (mit Timer)"), QStringLiteral("Tastatur"),
         QStringLiteral("text"),
         QStringLiteral("Drückt die Taste und lässt sie nach X Sekunden automatisch los.\n"
                        "Format: Taste|Sekunden  (z. B. ctrl|3)")},
        // --- Maus ---
        {QStringLiteral("mouse_move"), QStringLiteral("Maus bewegen"), QStringLiteral("Maus"),
         QStringLiteral("json"), QStringLiteral("JSON mit Zielkoordinaten.\nBeispiel: {\"x\": 100, \"y\": 200}")},
        {QStringLiteral("mouse_click"), QStringLiteral("Mausklick"), QStringLiteral("Maus"),
         QStringLiteral("json"), QStringLiteral("JSON.\nBeispiel: {\"x\": 100, \"y\": 200, \"button\": \"left\", \"pressed\": true}")},
        {QStringLiteral("mouse_scroll"), QStringLiteral("Mausrad"), QStringLiteral("Maus"),
         QStringLiteral("json"), QStringLiteral("JSON mit Scroll-Delta.\nBeispiel: {\"dx\": 0, \"dy\": -2}")},
        // --- Medien & Audio ---
        {QStringLiteral("media_play_pause"), QStringLiteral("Wiedergabe/Pause"), QStringLiteral("Medien & Audio"), QStringLiteral("none")},
        {QStringLiteral("media_next"), QStringLiteral("Nächster Titel"), QStringLiteral("Medien & Audio"), QStringLiteral("none")},
        {QStringLiteral("media_previous"), QStringLiteral("Vorheriger Titel"), QStringLiteral("Medien & Audio"), QStringLiteral("none")},
        {QStringLiteral("media_stop"), QStringLiteral("Stopp"), QStringLiteral("Medien & Audio"), QStringLiteral("none")},
        {QStringLiteral("volume_up"), QStringLiteral("Lauter"), QStringLiteral("Medien & Audio"),
         QStringLiteral("number"), QStringLiteral("Anzahl der Schritte (leer = 1).\nBeispiel: 2")},
        {QStringLiteral("volume_down"), QStringLiteral("Leiser"), QStringLiteral("Medien & Audio"),
         QStringLiteral("number"), QStringLiteral("Anzahl der Schritte (leer = 1).\nBeispiel: 2")},
        {QStringLiteral("toggle_mute"), QStringLiteral("Stummschalten"), QStringLiteral("Medien & Audio"), QStringLiteral("none")},
        {QStringLiteral("set_audio_device"), QStringLiteral("Audiogerät wählen"), QStringLiteral("Medien & Audio"),
         QStringLiteral("text"), QStringLiteral("Name (Teilstring) des Ausgabegeräts.\nBeispiel: Kopfhörer")},
        // --- Fenster ---
        {QStringLiteral("focus_window"), QStringLiteral("Fenster fokussieren"), QStringLiteral("Fenster"),
         QStringLiteral("text"),
         QStringLiteral("Holt das Fenster mit passendem Titel in den Vordergrund.\nTitel (Teilstring) angeben. Beispiel: Editor")},
        {QStringLiteral("window_management"), QStringLiteral("Fenster verwalten"), QStringLiteral("Fenster"),
         QStringLiteral("window"), QStringLiteral("Fenster anordnen.")},
        {QStringLiteral("cycle_windows"), QStringLiteral("Fenster durchschalten"), QStringLiteral("Fenster"),
         QStringLiteral("text"),
         QStringLiteral("Fenster einer App nacheinander fokussieren. Namen/Titel mit | oder ,\ntrennen (z. B. chrome | explorer).")},
        // --- Layer (vom Fenster behandelt) ---
        {QStringLiteral("layer"), QStringLiteral("Zu Layer wechseln"), QStringLiteral("Layer"), QStringLiteral("layer"), {}, true},
        {QStringLiteral("jump_to_layer"), QStringLiteral("Direkt zu Layer"), QStringLiteral("Layer"), QStringLiteral("layer"), {}, true},
        {QStringLiteral("back"), QStringLiteral("Zurück"), QStringLiteral("Layer"), QStringLiteral("none"), {}, true},
        {QStringLiteral("back_to_main"), QStringLiteral("Zum Start-Layer"), QStringLiteral("Layer"), QStringLiteral("none"), {}, true},
        // --- Netzwerk & SSH ---
        {QStringLiteral("http_request"), QStringLiteral("HTTP-Anfrage"), QStringLiteral("Netzwerk & SSH"),
         QStringLiteral("json"), QStringLiteral("JSON mit url/method/json/headers.\nBeispiel: {\"method\": \"GET\", \"url\": \"https://example.com\"}")},
        {QStringLiteral("ssh_command"), QStringLiteral("Befehl an SSH-Konsole"), QStringLiteral("Netzwerk & SSH"),
         QStringLiteral("ssh"), QStringLiteral("Sendet den Befehl an die aktive Konsole des SSHIT-Commander.")},
        {QStringLiteral("ssh_broadcast"), QStringLiteral("Befehl an alle Konsolen"), QStringLiteral("Netzwerk & SSH"),
         QStringLiteral("ssh"), QStringLiteral("Sendet den Befehl an alle Konsolen des aktiven Tabs.")},
        // --- Ablauf ---
        {QStringLiteral("delay"), QStringLiteral("Verzögerung"), QStringLiteral("Ablauf"),
         QStringLiteral("number"), QStringLiteral("Wartezeit in Sekunden (z. B. 0.5).")},
        {QStringLiteral("multi_action"), QStringLiteral("Mehrere Aktionen"), QStringLiteral("Ablauf"),
         QStringLiteral("sequence"), QStringLiteral("Mehrere Aktionen, bei jedem Druck nacheinander ausgeführt.")},
        {QStringLiteral("sequence"), QStringLiteral("Sequenz (eine pro Druck)"), QStringLiteral("Ablauf"),
         QStringLiteral("sequence"), QStringLiteral("Bei jedem Druck wird die nächste Aktion der Liste ausgeführt.")},
        // --- Sonstiges ---
        {QStringLiteral("clipboard_set"), QStringLiteral("In Zwischenablage"), QStringLiteral("Sonstiges"),
         QStringLiteral("text"), QStringLiteral("Legt den Text in die Zwischenablage.\nBeispiel: Mein Textbaustein")},
        {QStringLiteral("clipboard_clear"), QStringLiteral("Zwischenablage leeren"), QStringLiteral("Sonstiges"), QStringLiteral("none")},
        {QStringLiteral("toggle_state"), QStringLiteral("Mehrzustands-Taste"), QStringLiteral("Sonstiges"),
         QStringLiteral("json"),
         QStringLiteral("Liste von Zuständen (JSON). Jeder Druck führt die Aktion des aktuellen Zustands aus und wechselt zum nächsten\n"
                        "(Beschriftung/Icon ändern sich entsprechend)."), false, true},
        {QStringLiteral("command_cycle"), QStringLiteral("Befehl auswählen"), QStringLiteral("Sonstiges"),
         QStringLiteral("text"),
         QStringLiteral("Eine Zeile pro Eintrag. Beim Druck erscheint ein Auswahl-Menü;\nder gewählte Text wird getippt.\nBeispiel:\nls -la\ncd /var/log"), false, true},
        {QStringLiteral("clipboard_history"), QStringLiteral("Zwischenablage-Verlauf"), QStringLiteral("Sonstiges"),
         QStringLiteral("none"),
         QStringLiteral("Zeigt zuletzt kopierte Texte als Auswahl-Menü; die Auswahl wird in die Zwischenablage gelegt."), false, true},
        {QStringLiteral("none"), QStringLiteral("Keine Aktion"), QStringLiteral("Sonstiges"), QStringLiteral("none")},
    };
    return specs;
}

const ActionSpec &spec(const QString &actionType)
{
    for (const auto &s : actionSpecs()) {
        if (s.key == actionType)
            return s;
    }
    for (const auto &s : actionSpecs()) {
        if (s.key == QLatin1String("none"))
            return s;
    }
    return actionSpecs().back();
}

std::vector<std::pair<QString, std::vector<ActionSpec>>> groupedActions()
{
    std::vector<std::pair<QString, std::vector<ActionSpec>>> out;
    for (const auto &s : actionSpecs()) {
        auto it = std::find_if(out.begin(), out.end(),
                               [&](const auto &g) { return g.first == s.group; });
        if (it == out.end())
            out.push_back({s.group, {s}});
        else
            it->second.push_back(s);
    }
    return out;
}

// --- Hilfen: payload-Zugriff -----------------------------------------------

static QString payloadStr(const QJsonValue &p)
{
    if (p.isString())
        return p.toString();
    if (p.isDouble()) {
        const double d = p.toDouble();
        if (d == qint64(d))
            return QString::number(qint64(d));
        return QString::number(d);
    }
    if (p.isBool())
        return p.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    return {};
}

static void sleepMs(int ms)
{
    QThread::msleep(static_cast<unsigned long>(qMax(0, ms)));
}

#ifdef Q_OS_WIN

// --- Tastatur/Maus via WinAPI ----------------------------------------------

static WORD resolveVk(const QString &nameIn)
{
    QString name = nameIn.trimmed().toLower();
    static const QHash<QString, QString> aliases = {
        {QStringLiteral("win"), QStringLiteral("cmd")}, {QStringLiteral("windows"), QStringLiteral("cmd")},
        {QStringLiteral("super"), QStringLiteral("cmd")}, {QStringLiteral("control"), QStringLiteral("ctrl")},
        {QStringLiteral("return"), QStringLiteral("enter")}, {QStringLiteral("escape"), QStringLiteral("esc")},
    };
    name = aliases.value(name, name);
    static const QHash<QString, WORD> keys = {
        {QStringLiteral("ctrl"), VK_CONTROL}, {QStringLiteral("alt"), VK_MENU},
        {QStringLiteral("shift"), VK_SHIFT}, {QStringLiteral("cmd"), VK_LWIN},
        {QStringLiteral("enter"), VK_RETURN}, {QStringLiteral("esc"), VK_ESCAPE},
        {QStringLiteral("tab"), VK_TAB}, {QStringLiteral("space"), VK_SPACE},
        {QStringLiteral("backspace"), VK_BACK}, {QStringLiteral("delete"), VK_DELETE},
        {QStringLiteral("home"), VK_HOME}, {QStringLiteral("end"), VK_END},
        {QStringLiteral("up"), VK_UP}, {QStringLiteral("down"), VK_DOWN},
        {QStringLiteral("left"), VK_LEFT}, {QStringLiteral("right"), VK_RIGHT},
        {QStringLiteral("pageup"), VK_PRIOR}, {QStringLiteral("pagedown"), VK_NEXT},
        {QStringLiteral("f1"), VK_F1}, {QStringLiteral("f2"), VK_F2}, {QStringLiteral("f3"), VK_F3},
        {QStringLiteral("f4"), VK_F4}, {QStringLiteral("f5"), VK_F5}, {QStringLiteral("f6"), VK_F6},
        {QStringLiteral("f7"), VK_F7}, {QStringLiteral("f8"), VK_F8}, {QStringLiteral("f9"), VK_F9},
        {QStringLiteral("f10"), VK_F10}, {QStringLiteral("f11"), VK_F11}, {QStringLiteral("f12"), VK_F12},
    };
    if (keys.contains(name))
        return keys.value(name);
    if (name.size() == 1) {
        const short vk = VkKeyScanW(name.at(0).unicode());
        if (vk != -1)
            return static_cast<WORD>(vk & 0xFF);
    }
    return 0;
}

static void keyEvent(WORD vk, bool down)
{
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(INPUT));
}

static void typeUnicode(const QString &text)
{
    for (const QChar ch : text) {
        INPUT in[2] = {};
        for (int i = 0; i < 2; ++i) {
            in[i].type = INPUT_KEYBOARD;
            in[i].ki.wScan = ch.unicode();
            in[i].ki.dwFlags = KEYEVENTF_UNICODE | (i == 1 ? KEYEVENTF_KEYUP : 0);
        }
        SendInput(2, in, sizeof(INPUT));
    }
}

static void doHotkey(const QString &payload)
{
    std::vector<WORD> mods, keys;
    static const QHash<QString, WORD> modMap = {
        {QStringLiteral("ctrl"), VK_CONTROL}, {QStringLiteral("alt"), VK_MENU},
        {QStringLiteral("shift"), VK_SHIFT}, {QStringLiteral("cmd"), VK_LWIN},
        {QStringLiteral("win"), VK_LWIN}, {QStringLiteral("super"), VK_LWIN},
    };
    for (const QString &raw : payload.toLower().split(QLatin1Char('+'))) {
        const QString part = raw.trimmed();
        if (part.isEmpty())
            continue;
        if (modMap.contains(part))
            mods.push_back(modMap.value(part));
        else
            keys.push_back(resolveVk(part));
    }
    for (WORD m : mods)
        keyEvent(m, true);
    for (WORD k : keys) {
        keyEvent(k, true);
        keyEvent(k, false);
    }
    for (auto it = mods.rbegin(); it != mods.rend(); ++it)
        keyEvent(*it, false);
}

static void winSetClipboard(const QString &text)
{
    const QByteArray data(reinterpret_cast<const char *>(text.utf16()),
                          (text.size() + 1) * 2);
    if (!OpenClipboard(nullptr))
        throw std::runtime_error("OpenClipboard fehlgeschlagen");
    EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, data.size());
    if (h) {
        void *ptr = GlobalLock(h);
        memcpy(ptr, data.constData(), data.size());
        GlobalUnlock(h);
        SetClipboardData(CF_UNICODETEXT, h);
    }
    CloseClipboard();
}

static QString winGetClipboard()
{
    if (!OpenClipboard(nullptr))
        return {};
    QString out;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t *ptr = static_cast<const wchar_t *>(GlobalLock(h))) {
            out = QString::fromWCharArray(ptr);
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return out;
}

static void insertText(const QString &content)
{
    QString original;
    try {
        original = winGetClipboard();
    } catch (...) {
    }
    winSetClipboard(content);
    sleepMs(100);
    doHotkey(QStringLiteral("ctrl+v"));
    sleepMs(100);
    try {
        winSetClipboard(original);
    } catch (...) {
    }
}

static void pressMedia(const QString &actionType, int repeat)
{
    static const QHash<QString, WORD> vk = {
        {QStringLiteral("media_play_pause"), 0xB3}, {QStringLiteral("media_next"), 0xB0},
        {QStringLiteral("media_previous"), 0xB1}, {QStringLiteral("media_stop"), 0xB2},
        {QStringLiteral("volume_up"), 0xAF}, {QStringLiteral("volume_down"), 0xAE},
        {QStringLiteral("toggle_mute"), 0xAD},
    };
    const WORD code = vk.value(actionType, 0);
    if (!code)
        throw std::runtime_error(("Mediensteuerung nicht verfügbar: " + actionType).toStdString());
    for (int i = 0; i < qMax(1, repeat); ++i) {
        keybd_event(static_cast<BYTE>(code), 0, 0, 0);
        keybd_event(static_cast<BYTE>(code), 0, KEYEVENTF_KEYUP, 0);
        sleepMs(20);
    }
}

// --- Fenster-Management via WinAPI -----------------------------------------

struct WinMatch {
    QString needle;
    HWND found = nullptr;
};

static BOOL CALLBACK findByTitleProc(HWND hwnd, LPARAM lparam)
{
    auto *m = reinterpret_cast<WinMatch *>(lparam);
    if (!IsWindowVisible(hwnd))
        return TRUE;
    wchar_t buf[512];
    const int n = GetWindowTextW(hwnd, buf, 512);
    const QString title = QString::fromWCharArray(buf, n);
    if (!title.isEmpty() && title.contains(m->needle, Qt::CaseInsensitive)) {
        m->found = hwnd;
        return FALSE;
    }
    return TRUE;
}

static HWND findWindowByTitle(const QString &title)
{
    WinMatch m{title, nullptr};
    EnumWindows(findByTitleProc, reinterpret_cast<LPARAM>(&m));
    return m.found;
}

static void activate(HWND hwnd)
{
    if (IsIconic(hwnd))
        ShowWindow(hwnd, SW_RESTORE);
    SetForegroundWindow(hwnd);
}

static void focusWindow(const QString &title)
{
    if (title.trimmed().isEmpty())
        return;
    if (HWND h = findWindowByTitle(title.trimmed()))
        activate(h);
}

static void windowAction(const QJsonValue &payload)
{
    QString command, title;
    if (payload.isObject()) {
        command = payload.toObject().value(QStringLiteral("command")).toString();
        title = payload.toObject().value(QStringLiteral("window_title")).toString();
    } else {
        command = payload.toString();
    }
    if (command.isEmpty())
        return;
    HWND target = title.isEmpty() ? GetForegroundWindow() : findWindowByTitle(title);
    if (!target)
        return;
    activate(target);

    const QRect geo = QGuiApplication::primaryScreen()->availableGeometry();
    if (command == QLatin1String("maximize")) {
        ShowWindow(target, SW_MAXIMIZE);
    } else if (command == QLatin1String("minimize")) {
        ShowWindow(target, SW_MINIMIZE);
    } else if (command == QLatin1String("snap_left") || command == QLatin1String("snap_right")) {
        ShowWindow(target, SW_RESTORE);
        const int half = geo.width() / 2;
        const int x = geo.x() + (command == QLatin1String("snap_left") ? 0 : half);
        MoveWindow(target, x, geo.y(), half, geo.height(), TRUE);
    }
}

static bool g_cycleUnused = false;

#endif // Q_OS_WIN

static void openTarget(const QString &target)
{
    const QString t = target.trimmed();
    if (t.isEmpty())
        throw std::runtime_error("Kein Pfad/keine URL angegeben.");
    const QUrl url = QUrl::fromUserInput(t);
    if (!QDesktopServices::openUrl(url))
        throw std::runtime_error(("Konnte nicht öffnen: " + t).toStdString());
}

static void httpRequest(const QJsonValue &payload)
{
    if (!payload.isObject() || !payload.toObject().contains(QStringLiteral("url")))
        throw std::runtime_error("HTTP-Aktion benötigt ein Objekt mit 'url'.");
    const QJsonObject o = payload.toObject();
    QNetworkAccessManager manager;
    manager.setTransferTimeout(10000);
    QNetworkRequest req(QUrl(o.value(QStringLiteral("url")).toString()));
    const QJsonObject headers = o.value(QStringLiteral("headers")).toObject();
    for (auto it = headers.begin(); it != headers.end(); ++it)
        req.setRawHeader(it.key().toUtf8(), it.value().toVariant().toString().toUtf8());
    QByteArray body;
    if (o.contains(QStringLiteral("json")) && !o.value(QStringLiteral("json")).isNull()) {
        body = QJsonDocument::fromVariant(o.value(QStringLiteral("json")).toVariant()).toJson();
        if (!req.hasRawHeader("Content-Type"))
            req.setRawHeader("Content-Type", "application/json");
    }
    const QString method = o.value(QStringLiteral("method")).toString(QStringLiteral("GET")).toUpper();
    QNetworkReply *reply = manager.sendCustomRequest(req, method.toUtf8(), body);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    const bool err = reply->error() != QNetworkReply::NoError;
    const QString msg = reply->errorString();
    reply->deleteLater();
    if (err)
        throw std::runtime_error(("HTTP-Anfrage fehlgeschlagen: " + msg).toStdString());
}

std::optional<QString> executeAction(const QString &actionType, const QJsonValue &payload,
                                     ExecContext *context, const QString &keyId)
{
    ExecContext local;
    ExecContext *ctx = context ? context : &local;
    try {
        const ActionSpec &s = spec(actionType);
        if (actionType == QLatin1String("none") || s.navigation || s.gui)
            return std::nullopt;  // leer / Navigation / GUI -> vom Fenster behandelt

        if (actionType == QLatin1String("execute")) {
            const QString cmd = payloadStr(payload);
#ifdef Q_OS_WIN
            QProcess::startDetached(QStringLiteral("cmd.exe"),
                                    {QStringLiteral("/c"), cmd});
#else
            QProcess::startDetached(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), cmd});
#endif
        } else if (actionType == QLatin1String("open")) {
            openTarget(payloadStr(payload));
        } else if (actionType == QLatin1String("http_request")) {
            httpRequest(payload);
        } else if (actionType == QLatin1String("delay")) {
            sleepMs(int(payloadStr(payload).toDouble() * 1000));
        } else if (actionType == QLatin1String("ssh_command")) {
            if (!ctx->sshSend)
                throw std::runtime_error("Keine aktive Konsole verfügbar.");
            QString cmd = payloadStr(payload);
            bool run = true;
            if (payload.isObject()) {
                cmd = payload.toObject().value(QStringLiteral("command")).toString();
                run = payload.toObject().value(QStringLiteral("run")).toBool(true);
            }
            ctx->sshSend(cmd, run);
        } else if (actionType == QLatin1String("ssh_broadcast")) {
            if (!ctx->sshBroadcast)
                throw std::runtime_error("Keine Konsolen verfügbar.");
            QString cmd = payloadStr(payload);
            bool run = true;
            if (payload.isObject()) {
                cmd = payload.toObject().value(QStringLiteral("command")).toString();
                run = payload.toObject().value(QStringLiteral("run")).toBool(true);
            }
            ctx->sshBroadcast(cmd, run);
        } else if (actionType == QLatin1String("multi_action")
                   || actionType == QLatin1String("sequence")) {
            for (const QJsonValue &step : payload.toArray()) {
                if (!step.isObject())
                    continue;
                const auto err = executeAction(
                    step.toObject().value(QStringLiteral("action_type")).toString(QStringLiteral("none")),
                    step.toObject().value(QStringLiteral("payload")), ctx);
                if (err)
                    return err;
            }
        } else {
#ifdef Q_OS_WIN
            if (actionType == QLatin1String("write")) {
                typeUnicode(payloadStr(payload));
            } else if (actionType == QLatin1String("insert_text")) {
                insertText(payloadStr(payload));
            } else if (actionType == QLatin1String("hotkey")) {
                doHotkey(payloadStr(payload));
            } else if (actionType == QLatin1String("key_press")) {
                keyEvent(resolveVk(payloadStr(payload)), true);
            } else if (actionType == QLatin1String("key_release")) {
                keyEvent(resolveVk(payloadStr(payload)), false);
            } else if (actionType == QLatin1String("toggle_key")) {
                const QString id = keyId.isEmpty() ? payloadStr(payload) : keyId;
                const bool held = ctx->toggleState.value(id, false);
                const WORD k = resolveVk(payloadStr(payload));
                keyEvent(k, !held);
                ctx->toggleState.insert(id, !held);
            } else if (actionType == QLatin1String("toggle_key_timer")) {
                const QStringList parts = payloadStr(payload).split(QLatin1Char('|'));
                const WORD k = resolveVk(parts.value(0));
                double secs = parts.size() > 1 ? parts.at(1).toDouble() : 1.0;
                keyEvent(k, true);
                // einfache blockierende Umsetzung (laeuft ohnehin im Worker)
                sleepMs(int(qMax(0.0, secs) * 1000));
                keyEvent(k, false);
            } else if (actionType == QLatin1String("insert_datetime")) {
                QString fmt = payloadStr(payload).trimmed();
                // strftime -> Qt-Format (die gaengigen Codes)
                QString qfmt = fmt.isEmpty() ? QStringLiteral("dd.MM.yyyy HH:mm") : fmt;
                qfmt.replace(QStringLiteral("%d"), QStringLiteral("dd"))
                    .replace(QStringLiteral("%m"), QStringLiteral("MM"))
                    .replace(QStringLiteral("%Y"), QStringLiteral("yyyy"))
                    .replace(QStringLiteral("%H"), QStringLiteral("HH"))
                    .replace(QStringLiteral("%M"), QStringLiteral("mm"))
                    .replace(QStringLiteral("%S"), QStringLiteral("ss"));
                insertText(QDateTime::currentDateTime().toString(qfmt));
            } else if (actionType == QLatin1String("clipboard_set")) {
                winSetClipboard(payloadStr(payload));
            } else if (actionType == QLatin1String("clipboard_clear")) {
                winSetClipboard(QString());
            } else if (actionType == QLatin1String("lock_screen")) {
                LockWorkStation();
            } else if (actionType == QLatin1String("monitor_off")) {
                SendMessageW(HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER, 2);
            } else if (actionType == QLatin1String("media_play_pause")
                       || actionType == QLatin1String("media_next")
                       || actionType == QLatin1String("media_previous")
                       || actionType == QLatin1String("media_stop")) {
                pressMedia(actionType, 1);
            } else if (actionType == QLatin1String("volume_up")
                       || actionType == QLatin1String("volume_down")) {
                int steps = qMax(1, int(payloadStr(payload).toDouble()));
                if (payloadStr(payload).isEmpty()) steps = 1;
                pressMedia(actionType, steps);
            } else if (actionType == QLatin1String("toggle_mute")) {
                pressMedia(QStringLiteral("toggle_mute"), 1);
            } else if (actionType == QLatin1String("focus_window")) {
                focusWindow(payloadStr(payload));
            } else if (actionType == QLatin1String("window_management")) {
                windowAction(payload);
            } else if (actionType == QLatin1String("smart_open")) {
                const QStringList parts = payloadStr(payload).split(QLatin1Char('|'));
                const QString launch = parts.value(0).trimmed();
                const QString title = parts.size() > 2 ? parts.at(2).trimmed() : QString();
                const QString search = !title.isEmpty() ? title : launch;
                if (HWND h = findWindowByTitle(search)) {
                    activate(h);
                } else {
                    QProcess::startDetached(QStringLiteral("cmd.exe"),
                                            {QStringLiteral("/c"), launch});
                }
            } else if (actionType == QLatin1String("cycle_windows")) {
                QStringList names;
                for (const QString &n : payloadStr(payload).split(QRegularExpression(QStringLiteral("[|,]"))))
                    if (!n.trimmed().isEmpty())
                        names << n.trimmed().toLower();
                if (!names.isEmpty()) {
                    // vereinfachtes Durchschalten: erstes passendes Fenster fokussieren
                    for (const QString &n : names) {
                        if (HWND h = findWindowByTitle(n)) {
                            activate(h);
                            break;
                        }
                    }
                }
            } else if (actionType == QLatin1String("mouse_move")) {
                SetCursorPos(payload.toObject().value(QStringLiteral("x")).toInt(),
                             payload.toObject().value(QStringLiteral("y")).toInt());
            } else if (actionType == QLatin1String("mouse_click")) {
                const QJsonObject o = payload.toObject();
                SetCursorPos(o.value(QStringLiteral("x")).toInt(),
                             o.value(QStringLiteral("y")).toInt());
                sleepMs(50);
                const QString btn = o.value(QStringLiteral("button")).toString(QStringLiteral("left"));
                const bool pressed = o.value(QStringLiteral("pressed")).toBool(true);
                DWORD down = btn == QLatin1String("right") ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN;
                DWORD up = btn == QLatin1String("right") ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP;
                mouse_event(pressed ? down : up, 0, 0, 0, 0);
            } else if (actionType == QLatin1String("mouse_scroll")) {
                mouse_event(MOUSEEVENTF_WHEEL, 0, 0,
                            payload.toObject().value(QStringLiteral("dy")).toInt() * 120, 0);
            } else if (actionType == QLatin1String("set_audio_device")) {
                throw std::runtime_error(
                    "Umschalten des Standard-Audiogeräts ist in dieser Version nicht "
                    "implementiert. Tipp: ein Hilfsprogramm wie 'nircmd' per Aktion "
                    "'Programm ausführen' aufrufen.");
            } else {
                throw std::runtime_error(("Unbekannter Action-Typ: " + actionType).toStdString());
            }
#else
            throw std::runtime_error(
                ("Aktion '" + actionType + "' wird auf dieser Plattform nicht unterstützt.")
                    .toStdString());
#endif
        }
        return std::nullopt;
    } catch (const std::exception &exc) {
        return QString::fromUtf8(exc.what());
    } catch (...) {
        return QStringLiteral("Unbekannter Fehler bei Aktion '%1'.").arg(actionType);
    }
}

QString toPynputHotkey(const QString &shortcut)
{
    static const QHash<QString, QString> alias = {
        {QStringLiteral("control"), QStringLiteral("ctrl")}, {QStringLiteral("win"), QStringLiteral("cmd")},
        {QStringLiteral("super"), QStringLiteral("cmd")}, {QStringLiteral("meta"), QStringLiteral("cmd")},
        {QStringLiteral("return"), QStringLiteral("enter")}, {QStringLiteral("escape"), QStringLiteral("esc")},
    };
    QStringList parts;
    for (const QString &raw : shortcut.split(QLatin1Char('+'))) {
        QString p = raw.trimmed().toLower();
        if (p.isEmpty())
            continue;
        p = alias.value(p, p);
        parts << (p.size() > 1 ? QStringLiteral("<%1>").arg(p) : p);
    }
    return parts.join(QLatin1Char('+'));
}

std::optional<QString> missingDependencyHint(const QString &actionType)
{
#ifndef Q_OS_WIN
    static const QStringList needNative = {
        QStringLiteral("write"), QStringLiteral("insert_text"), QStringLiteral("insert_datetime"),
        QStringLiteral("hotkey"), QStringLiteral("key_press"), QStringLiteral("key_release"),
        QStringLiteral("toggle_key"), QStringLiteral("toggle_key_timer"), QStringLiteral("mouse_move"),
        QStringLiteral("mouse_click"), QStringLiteral("mouse_scroll"), QStringLiteral("window_management"),
        QStringLiteral("smart_open"), QStringLiteral("cycle_windows"), QStringLiteral("focus_window"),
        QStringLiteral("media_play_pause"), QStringLiteral("lock_screen"), QStringLiteral("monitor_off"),
    };
    if (needNative.contains(actionType))
        return QStringLiteral("Diese Aktion wird auf dieser Plattform nicht unterstützt.");
#else
    Q_UNUSED(actionType);
#endif
    return std::nullopt;
}

} // namespace ncssh::core::macroactions
